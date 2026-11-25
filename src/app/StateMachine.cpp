#include "StateMachine.h"
#include "Calculations.h"
#include "../storage/Settings.h"
#include "../util/Logger.h"
#include "config.h"
#include <math.h>

StateMachine g_sm;

// ─── helpers ──────────────────────────────────────────────────────────────

bool StateMachine::sleepAllowed(Screen s) {
    switch (s) {
        case Screen::PARKING:
        case Screen::CHARGING:
        case Screen::DOSING:
        case Screen::CALIBRATING:
            return false;
        default:
            return true;
    }
}

int32_t StateMachine::mmToSteps(float mm) {
    float pitch = g_state.screwPitch > 0.0f ? g_state.screwPitch : 1.0f;
    return (int32_t)(mm / pitch * FULL_STEPS_PER_REV + 0.5f);
}

// Предел хода к концевику: калиброванный H с запасом, до калибровки — MAX_TRAVEL_MM.
// Позиция после включения не известна, поэтому предел — от текущей точки, а не от нуля.
int32_t StateMachine::travelLimit() const {
    int32_t H = g_state.fullPathSteps;
    if (H <= 0) return mmToSteps(MAX_TRAVEL_MM);
    return H + max(H / 100 * TRAVEL_SPARE_PCT, mmToSteps(TRAVEL_SPARE_MM));
}

// Ход к концевику: запускаем мотор и заводим сторож. Сбой ловит moveStuck(): мотор доехал до
// цели (предела хода), а концевик не сработал, или вышел таймаут по времени (страховка).
void StateMachine::startMove(int32_t target, float speed, uint32_t nowMs) {
    float spd  = max(1.0f, speed);
    int32_t from = m_stepper ? m_stepper->currentPosition() : 0;
    float dist = (float)abs(target - from);
    float ms   = dist / spd * 1000.0f * STUCK_TIME_FACTOR + STUCK_EXTRA_MS;
    m_moveTimeoutMs = (uint32_t)max((float)STUCK_MIN_MS, min(ms, 3.6e6f));   // не больше часа
    m_moveStartMs   = nowMs;
    if (m_stepper) {
        m_stepper->enable();
        m_stepper->moveTo(target, spd);
    }
}

bool StateMachine::moveStuck(uint32_t nowMs) {
    // Сбой хода к концевику: слишком долго ИЛИ мотор доехал до цели, а концевик так и не сработал.
    if (nowMs - m_moveStartMs > m_moveTimeoutMs) return true;
    if (m_stepper && !m_stepper->isBusy())       return true;
    return false;
}

void StateMachine::jogTo(int32_t target) {
    if (!m_stepper) return;
    // Не трогаемся в сторону уже нажатого концевика.
    SwitchSnapshot sw = m_switches ? m_switches->read() : SwitchSnapshot{false, false, false, false};
    int32_t pos = m_stepper->currentPosition();
    if ((target < pos && sw.top) || (target > pos && sw.bot)) {
        m_stepper->stop();
        m_jogHolding = false;
        requestBroadcast();
        return;
    }
    m_stepper->enable();
    m_stepper->moveTo(target, g_state.jogSpeed);
    requestBroadcast();
}

// Ручной режим: сдвинуть мотор на jogSteps (dir<0 вверх, dir>0 вниз).
void StateMachine::jog(int dir) {
    if (g_state.screen != Screen::DEBUG || !m_stepper) return;
    m_jogHolding = false;
    int32_t step = (dir < 0) ? -g_state.jogSteps : g_state.jogSteps;
    jogTo(m_stepper->currentPosition() + step);
}

// Ручной режим: сдвиг на steps от текущей цели — щелчки энкодера пульта складываются.
void StateMachine::jogBy(int32_t steps) {
    if (g_state.screen != Screen::DEBUG || !m_stepper || steps == 0) return;
    m_jogHolding = false;
    int32_t base = m_stepper->isBusy() ? m_stepper->targetPosition() : m_stepper->currentPosition();
    jogTo(base + steps);
}

