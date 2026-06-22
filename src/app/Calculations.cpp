#include "Calculations.h"
#include "config.h"
#include <math.h>

namespace calc {

static constexpr float PI_F = 3.14159265f;

// Cross-section area (mm²) from diameter (mm)
static float area(float diamMm) {
    float r = diamMm * 0.5f;
    return PI_F * r * r;
}

float volA() {
    int idx = g_state.syringeA.presetIdx;
    if (idx < 0 || idx >= g_state.presetsCount) return 0.0f;
    return g_state.presets[idx].vol;
}

float volB() {
    int idx = g_state.syringeB.presetIdx;
    if (idx < 0 || idx >= g_state.presetsCount) return 0.0f;
    return g_state.presets[idx].vol;
}

float flowA() {
    if (g_state.doseTimeMin <= 0.0f) return 0.0f;
    return volA() / g_state.doseTimeMin;
}

float screwSpeedMmPerMin() {
    float sA = area(g_state.syringeA.diameter);
    if (sA <= 0.0f) return 0.0f;
    return flowA() / sA;   // mm³/min / mm² = mm/min
}

float flowB() {
    return screwSpeedMmPerMin() * area(g_state.syringeB.diameter);
}

float motorRpm() {
    if (g_state.screwPitch <= 0.0f) return 0.0f;
    return screwSpeedMmPerMin() / g_state.screwPitch;
}

TimeRange timeRangeMin() {
    float vA = volA();
    if (vA <= 0.0f) return {0.0f, 0.0f};

    // Flow-limited range
    float tMin = vA / MAX_FLOW_MLPM;
    float tMax = vA / MIN_FLOW_MLPM;

    // RPM-limited range: clamp based on motor RPM [MOTOR_RPM_MIN, MOTOR_RPM_MAX]
    // rpm = v_screw / pitch; v_screw = flow / S_A
    // flow = vol / t  =>  t = vol / flow = vol * S_A * pitch / rpm
    float sA = area(g_state.syringeA.diameter);
    if (sA > 0.0f && g_state.screwPitch > 0.0f) {
        float tAtRpmMax = vA * sA * g_state.screwPitch / (MAX_FLOW_MLPM * sA);
        float tAtRpmMin = vA / (MOTOR_RPM_MIN * g_state.screwPitch * sA / 1.0f);
        // Simpler: rpm = vScrew/pitch, vScrew = flowA/sA, flowA = vA/t
        // => t = vA / (rpm * pitch * sA)
        float tRpmMax = vA / (MOTOR_RPM_MAX * g_state.screwPitch * sA);
        float tRpmMin = vA / (MOTOR_RPM_MIN * g_state.screwPitch * sA);
        tMin = max(tMin, tRpmMax);
        tMax = min(tMax, tRpmMin);
    }

    if (tMin > tMax) tMin = tMax;
    return {tMin, tMax};
}

}  // namespace calc
