#pragma once
#include <Arduino.h>
#include "config.h"

/**
 * Антидребезг с раздельными задержками на нажатие и отпускание.
 *
 * Для концевиков нажатие подтверждается быстро (SWITCH_PRESS_MS), чтобы каретка
 * не проезжала за точку срабатывания, а отпускание — медленно
 * (SWITCH_DEBOUNCE_MS), чтобы дребезг контакта не давал ложных отпусканий.
 */
class Debounce {
public:
    explicit Debounce(uint32_t delayMs = SWITCH_DEBOUNCE_MS)
        : m_pressDelay(delayMs), m_releaseDelay(delayMs) {}
    Debounce(uint32_t pressMs, uint32_t releaseMs)
        : m_pressDelay(pressMs), m_releaseDelay(releaseMs) {}

    // Feed raw pin state; returns true when a stable transition is detected.
    bool update(bool raw, uint32_t nowMs) {
        if (raw != m_pending) {
            m_pending  = raw;
            m_changeMs = nowMs;
        }
        uint32_t need = m_pending ? m_pressDelay : m_releaseDelay;
        if ((nowMs - m_changeMs) >= need && m_stable != m_pending) {
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
    uint32_t m_pressDelay;
    uint32_t m_releaseDelay;
};
