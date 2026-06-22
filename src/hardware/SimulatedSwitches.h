#pragma once
#include "ILimitSwitches.h"
#include "../app/AppState.h"

/**
 * Simulates limit switches based on the current screen state.
 *
 * PARKING  → top  fires after 1200 ms
 * CHARGING → a    fires after  900 ms, b fires 800 ms later
 * DOSING   → bot  fires after totalSec / SIM_TIME_FACTOR real seconds
 */
class SimulatedSwitches : public ILimitSwitches {
public:
    SwitchSnapshot read() override;
    void tick(uint32_t nowMs) override;
    void debugSetSwitch(uint8_t idx, bool state) override;

    // Called by StateMachine when entering a new screen so timers reset
    void notifyScreenChange(Screen newScreen, uint32_t nowMs) override;

private:
    Screen   m_lastScreen  = Screen::PARKING;
    uint32_t m_screenEnterMs = 0;

    bool m_aFired = false;
    bool m_bFired = false;

    // Manual overrides from web debug panel
    bool m_debugTop = false;
    bool m_debugA   = false;
    bool m_debugB   = false;
    bool m_debugBot = false;

    // Computed switch states
    bool m_top = false;
    bool m_a   = false;
    bool m_b   = false;
    bool m_bot = false;
};
