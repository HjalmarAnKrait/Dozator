#pragma once
#include <Arduino.h>
#include "AppState.h"
#include "../input/EncoderInput.h"
#include "../hardware/IStepperDriver.h"
#include "../hardware/ILimitSwitches.h"

class Settings;

/**
 * Central state machine. Owns all transition logic and side effects.
 * Reads/writes g_state directly. Does NOT render or broadcast – callers do.
 */
class StateMachine {
public:
    void begin(IStepperDriver* stepper, ILimitSwitches* switches, Settings* settings);
    void transitionTo(Screen next);
    void onEvent(EncoderEvent ev);
    void tick(uint32_t nowMs);

    bool needsBroadcast() {
        bool b = m_broadcastPending;
        m_broadcastPending = false;
        return b;
    }

private:
    IStepperDriver*  m_stepper  = nullptr;
    ILimitSwitches*  m_switches = nullptr;
    Settings*        m_settings = nullptr;

    bool     m_broadcastPending = false;
    uint32_t m_dosingStartMs    = 0;

    // ── Per-screen event handlers ──────────────────────────────────────────
    void handleParking(EncoderEvent ev);
    void handleParked(EncoderEvent ev);
    void handleCharging(EncoderEvent ev);
    void handleCharged(EncoderEvent ev);
    void handleDosing(EncoderEvent ev);
    void handleDone(EncoderEvent ev);
    void handleServiceMenu(EncoderEvent ev);
    void handleServicePitch(EncoderEvent ev);
    void handleServiceSleep(EncoderEvent ev);
    void handleServiceSyringes(EncoderEvent ev);
    void handleServiceSyringeEdit(EncoderEvent ev);
    void handleServiceFont(EncoderEvent ev);

    // ── Helpers ────────────────────────────────────────────────────────────
    void pushBreadcrumb();
    void popBreadcrumb();
    void adjustParkedEdit(int dir);
    void adjustChargedEdit(int dir);
    void adjustSyringeEditField(int dir);

    static bool sleepAllowed(Screen s);
    // Advance cursor by dir (+1/-1) within [0, total-1].
    // Wraps around if circularNav is enabled, clamps otherwise.
    static int navWrap(int cur, int dir, int total);
    void requestBroadcast() { m_broadcastPending = true; }
};

extern StateMachine g_sm;
