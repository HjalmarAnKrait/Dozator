#pragma once
#include <Arduino.h>
#include "config.h"

class Debounce {
public:
    explicit Debounce(uint32_t delayMs = SWITCH_DEBOUNCE_MS)
        : m_delay(delayMs) {}

    // Feed raw pin state; returns true when a stable transition is detected.
    bool update(bool raw, uint32_t nowMs) {
        if (raw != m_pending) {
            m_pending  = raw;
            m_changeMs = nowMs;
        }
        if ((nowMs - m_changeMs) >= m_delay && m_stable != m_pending) {
            m_stable = m_pending;
            return true;
        }
        return false;
    }

    bool stable() const { return m_stable; }

private:
    bool     m_stable    = false;
    bool     m_pending   = false;
    uint32_t m_changeMs  = 0;
    uint32_t m_delay;
};
