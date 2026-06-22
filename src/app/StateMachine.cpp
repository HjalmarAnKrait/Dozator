#include "StateMachine.h"
#include "Calculations.h"
#include "../storage/Settings.h"
#include "../util/Logger.h"
#include "config.h"
#include <math.h>

StateMachine g_sm;

// ─── helpers ──────────────────────────────────────────────────────────────

int StateMachine::navWrap(int cur, int dir, int total) {
    int next = cur + dir;
    if (g_state.circularNav) {
        return (next % total + total) % total;
    }
    return constrain(next, 0, total - 1);
}

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

void StateMachine::pushBreadcrumb() {
    if (g_state.breadcrumbDepth < 6)
        g_state.breadcrumb[g_state.breadcrumbDepth++] = {g_state.screen, g_state.cursorIndex};
}

void StateMachine::popBreadcrumb() {
    if (g_state.breadcrumbDepth == 0) return;
    BreadcrumbEntry entry = g_state.breadcrumb[--g_state.breadcrumbDepth];
    transitionTo(entry.screen);
    g_state.cursorIndex = entry.cursor;
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
        case Screen::PARKED:
            g_state.switches = {true, false, false, false};
            g_state.cursorIndex = 0;
            if (m_stepper) m_stepper->disable();
            break;

        case Screen::CHARGING:
            g_state.switches = {false, false, false, false};
            g_state.cursorIndex = 0;
            if (m_stepper) m_stepper->enable();
            break;

        case Screen::CHARGED:
            g_state.switches.a = true;
            g_state.switches.b = true;
            g_state.cursorIndex = 0;
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
            g_state.cursorIndex = 0;
            g_state.breadcrumbDepth = 0;
            if (m_stepper) { m_stepper->enable(); m_stepper->moveTo(0, 800.0f); }
            break;

        case Screen::SERVICE_MENU:
            g_state.cursorIndex = 0;
            break;

        case Screen::SERVICE_PITCH:
        case Screen::SERVICE_SLEEP:
            g_state.cursorIndex = 0;
            g_state.editing = true;
            break;

        case Screen::SERVICE_FONT:
            // Start cursor on currently active font
            g_state.cursorIndex = g_state.fontIndex;
            break;

        case Screen::SERVICE_SYRINGES:
        case Screen::SERVICE_SYRINGE_EDIT:
            g_state.cursorIndex = 0;
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

    // Underflow guard: lastInteractionMs can be set from millis() inside onEvent(),
    // which fires slightly after the nowMs captured at loop() start. Without the guard,
    // (nowMs - lastInteractionMs) wraps to ~4B and triggers immediate sleep.
    if (sleepAllowed(g_state.screen) && !g_state.displaySleeping) {
        if (nowMs >= g_state.lastInteractionMs &&
            (nowMs - g_state.lastInteractionMs) >= (uint32_t)g_state.sleepTimeoutSec * 1000) {
            g_state.displaySleeping = true;
            requestBroadcast();
        }
    }
}

void StateMachine::onEvent(EncoderEvent ev) {
    g_state.lastInteractionMs = millis();
    if (g_state.displaySleeping) {
        g_state.displaySleeping = false;
        requestBroadcast();
        return;
    }
    switch (g_state.screen) {
        case Screen::PARKING:              handleParking(ev);          break;
        case Screen::PARKED:               handleParked(ev);           break;
        case Screen::CHARGING:             handleCharging(ev);         break;
        case Screen::CHARGED:              handleCharged(ev);          break;
        case Screen::DOSING:               handleDosing(ev);           break;
        case Screen::DONE:                 handleDone(ev);             break;
        case Screen::SERVICE_MENU:         handleServiceMenu(ev);      break;
        case Screen::SERVICE_PITCH:        handleServicePitch(ev);     break;
        case Screen::SERVICE_SLEEP:        handleServiceSleep(ev);     break;
        case Screen::SERVICE_SYRINGES:     handleServiceSyringes(ev);  break;
        case Screen::SERVICE_SYRINGE_EDIT: handleServiceSyringeEdit(ev); break;
        case Screen::SERVICE_FONT:         handleServiceFont(ev);      break;
    }
}

void StateMachine::handleParking(EncoderEvent) {}

void StateMachine::adjustParkedEdit(int dir) {
    switch (g_state.cursorIndex) {
        case 0: {
            int idx = ((int)g_state.syringeA.presetIdx + dir + g_state.presetsCount) % g_state.presetsCount;
            g_state.syringeA.presetIdx = idx;
            g_state.syringeA.diameter  = g_state.presets[idx].diam;
            break;
        }
        case 1:
            g_state.syringeA.diameter = roundf(constrain(g_state.syringeA.diameter + dir * 0.1f, 1.0f, 150.0f) * 10.0f) / 10.0f;
            break;
        case 2: {
            int idx = ((int)g_state.syringeB.presetIdx + dir + g_state.presetsCount) % g_state.presetsCount;
            g_state.syringeB.presetIdx = idx;
            g_state.syringeB.diameter  = g_state.presets[idx].diam;
            break;
        }
        case 3:
            g_state.syringeB.diameter = roundf(constrain(g_state.syringeB.diameter + dir * 0.1f, 1.0f, 150.0f) * 10.0f) / 10.0f;
            break;
    }
}

