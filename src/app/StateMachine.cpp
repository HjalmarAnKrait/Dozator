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
            if (m_stepper) m_stepper->enable();
            break;

        case Screen::CHARGED:
            g_state.switches.a = true;
            g_state.switches.b = true;
            break;

        case Screen::DOSING: {
            g_state.dosing = {0, g_state.doseTimeMin * 60.0f, 0, 0, 0};
            m_dosingStartMs = millis();
            if (m_stepper) {
                float rpm   = calc::motorRpm();
                float speed = max(1.0f, rpm * FULL_STEPS_PER_REV / 60.0f);
                int32_t tgt = (int32_t)(g_state.dosing.totalSec * speed);
                m_stepper->enable();
                m_stepper->moveTo(tgt, speed);
            }
            break;
        }

        case Screen::DONE:
            g_state.switches.bot = true;
            if (m_stepper) m_stepper->stop();
            break;

        case Screen::PARKING:
            g_state.switches = {false, false, false, false};
            if (m_stepper) { m_stepper->enable(); m_stepper->moveTo(0, 800.0f); }
            break;

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
            if (sw.top) { transitionTo(Screen::PARKED); return; }
            break;

        case Screen::CHARGING:
            g_state.switches.a = sw.a;
            g_state.switches.b = sw.b;
            if (sw.a && sw.b) { transitionTo(Screen::CHARGED); return; }
            break;

        case Screen::DOSING: {
            float simElapsed = ((nowMs - m_dosingStartMs) / 1000.0f) * SIM_TIME_FACTOR;
            simElapsed = min(simElapsed, g_state.dosing.totalSec);
            g_state.dosing.elapsedSec = simElapsed;
            if (g_state.dosing.totalSec > 0)
                g_state.dosing.progress = simElapsed / g_state.dosing.totalSec;
            g_state.dosing.volumeA = calc::volA() * g_state.dosing.progress;
            g_state.dosing.volumeB = calc::volB() * g_state.dosing.progress;
            if (sw.bot || simElapsed >= g_state.dosing.totalSec) {
                g_state.dosing.progress = 1.0f;
                transitionTo(Screen::DONE);
                return;
            }
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
