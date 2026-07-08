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

bool StateMachine::moveStuck(uint32_t nowMs) {
    // Сбой хода к концевику: слишком долго ИЛИ мотор доехал до программного предела.
    if (nowMs - m_moveStartMs > STUCK_TIMEOUT_MS) return true;
    if (m_stepper && !m_stepper->isBusy())        return true;
    return false;
}

// Двухфазная кнопка (физическая D3 и глоб. кнопка UI): СТОП → (после стопа) Парковка.
void StateMachine::stopButtonPress() {
    if (g_state.screen == Screen::STOPPED) {
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

    g_state.screen           = next;
    g_state.editing          = false;
    g_state.displaySleeping  = false;
    g_state.lastInteractionMs = millis();
    m_moveStartMs            = millis();   // старт отсчёта для сторожа застревания

    switch (next) {
        case Screen::IDLE:
            // Простой: ничего не двигаем (никаких шагов при старте).
            g_state.switches = {false, false, false, false};
            if (m_stepper) m_stepper->disable();
            break;

        case Screen::PARKED:
            g_state.switches = {true, false, false, false};
            if (m_stepper) m_stepper->disable();
            break;

        case Screen::CHARGING:
            g_state.switches = {false, false, false, false};
            if (m_stepper) {
                m_stepper->enable();
                // Плавный спуск к концевикам A/B; tick() остановит по прижатию обоих.
                // Направление вниз = знак, противоположный хоумингу (там "-").
                m_stepper->moveTo(m_stepper->currentPosition() + 100000, g_state.chargeSpeed);
            }
            break;

        case Screen::CHARGED:
            g_state.switches.a = true;
            g_state.switches.b = true;
            if (m_stepper) m_stepper->stop();   // оба концевика прижаты — стоп
            break;

        case Screen::DOSING: {
            g_state.dosing = {0, g_state.doseTimeMin * 60.0f, 0, 0, 0};
            m_dosingStartMs = millis();
            // Дистанционная модель: доза = остаток L2 = H − L (заряд→конец) за время t.
            int32_t L  = m_stepper ? m_stepper->currentPosition() : 0;  // позиция заряда (2)
            int32_t H  = g_state.fullPathSteps;                         // полный ход (калибровка)
            int32_t L2 = H - L;
            m_dosingStartPos    = L;
            m_dosingTargetSteps = L2;
            // Плановый объём дозы по расстоянию L2 (только для отображения).
            float l2mm = L2 * (g_state.screwPitch / (float)FULL_STEPS_PER_REV);
            g_state.planVolA = calc::distToVolumeMl(g_state.syringeA.diameter, l2mm);
            g_state.planVolB = calc::distToVolumeMl(g_state.syringeB.diameter, l2mm);
            if (m_stepper && L2 > 0) {
                float t = max(1.0f, g_state.dosing.totalSec);
                float speed = constrain((float)L2 / t, 1.0f, 15000.0f);   // V = L2 / t
                m_stepper->enable();
                m_stepper->moveTo(H, speed);                              // едем в позицию 3
            }
            break;
        }

        case Screen::CALIBRATING:
            g_state.switches = {false, false, false, false};
            m_calibPhase = 0;
            if (m_stepper) {
                SwitchSnapshot csw = m_switches ? m_switches->read()
                                                : SwitchSnapshot{false, false, false, false};
                m_stepper->enable();
                if (csw.top) {                 // уже дома → сразу спуск к концу (фаза 1)
                    m_stepper->zero();
                    m_calibPhase = 1;
                    m_stepper->moveTo(m_stepper->currentPosition() + 5000000, g_state.parkSpeed);
                } else {                       // сначала к дому (TOP)
                    m_stepper->moveTo(m_stepper->currentPosition() - 5000000, g_state.parkSpeed);
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

        case Screen::PARKING: {
            g_state.switches = {false, false, false, false};
            // Если концевик TOP уже прижат — мы уже дома: не двигаемся, tick() сразу
            // уведёт в PARKED. Иначе едем к TOP большим ходом (tick() остановит по SW_TOP).
            SwitchSnapshot sw = m_switches ? m_switches->read()
                                           : SwitchSnapshot{false, false, false, false};
            if (m_stepper) {
                if (sw.top) {
                    m_stepper->stop();
                } else {
                    m_stepper->enable();
                    // Если едет НЕ в ту сторону — поменяй "-" на "+" ниже.
                    m_stepper->moveTo(m_stepper->currentPosition() - 100000, g_state.parkSpeed);
                }
            }
            break;
        }

        default:
            break;
    }

    if (m_switches) m_switches->notifyScreenChange(next, millis());
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
            if (moveStuck(nowMs)) {   // оба концевика A/B не прижались → сбой
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
            if (m_calibPhase == 0) {                 // фаза 0: хоуминг к дому (TOP)
                if (sw.top) {
                    if (m_stepper) {
                        m_stepper->stop();
                        m_stepper->zero();
                        m_stepper->enable();
                        m_stepper->moveTo(m_stepper->currentPosition() + 5000000, g_state.parkSpeed);
                    }
                    m_calibPhase = 1;
                    m_moveStartMs = nowMs;           // сброс таймера сторожа на фазу 1
                    requestBroadcast();
                } else if (moveStuck(nowMs)) {
                    g_state.stopCause = StopCause::STUCK_HOME;
                    transitionTo(Screen::STOPPED);
                    return;
                }
            } else {                                 // фаза 1: спуск к концу (BOT)
                if (sw.bot) {
                    int32_t H = m_stepper ? m_stepper->currentPosition() : 0;
                    if (m_stepper) m_stepper->stop();
                    if (H < MIN_CALIB_STEPS) {       // подозрительно малый ход → брак калибровки
                        g_state.stopCause = StopCause::CALIB_FAIL;
                        transitionTo(Screen::STOPPED);
                        return;
                    }
                    g_state.fullPathSteps = H;
                    if (m_settings) m_settings->markDirty();
                    LOG_INFO("Calibrated: H=%d steps", (int)H);
                    transitionTo(Screen::IDLE);
                    return;
                } else if (moveStuck(nowMs)) {
                    g_state.stopCause = StopCause::STUCK_END;
                    transitionTo(Screen::STOPPED);
                    return;
                }
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
