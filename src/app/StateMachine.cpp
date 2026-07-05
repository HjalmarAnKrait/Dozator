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
            return false;
        default:
            return true;
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
            if (m_stepper) {
                float rpm   = calc::motorRpm();
                float speed = max(1.0f, rpm * FULL_STEPS_PER_REV / 60.0f);
                // Полное число микрошагов, чтобы выдавить весь объём за totalSec.
                m_dosingTargetSteps = (int32_t)(g_state.dosing.totalSec * speed);
                if (m_dosingTargetSteps < 1) m_dosingTargetSteps = 1;
                m_stepper->zero();                              // отсчёт дозы от 0
                m_stepper->enable();
                m_stepper->moveTo(m_dosingTargetSteps, speed);  // +N шагов от текущей позиции
            }
            break;
        }

        case Screen::DONE:
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
            break;

        case Screen::CHARGING:
            g_state.switches.a = sw.a;
            g_state.switches.b = sw.b;
            if (sw.a && sw.b) { transitionTo(Screen::CHARGED); return; }
            break;

        case Screen::DOSING: {
            float total = g_state.dosing.totalSec;
            // Секундомер — реальное время с момента ПУСК (для отображения).
            float elapsed = (nowMs >= m_dosingStartMs) ? (nowMs - m_dosingStartMs) / 1000.0f : 0.0f;
            g_state.dosing.elapsedSec = min(elapsed, total);

            // Прогресс и выдавленный объём — по ФАКТИЧЕСКИМ шагам мотора (не по времени).
            int32_t pos = m_stepper ? m_stepper->currentPosition() : 0;
            int32_t tgt = (m_dosingTargetSteps > 0) ? m_dosingTargetSteps : 1;
            float progress = constrain((float)pos / (float)tgt, 0.0f, 1.0f);

            bool byBot  = sw.bot;                                      // концевик раньше времени
            bool byDone = (pos >= tgt) || (m_stepper && !m_stepper->isBusy());  // мотор отработал все шаги
            if (byBot || byDone) {
                if (!byBot) progress = 1.0f;
                g_state.doneReason = byBot ? DoneReason::BOT : DoneReason::TIMER;
                g_state.dosing.progress = progress;
                g_state.dosing.volumeA  = calc::volA() * progress;
                g_state.dosing.volumeB  = calc::volB() * progress;
                transitionTo(Screen::DONE);
                return;
            }
            g_state.dosing.progress = progress;
            g_state.dosing.volumeA  = calc::volA() * progress;
            g_state.dosing.volumeB  = calc::volB() * progress;
            requestBroadcast();
            break;
        }

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
