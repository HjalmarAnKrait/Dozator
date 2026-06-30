#pragma once
#include <Arduino.h>
#include "config.h"

constexpr uint8_t MAX_PRESETS = 8;

struct SyringePreset {
    float vol;    // ml
    float diam;   // mm (outer diameter)
};

struct SyringeChoice {
    int8_t presetIdx;  // index into presets[]
    float  diameter;   // actual diameter (may differ from preset after manual edit)
};

struct DosingProgress {
    float elapsedSec;
    float totalSec;
    float progress;   // 0..1
    float volumeA;    // ml actually delivered
    float volumeB;
};

struct SwitchStates {
    bool top;
    bool a;
    bool b;
    bool bot;
};

enum class Screen : uint8_t {
    IDLE,        // стартовое состояние: ничего не делаем, ждём команды пользователя
    PARKING,
    PARKED,
    CHARGING,
    CHARGED,
    DOSING,
    DONE,
    SERVICE_MENU,
    SERVICE_PITCH,
    SERVICE_SLEEP,
    SERVICE_SYRINGES,
    SERVICE_SYRINGE_EDIT,
    SERVICE_FONT
};

struct BreadcrumbEntry {
    Screen  screen;
    uint8_t cursor;
};

struct AppState {
    Screen  screen      = Screen::IDLE;
    uint8_t cursorIndex = 0;
    bool    editing     = false;

    // ── Settings (persisted) ───────────────────────────────────────────────
    float    screwPitch       = 2.0f;     // mm/rev
    uint16_t sleepTimeoutSec  = DEFAULT_SLEEP_SEC;
    uint8_t  fontIndex        = DEFAULT_FONT_INDEX;
    bool     circularNav      = true;
    SyringePreset presets[MAX_PRESETS];
    uint8_t  presetsCount     = 3;

    // ── Per-cycle ──────────────────────────────────────────────────────────
    SyringeChoice syringeA    = {1, 19.0f};
    SyringeChoice syringeB    = {2, 28.6f};
    float         doseTimeMin = 2.0f;
    SwitchStates  switches    = {false, false, false, false};
    // Живые показания концевиков (ILimitSwitches::read()) — для постоянной
    // отладочной индикации в UI, независимо от экрана/стадии.
    SwitchStates  rawSwitches = {false, false, false, false};
    DosingProgress dosing     = {0, 0, 0, 0, 0};

    // ── UI state ───────────────────────────────────────────────────────────
    bool     displaySleeping     = false;
    uint32_t lastInteractionMs   = 0;
    int8_t   editingPresetIdx    = -1;  // used in SERVICE_SYRINGE_EDIT

    BreadcrumbEntry breadcrumb[6];
    uint8_t         breadcrumbDepth = 0;
};

extern AppState g_state;

// ── Default preset values ─────────────────────────────────────────────────
inline void initDefaultPresets(AppState& s) {
    s.presetsCount = 3;
    s.presets[0] = {10.0f, 14.5f};
    s.presets[1] = {20.0f, 19.0f};
    s.presets[2] = {50.0f, 28.6f};
}