void StateMachine::handleParked(EncoderEvent ev) {
    constexpr int TOTAL = 6;  // items 0-5
    if (g_state.editing) {
        switch (ev) {
            case EncoderEvent::CW:    adjustParkedEdit(+1); requestBroadcast(); break;
            case EncoderEvent::CCW:   adjustParkedEdit(-1); requestBroadcast(); break;
            case EncoderEvent::CLICK:
            case EncoderEvent::LONG:
                g_state.editing = false; requestBroadcast(); break;
            default: break;
        }
        return;
    }
    switch (ev) {
        case EncoderEvent::CW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, +1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, -1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CLICK:
            switch (g_state.cursorIndex) {
                case 0: case 1: case 2: case 3:
                    g_state.editing = true; requestBroadcast(); break;
                case 4:
                    transitionTo(Screen::CHARGING); break;
                case 5:
                    pushBreadcrumb(); transitionTo(Screen::SERVICE_MENU); break;
            }
            break;
        default: break;
    }
}

void StateMachine::handleCharging(EncoderEvent ev) {
    if (ev == EncoderEvent::LONG) transitionTo(Screen::PARKING);
}

void StateMachine::adjustChargedEdit(int dir) {
    g_state.doseTimeMin = roundf(constrain(g_state.doseTimeMin + dir * 0.1f, 0.1f, 999.0f) * 10.0f) / 10.0f;
}

void StateMachine::handleCharged(EncoderEvent ev) {
    constexpr int TOTAL = 2;  // 0=time, 1=ПУСК
    if (g_state.editing) {
        switch (ev) {
            case EncoderEvent::CW:    adjustChargedEdit(+1); requestBroadcast(); break;
            case EncoderEvent::CCW:   adjustChargedEdit(-1); requestBroadcast(); break;
            case EncoderEvent::CLICK:
                g_state.editing = false; requestBroadcast(); break;
            case EncoderEvent::LONG:
                // Long press from edit mode: exit edit, then fall through to full return
                g_state.editing = false;
                transitionTo(Screen::PARKING); break;
            default: break;
        }
        return;
    }
    switch (ev) {
        case EncoderEvent::CW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, +1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, -1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CLICK:
            if (g_state.cursorIndex == 0) { g_state.editing = true; requestBroadcast(); }
            else transitionTo(Screen::DOSING);
            break;
        case EncoderEvent::LONG:
            // Return to parking (home position 0) and restart the cycle
            transitionTo(Screen::PARKING); break;
        default: break;
    }
}

void StateMachine::handleDosing(EncoderEvent ev) {
    if (ev == EncoderEvent::LONG) { g_state.dosing.progress = 1.0f; transitionTo(Screen::DONE); }
}

void StateMachine::handleDone(EncoderEvent ev) {
    if (ev == EncoderEvent::CLICK || ev == EncoderEvent::LONG) transitionTo(Screen::PARKING);
}

