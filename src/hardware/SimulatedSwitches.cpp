#include "SimulatedSwitches.h"
#include "config.h"

void SimulatedSwitches::notifyScreenChange(Screen newScreen, uint32_t nowMs) {
    m_lastScreen    = newScreen;
    m_screenEnterMs = nowMs;
    m_aFired = false;
    m_bFired = false;
    m_top = false;
    m_a   = false;
    m_b   = false;
    m_bot = false;
}

void SimulatedSwitches::tick(uint32_t nowMs) {
    // Underflow guard: m_screenEnterMs is set from millis() inside transitionTo(),
    // which is called from onEvent(). The nowMs here is captured earlier in loop().
    // If nowMs < m_screenEnterMs, elapsed would wrap to ~4B, firing switches instantly.
    if (nowMs < m_screenEnterMs) return;
    uint32_t elapsed = nowMs - m_screenEnterMs;

    switch (m_lastScreen) {
        case Screen::PARKING:
            if (!m_top && elapsed >= 1200) {
                m_top = true;
            }
            break;

        case Screen::CHARGING:
            if (!m_aFired && elapsed >= 900) {
                m_a      = true;
                m_aFired = true;
            }
            if (m_aFired && !m_bFired && elapsed >= (900 + 800)) {
                m_b      = true;
                m_bFired = true;
            }
            break;

        case Screen::DOSING: {
            float totalSec  = g_state.dosing.totalSec;
            float simMs     = (totalSec * 1000.0f) / SIM_TIME_FACTOR;
            if (!m_bot && elapsed >= (uint32_t)simMs) {
                m_bot = true;
            }
            break;
        }

        default:
            break;
    }
}

SwitchSnapshot SimulatedSwitches::read() {
    return {
        m_top || m_debugTop,
        m_a   || m_debugA,
        m_b   || m_debugB,
        m_bot || m_debugBot
    };
}

void SimulatedSwitches::debugSetSwitch(uint8_t idx, bool state) {
    switch (idx) {
        case 0: m_debugTop = state; break;
        case 1: m_debugA   = state; break;
        case 2: m_debugB   = state; break;
        case 3: m_debugBot = state; break;
    }
}
