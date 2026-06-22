#include "SimulatedStepper.h"

void SimulatedStepper::moveTo(int32_t targetSteps, float speedStepsPerSec) {
    m_target = targetSteps;
    m_speed  = max(speedStepsPerSec, 1.0f);
    m_lastMs = millis();
    m_accumFrac = 0.0f;
}

void SimulatedStepper::stop() {
    m_target = m_pos;
}

void SimulatedStepper::tick(uint32_t nowMs) {
    if (m_pos == m_target) return;

    uint32_t dt = nowMs - m_lastMs;
    m_lastMs = nowMs;

    float stepsF = m_speed * (dt / 1000.0f) + m_accumFrac;
    int32_t steps = (int32_t)stepsF;
    m_accumFrac = stepsF - steps;

    if (steps == 0) return;

    if (m_target > m_pos) {
        m_pos = min((int32_t)(m_pos + steps), m_target);
    } else {
        m_pos = max((int32_t)(m_pos - steps), m_target);
    }
}