// Ручной режим: движение по удержанию. Клиент повторяет команду, пока держит кнопку;
// если повтор не пришёл за JOG_HOLD_DEADMAN_MS (обрыв связи, телефон уснул) — стоп.
void StateMachine::jogHold(int dir) {
    if (g_state.screen != Screen::DEBUG || !m_stepper) return;
    m_jogHoldUntilMs = millis() + JOG_HOLD_DEADMAN_MS;
    int32_t pos = m_stepper->currentPosition();
    int32_t tgt = m_stepper->targetPosition();
    bool sameDir = m_jogHolding && m_stepper->isBusy() && ((tgt - pos) > 0) == (dir > 0);
    m_jogHolding = true;
    if (sameDir) return;                         // повтор удержания — только продлеваем deadman
    jogTo(pos + ((dir < 0) ? -FAR_MOVE_STEPS : FAR_MOVE_STEPS));
}

// Ручной режим: остановить движение (отпустили кнопку).
void StateMachine::jogStop() {
    m_jogHolding = false;
    if (g_state.screen == Screen::DEBUG && m_stepper) {
        m_stepper->stop();
        requestBroadcast();
    }
}

// Двухфазная кнопка (физическая D3 и глоб. кнопка UI): СТОП → (после стопа) Парковка.
void StateMachine::stopButtonPress() {
    if (g_state.screen == Screen::STOPPED) {
        if (g_state.otaActive) return;          // пишется прошивка — никуда не едем
        // Только что остановили (с пульта, из веба или этой же кнопкой) и жмут
        // «для верности» — это не просьба ехать на парковку.
        if (millis() - m_enteredMs < STOP_PARK_GUARD_MS) return;
        transitionTo(Screen::PARKING);
    } else {
        g_state.stopCause = StopCause::MANUAL;
        transitionTo(Screen::STOPPED);
    }
}

void StateMachine::begin(IStepperDriver* stepper, ILimitSwitches* switches, Settings* settings) {
    m_stepper  = stepper;
    m_switches = switches;
    m_settings = settings;
}

