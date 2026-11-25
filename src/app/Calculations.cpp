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

// Оставшийся ход дозы H − L, мм (≤ 0 — не определён).
static float remainingDoseMm() {
    if (g_state.fullPathSteps <= 0) return 0.0f;
    int32_t L2 = g_state.fullPathSteps - g_state.motorPos;
    if (L2 <= 0) return 0.0f;
    return L2 * (g_state.screwPitch / (float)FULL_STEPS_PER_REV);
}

float doseVolA() {
    if (g_state.screen == Screen::CHARGED) {
        float mm = remainingDoseMm();
        if (mm > 0.0f) return distToVolumeMl(g_state.syringeA.diameter, mm);
    }
    if ((g_state.screen == Screen::DOSING || g_state.screen == Screen::DONE) && g_state.planVolA > 0.0f)
        return g_state.planVolA;
    return volA();
}

float doseVolB() {
    if (g_state.screen == Screen::CHARGED) {
        float mm = remainingDoseMm();
        if (mm > 0.0f) return distToVolumeMl(g_state.syringeB.diameter, mm);
    }
    if ((g_state.screen == Screen::DOSING || g_state.screen == Screen::DONE) && g_state.planVolB > 0.0f)
        return g_state.planVolB;
    return volB();
}

float flowA() {
    if (g_state.doseTimeMin <= 0.0f) return 0.0f;
    return doseVolA() / g_state.doseTimeMin;
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

float distToVolumeMl(float diameterMm, float distMm) {
    // объём = площадь(мм²) × ход(мм) = мм³ → мл (÷1000)
    return area(diameterMm) * distMm / 1000.0f;
}

TimeRange timeRangeMin() {
    float vA = doseVolA();
    if (vA <= 0.0f) return {0.0f, 0.0f};

    // Предел по потоку: t = объём / поток. Нижнего предела потока нет — малую дозу
    // тоже можно растянуть до DOSE_TIME_MAX_MIN.
    float tMin = vA / MAX_FLOW_MLPM;   // мин. время при макс. потоке
    float tMax = DOSE_TIME_MAX_MIN;

    // Предел по скорости мотора. Ход дозы в шагах: объём → мм (÷ площадь) → обороты (÷ шаг винта).
    //   Быстрее MOTOR_SPS_MAX мотор срывается; медленнее DOSE_SPS_MIN не едем.
    //   Нижнего предела оборотов у шагового мотора нет: длинную дозу ограничивает DOSE_TIME_MAX_MIN.
    float sA = area(g_state.syringeA.diameter);
    if (sA > 0.0f && g_state.screwPitch > 0.0f) {
        float steps = vA * 1000.0f / sA / g_state.screwPitch * FULL_STEPS_PER_REV;
        tMin = max(tMin, steps / MOTOR_SPS_MAX / 60.0f);
        tMax = min(tMax, steps / DOSE_SPS_MIN / 60.0f);
    }
    if (tMin > tMax) tMin = tMax;
    return {tMin, tMax};
}

}  // namespace calc
