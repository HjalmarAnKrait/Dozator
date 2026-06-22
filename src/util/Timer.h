#pragma once
#include <Arduino.h>

// Non-blocking millis()-based one-shot timer.
class Timer {
public:
    void start(uint32_t durationMs) {
        m_start = millis();
        m_duration = durationMs;
        m_running = true;
    }

    void stop() { m_running = false; }
    bool isRunning() const { return m_running; }

    bool expired(uint32_t nowMs) const {
        return m_running && (nowMs - m_start >= m_duration);
    }

    uint32_t elapsed(uint32_t nowMs) const {
        return m_running ? (nowMs - m_start) : 0;
    }

private:
    uint32_t m_start    = 0;
    uint32_t m_duration = 0;
    bool     m_running  = false;
};
