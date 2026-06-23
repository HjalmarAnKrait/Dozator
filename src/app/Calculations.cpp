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
    // flowA в мл/мин → мм³/мин (×1000), делим на площадь (мм²) → мм/мин.
    return flowA() * 1000.0f / sA;
}

float flowB() {
    // vScrew(мм/мин) × площадь B(мм²) = мм³/мин → мл/мин (÷1000).
    return screwSpeedMmPerMin() * area(g_state.syringeB.diameter) / 1000.0f;
}

float motorRpm() {
    if (g_state.screwPitch <= 0.0f) return 0.0f;
    return screwSpeedMmPerMin() / g_state.screwPitch;
}

TimeRange timeRangeMin() {
    float vA = volA();
    if (vA <= 0.0f) return {0.0f, 0.0f};

    // Предел по потоку: t = объём / поток.
    float tMin = vA / MAX_FLOW_MLPM;   // мин. время при макс. потоке
    float tMax = vA / MIN_FLOW_MLPM;   // макс. время при мин. потоке

    // Предел по оборотам мотора:
    //   rpm = vScrew/pitch,  vScrew = vA*1000/(t*sA)  =>  t = vA*1000/(rpm*sA*pitch)
    float sA = area(g_state.syringeA.diameter);
    if (sA > 0.0f && g_state.screwPitch > 0.0f) {
        float tRpmMax = vA * 1000.0f / (MOTOR_RPM_MAX * sA * g_state.screwPitch);  // при макс. rpm
        float tRpmMin = vA * 1000.0f / (MOTOR_RPM_MIN * sA * g_state.screwPitch);  // при мин. rpm
        tMin = max(tMin, tRpmMax);
        tMax = min(tMax, tRpmMin);
    }

    if (tMin > tMax) tMin = tMax;
    return {tMin, tMax};
}

}  // namespace calc