void StateMachine::transitionTo(Screen next) {
    LOG_INFO("SM: %d -> %d", (int)g_state.screen, (int)next);

    uint32_t now = millis();
    g_state.screen           = next;
    m_enteredMs              = now;
    g_state.editing          = false;
    g_state.displaySleeping  = false;
    g_state.lastInteractionMs = now;
    m_jogHolding             = false;

    SwitchSnapshot sw = m_switches ? m_switches->read() : SwitchSnapshot{false, false, false, false};

    switch (next) {
        case Screen::IDLE:
            // Простой: ничего не двигаем (никаких шагов при старте).
            g_state.switches = {false, false, false, false};
            if (m_stepper) { m_stepper->stop(); m_stepper->disable(); }
            break;

        case Screen::PARKED:
            g_state.switches = {true, false, false, false};
            if (m_stepper) { m_stepper->stop(); m_stepper->disable(); }
            break;

        case Screen::CHARGING:
            // Плавный спуск к концевикам A/B, но не дальше H: tick() остановит по прижатию
            // обоих. Доехали до H или нажался BOT, а A/B нет — сбой (нет шприцев, концевик).
            g_state.switches = {false, false, false, false};
            startMove(g_state.fullPathSteps, g_state.chargeSpeed, now);
            break;

        case Screen::CHARGED:
            g_state.switches.a = true;
            g_state.switches.b = true;
            if (m_stepper) m_stepper->stop();   // оба концевика прижаты — стоп
            break;

        case Screen::DOSING: {
            // Дистанционная модель: доза = остаток L2 = H − L (заряд→конец) за время t.
            int32_t L  = m_stepper ? m_stepper->currentPosition() : 0;  // позиция заряда (2)
            int32_t H  = g_state.fullPathSteps;                         // полный ход (калибровка)
            int32_t L2 = H - L;
            if (L2 <= 0) {
                // Заряд ниже H: калибровка устарела или H поправили вручную. Не «готово», а ошибка.
                LOG_WARN("DOSING refused: L=%ld >= H=%ld", (long)L, (long)H);
                g_state.stopCause = StopCause::BAD_CHARGE;
                transitionTo(Screen::STOPPED);
                return;
            }
            g_state.dosing = {0, g_state.doseTimeMin * 60.0f, 0, 0, 0};
            m_dosingStartMs     = now;
            m_dosingStartPos    = L;
            m_dosingTargetSteps = L2;
            // Плановый объём дозы по расстоянию L2.
            float l2mm = L2 * (g_state.screwPitch / (float)FULL_STEPS_PER_REV);
            g_state.planVolA = calc::distToVolumeMl(g_state.syringeA.diameter, l2mm);
            g_state.planVolB = calc::distToVolumeMl(g_state.syringeB.diameter, l2mm);
            if (m_stepper) {
                float t = max(1.0f, g_state.dosing.totalSec);
                float speed = constrain((float)L2 / t, DOSE_SPS_MIN, MOTOR_SPS_MAX);   // V = L2 / t
                m_stepper->enable();
                m_stepper->moveTo(H, speed);                              // едем в позицию 3
            }
            break;
        }

        case Screen::CALIBRATING:
            g_state.switches = {false, false, false, false};
            g_state.calibPhase = 0;
            m_calibStep = 0;
            if (m_stepper) {
                // Калибровка меряет H заново: предел хода — MAX_TRAVEL_MM, а не старый H
                // (механику могли переделать). Сломанный BOT — сбой примерно за 100 с при 2 мм/с.
                if (sw.top) {                  // уже дома → сразу спуск к концу (фаза 1)
                    m_stepper->stop();
                    m_stepper->zero();
                    g_state.calibPhase = 1;
                    startMove(mmToSteps(MAX_TRAVEL_MM), g_state.parkSpeed, now);
                } else {                       // сначала к дому (TOP)
                    startMove(m_stepper->currentPosition() - mmToSteps(MAX_TRAVEL_MM), g_state.parkSpeed, now);
                }
            }
            break;

        case Screen::DONE:
            if (m_stepper) m_stepper->stop();
            break;

        case Screen::STOPPED:
            // Аварийный стоп: мотор немедленно остановлен, держим позицию, ждём команды.
            g_state.switches = {false, false, false, false};
            if (m_stepper) m_stepper->stop();
            break;

        case Screen::DEBUG:
            // Ручной режим: мотор включён, ждём jog-команд. Концевик в сторону движения
            // останавливает мотор (tick), от концевика уехать можно.
            g_state.switches = {false, false, false, false};
            if (m_stepper) { m_stepper->stop(); m_stepper->enable(); }
            break;

        case Screen::PARKING: {
            g_state.switches = {false, false, false, false};
            // Если концевик TOP уже прижат — мы уже дома: не двигаемся, tick() сразу
            // уведёт в PARKED. Иначе едем к TOP (tick() остановит по SW_TOP).
            if (m_stepper) {
                if (sw.top) {
                    m_stepper->stop();
                } else {
                    // Если едет НЕ в ту сторону — поменяй "-" на "+" ниже.
                    startMove(m_stepper->currentPosition() - travelLimit(), g_state.parkSpeed, now);
                }
            }
            break;
        }

        default:
            break;
    }

    if (m_switches) m_switches->notifyScreenChange(next, now);
    requestBroadcast();
}

