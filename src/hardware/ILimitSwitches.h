#pragma once
#include <Arduino.h>

// Forward-declared; full definition in app/AppState.h. Keeps the HW interface
// free of an app-layer include while allowing the sim hook below.
enum class Screen : uint8_t;

struct SwitchSnapshot {
    bool top;
    bool a;
    bool b;
    bool bot;
};

/** Hardware abstraction for limit / position switches. */
class ILimitSwitches {
public:
    virtual ~ILimitSwitches() = default;

    virtual SwitchSnapshot read() = 0;
    virtual void tick(uint32_t nowMs) = 0;

    // Only used by SimulatedSwitches and web debug panel; real impl ignores.
    virtual void debugSetSwitch(uint8_t idx, bool state) { (void)idx; (void)state; }

    // Sim-only hook: lets SimulatedSwitches reset its timers on screen change.
    // Real hardware ignores it. Keeps StateMachine decoupled from the concrete impl.
    virtual void notifyScreenChange(Screen newScreen, uint32_t nowMs) { (void)newScreen; (void)nowMs; }
};