void StateMachine::handleServiceMenu(EncoderEvent ev) {
    // 0=Шаг, 1=Шприцы, 2=Сон, 3=Шрифт, 4=Кольцо меню (toggle), 5=Назад
    constexpr int TOTAL = 6;
    switch (ev) {
        case EncoderEvent::CW:
            // Service menu always navigates circularly so the user can always reach Назад
            g_state.cursorIndex = (g_state.cursorIndex + 1) % TOTAL;
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = (g_state.cursorIndex + TOTAL - 1) % TOTAL;
            requestBroadcast(); break;
        case EncoderEvent::CLICK:
            switch (g_state.cursorIndex) {
                case 0: pushBreadcrumb(); transitionTo(Screen::SERVICE_PITCH);    break;
                case 1: pushBreadcrumb(); transitionTo(Screen::SERVICE_SYRINGES); break;
                case 2: pushBreadcrumb(); transitionTo(Screen::SERVICE_SLEEP);    break;
                case 3: pushBreadcrumb(); transitionTo(Screen::SERVICE_FONT);     break;
                case 4:
                    g_state.circularNav = !g_state.circularNav;
                    m_settings->markDirty();
                    requestBroadcast(); break;
                case 5: popBreadcrumb(); break;
            }
            break;
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}

void StateMachine::handleServicePitch(EncoderEvent ev) {
    switch (ev) {
        case EncoderEvent::CW:
            g_state.screwPitch = min(10.0f, g_state.screwPitch + 0.5f);
            m_settings->markDirty(); requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.screwPitch = max(0.5f, g_state.screwPitch - 0.5f);
            m_settings->markDirty(); requestBroadcast(); break;
        case EncoderEvent::CLICK:
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}

void StateMachine::handleServiceSleep(EncoderEvent ev) {
    switch (ev) {
        case EncoderEvent::CW:
            if (g_state.sleepTimeoutSec < 300) g_state.sleepTimeoutSec += 5;
            m_settings->markDirty(); requestBroadcast(); break;
        case EncoderEvent::CCW:
            if (g_state.sleepTimeoutSec > 5) g_state.sleepTimeoutSec -= 5;
            m_settings->markDirty(); requestBroadcast(); break;
        case EncoderEvent::CLICK:
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}

void StateMachine::handleServiceFont(EncoderEvent ev) {
    constexpr int TOTAL = 5;  // 4 fonts + Назад
    switch (ev) {
        case EncoderEvent::CW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, +1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, -1, TOTAL);
            requestBroadcast(); break;
        case EncoderEvent::CLICK:
            if (g_state.cursorIndex < 4) {
                g_state.fontIndex = g_state.cursorIndex;
                m_settings->markDirty();
                requestBroadcast();
            } else {
                popBreadcrumb();
            }
            break;
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}

void StateMachine::handleServiceSyringes(EncoderEvent ev) {
    int total = g_state.presetsCount + 2;  // presets + Новый + Назад
    switch (ev) {
        case EncoderEvent::CW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, +1, total);
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, -1, total);
            requestBroadcast(); break;
        case EncoderEvent::CLICK:
            if (g_state.cursorIndex < g_state.presetsCount) {
                g_state.editingPresetIdx = g_state.cursorIndex;
                pushBreadcrumb(); transitionTo(Screen::SERVICE_SYRINGE_EDIT);
            } else if (g_state.cursorIndex == g_state.presetsCount && g_state.presetsCount < MAX_PRESETS) {
                g_state.presets[g_state.presetsCount] = {10.0f, 14.5f};
                g_state.editingPresetIdx = g_state.presetsCount++;
                m_settings->markDirty();
                pushBreadcrumb(); transitionTo(Screen::SERVICE_SYRINGE_EDIT);
            } else {
                popBreadcrumb();
            }
            break;
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}

void StateMachine::adjustSyringeEditField(int dir) {
    int idx = g_state.editingPresetIdx;
    if (idx < 0 || idx >= g_state.presetsCount) return;
    if (g_state.cursorIndex == 0) {
        g_state.presets[idx].vol = constrain(g_state.presets[idx].vol + dir, 1.0f, 500.0f);
    } else if (g_state.cursorIndex == 1) {
        g_state.presets[idx].diam = roundf(constrain(g_state.presets[idx].diam + dir * 0.1f, 1.0f, 150.0f) * 10.0f) / 10.0f;
    }
    m_settings->markDirty();
}

void StateMachine::handleServiceSyringeEdit(EncoderEvent ev) {
    bool hasDelete = (g_state.presetsCount > 1);
    int  backIdx   = hasDelete ? 3 : 2;
    int  total     = backIdx + 1;

    if (g_state.editing) {
        switch (ev) {
            case EncoderEvent::CW:    adjustSyringeEditField(+1); requestBroadcast(); break;
            case EncoderEvent::CCW:   adjustSyringeEditField(-1); requestBroadcast(); break;
            case EncoderEvent::CLICK:
            case EncoderEvent::LONG:
                g_state.editing = false; requestBroadcast(); break;
            default: break;
        }
        return;
    }
    switch (ev) {
        case EncoderEvent::CW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, +1, total);
            requestBroadcast(); break;
        case EncoderEvent::CCW:
            g_state.cursorIndex = navWrap(g_state.cursorIndex, -1, total);
            requestBroadcast(); break;
        case EncoderEvent::CLICK: {
            int delIdx = hasDelete ? 2 : -1;
            if (g_state.cursorIndex == 0 || g_state.cursorIndex == 1) {
                g_state.editing = true; requestBroadcast();
            } else if (g_state.cursorIndex == delIdx) {
                int di = g_state.editingPresetIdx;
                for (int i = di; i < g_state.presetsCount - 1; i++)
                    g_state.presets[i] = g_state.presets[i + 1];
                g_state.presetsCount--;
                if (g_state.syringeA.presetIdx >= g_state.presetsCount) g_state.syringeA.presetIdx = g_state.presetsCount - 1;
                if (g_state.syringeB.presetIdx >= g_state.presetsCount) g_state.syringeB.presetIdx = g_state.presetsCount - 1;
                g_state.syringeA.diameter = g_state.presets[g_state.syringeA.presetIdx].diam;
                g_state.syringeB.diameter = g_state.presets[g_state.syringeB.presetIdx].diam;
                m_settings->markDirty();
                popBreadcrumb();
            } else if (g_state.cursorIndex == backIdx) {
                popBreadcrumb();
            }
            break;
        }
        case EncoderEvent::LONG: popBreadcrumb(); break;
        default: break;
    }
}