void StateMachine::tick(uint32_t nowMs) {
    SwitchSnapshot sw = m_switches ? m_switches->read() : SwitchSnapshot{false,false,false,false};

    // Живые показания концевиков — всегда транслируем в UI для отладки.
    // Бродкастим при любом изменении (троттлинг 10 Гц делает WsBroadcaster).
    if (sw.top != g_state.rawSwitches.top || sw.a != g_state.rawSwitches.a ||
        sw.b   != g_state.rawSwitches.b   || sw.bot != g_state.rawSwitches.bot) {
        g_state.rawSwitches = {sw.top, sw.a, sw.b, sw.bot};
        requestBroadcast();
    }

    if (m_stepper) g_state.motorPos = m_stepper->currentPosition();

    switch (g_state.screen) {
        case Screen::PARKING:
            if (sw.top) {
                if (m_stepper) { m_stepper->stop(); m_stepper->zero(); }  // дома → позиция 0
                transitionTo(Screen::PARKED);
                return;
            }
            if (moveStuck(nowMs)) {   // TOP не найден → сбой
                g_state.stopCause = StopCause::STUCK_HOME;
                transitionTo(Screen::STOPPED);
                return;
            }
            break;

        case Screen::CHARGING:
            g_state.switches.a = sw.a;
            g_state.switches.b = sw.b;
            if (sw.a && sw.b) { transitionTo(Screen::CHARGED); return; }
            if (sw.bot || moveStuck(nowMs)) {   // дошли до низа/до H, а A и B не прижались → сбой
                g_state.stopCause = StopCause::STUCK_CHARGE;
                transitionTo(Screen::STOPPED);
                return;
            }
            break;

        case Screen::DOSING: {
            float total = g_state.dosing.totalSec;
            // Секундомер — реальное время с момента ПУСК (для отображения).
            float elapsed = (nowMs >= m_dosingStartMs) ? (nowMs - m_dosingStartMs) / 1000.0f : 0.0f;
            g_state.dosing.elapsedSec = min(elapsed, total);

            // Прогресс — по фактически пройденному расстоянию (шагам) от заряда (2) к концу (3).
            int32_t pos = m_stepper ? m_stepper->currentPosition() : 0;
            int32_t traveled = pos - m_dosingStartPos;
            int32_t L2 = (m_dosingTargetSteps > 0) ? m_dosingTargetSteps : 1;
            float progress = constrain((float)traveled / (float)L2, 0.0f, 1.0f);

            bool byBot  = sw.bot;                                              // конец (3) раньше времени
            bool byDone = (traveled >= L2) || (m_stepper && !m_stepper->isBusy());
            if (byBot || byDone) {
                if (!byBot) progress = 1.0f;
                g_state.doneReason = byBot ? DoneReason::BOT : DoneReason::TIMER;
                g_state.dosing.progress = progress;
                g_state.dosing.volumeA  = g_state.planVolA * progress;
                g_state.dosing.volumeB  = g_state.planVolB * progress;
                transitionTo(Screen::DONE);
                return;
            }
            g_state.dosing.progress = progress;
            g_state.dosing.volumeA  = g_state.planVolA * progress;
            g_state.dosing.volumeB  = g_state.planVolB * progress;
            requestBroadcast();
            break;
        }

        case Screen::CALIBRATING:
            if (g_state.calibPhase == 0) {                 // фаза 0: хоуминг к дому (TOP)
                if (sw.top) {
                    if (m_stepper) {
                        m_stepper->stop();
                        m_stepper->zero();
                    }
                    g_state.calibPhase = 1;
                    m_calibStep = 0;
                    startMove(mmToSteps(MAX_TRAVEL_MM), g_state.parkSpeed, nowMs);   // новый сторож на фазу 1
                    requestBroadcast();
                } else if (moveStuck(nowMs)) {
                    g_state.stopCause = StopCause::STUCK_HOME;
                    transitionTo(Screen::STOPPED);
                    return;
                }
            } else if (m_calibStep == 0) {           // фаза 1: быстро к концу (BOT)
                if (sw.bot) {
                    if (m_stepper) m_stepper->stop();
                    // На скорости парковки каретка проезжает точку срабатывания (дребезг + цикл).
                    // Отъезжаем и подходим медленно — H не зависит от скорости.
                    m_calibStep = 1;
                    int32_t pos = m_stepper ? m_stepper->currentPosition() : 0;
                    startMove(pos - mmToSteps(CALIB_BACKOFF_MM), g_state.parkSpeed, nowMs);
                } else if (moveStuck(nowMs)) {
                    g_state.stopCause = StopCause::STUCK_END;
                    transitionTo(Screen::STOPPED);
                    return;
                }
            } else if (m_calibStep == 1) {           // отъезд от BOT
                if (m_stepper && m_stepper->isBusy()) break;
                if (sw.bot) {                        // отъехали, а BOT всё нажат — концевик залип
                    LOG_WARN("Calib: BOT still pressed after backoff");
                    g_state.stopCause = StopCause::CALIB_FAIL;
                    transitionTo(Screen::STOPPED);
                    return;
                }
                m_calibStep = 2;
                int32_t pos = m_stepper ? m_stepper->currentPosition() : 0;
                float slow = min(g_state.parkSpeed, (float)mmToSteps(CALIB_SLOW_MM_S));
                startMove(pos + 2 * mmToSteps(CALIB_BACKOFF_MM), slow, nowMs);
            } else {                                 // медленный подход к BOT
                if (sw.bot) {
                    int32_t pos = m_stepper ? m_stepper->currentPosition() : 0;
                    if (m_stepper) m_stepper->stop();
                    if (pos < MIN_CALIB_STEPS) {     // подозрительно малый ход → брак калибровки
                        g_state.stopCause = StopCause::CALIB_FAIL;
                        transitionTo(Screen::STOPPED);
                        return;
                    }
                    // H чуть короче точки BOT: доза заканчивается по ходу, концевик остаётся страховкой.
                    int32_t H = pos - mmToSteps(CALIB_END_MARGIN_MM);
                    g_state.fullPathSteps = H;
                    if (m_settings) m_settings->markDirty();
                    LOG_INFO("Calibrated: H=%d steps (BOT at %d)", (int)H, (int)pos);
                    transitionTo(Screen::IDLE);
                    return;
                } else if (moveStuck(nowMs)) {       // вернулись туда, где BOT срабатывал, а он молчит
                    g_state.stopCause = StopCause::CALIB_FAIL;
                    transitionTo(Screen::STOPPED);
                    return;
                }
            }
            break;

        case Screen::DEBUG:
            if (m_stepper && m_stepper->isBusy()) {
                int32_t dir = m_stepper->targetPosition() - m_stepper->currentPosition();
                // Концевик в сторону движения — стоп (упор каретки не давим).
                if ((dir < 0 && sw.top) || (dir > 0 && sw.bot)) {
                    m_stepper->stop();
                    m_jogHolding = false;
                    LOG_INFO("Jog stopped by %s", dir < 0 ? "TOP" : "BOT");
                } else if (m_jogHolding && (int32_t)(nowMs - m_jogHoldUntilMs) > 0) {
                    m_stepper->stop();              // удержание не подтверждено — deadman
                    m_jogHolding = false;
                    LOG_WARN("Jog hold deadman stop");
                }
                requestBroadcast();                 // пока едем — шлём живую позицию
            }
            break;

        default:
            break;
    }

    // Underflow guard: lastInteractionMs can be set from millis() slightly after
    // the nowMs captured at loop() start. Without the guard, (nowMs - last) wraps
    // to ~4B and triggers immediate sleep.
    if (sleepAllowed(g_state.screen) && !g_state.displaySleeping) {
        if (nowMs >= g_state.lastInteractionMs &&
            (nowMs - g_state.lastInteractionMs) >= (uint32_t)g_state.sleepTimeoutSec * 1000) {
            g_state.displaySleeping = true;
            requestBroadcast();
        }
    }
}
