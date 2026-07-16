#pragma once
#include <Arduino.h>

// ─── Simulation ────────────────────────────────────────────────────────────
#define USE_SIMULATED_HW  0    // 0 = реальное железо (RealStepper/RealSwitches)
#define SIM_TIME_FACTOR   1    // используется только симуляцией

// ─── Real HW pins (активная распиновка) ─────────────────────────────────────
// Headless: без дисплея и энкодера. Карта пинов — см. HARDWARE.md §2.1.
// Концевики: 4 сигнала + общий GND (3.3В к коробке тянуть НЕ нужно).
// Драйвер: 3 сигнала + GND.

// Мотор (EN не заведён на МК — на драйвере RST+SLP→+5V, ENABLE не используется):
#define STEP_STEP_PIN  4   // D2 → A4988 STEP
#define STEP_DIR_PIN   5   // D1 → A4988 DIR

// Микрошаг A4988 — теперь управляется МК (для 1/16 все три HIGH):
#define MS1_PIN        13  // D7
#define MS2_PIN        12  // D6
#define MS3_PIN        14  // D5

// Концевики. ⚠️ Полярность РАЗНАЯ из-за особенностей пинов:
//  TOP/A/B — INPUT_PULLUP, замыкание на GND, срабатывание = LOW.
//  BOT (GPIO16) — нет внутренней подтяжки вверх → INPUT_PULLDOWN_16,
//                 подключать на 3.3В, срабатывание = HIGH.
#define SW_TOP_PIN     3   // RX/GPIO3  (INPUT_PULLUP, на GND) — теряется Serial RX
#define SW_A_PIN       0   // D3        (INPUT_PULLUP, на GND) ⚠️ boot-пин: разомкнут при старте
#define SW_B_PIN       2   // D4        (INPUT_PULLUP, на GND) ⚠️ boot-пин: разомкнут при старте
#define SW_BOT_PIN     16  // D0        (INPUT_PULLDOWN_16, на 3.3В, active-HIGH)

// Физическая кнопка СТОП (двухфазная). GPIO15 — boot-пин с подтяжкой ВНИЗ,
// поэтому кнопку подключать на 3.3В (active-HIGH), INPUT.
#define BTN_STOP_PIN   15  // D8/GPIO15 (INPUT, на 3.3В, active-HIGH) ⚠️ boot-пин: разомкнута при старте

// ─── Motor ─────────────────────────────────────────────────────────────────
#define STEPS_PER_MOTOR_REV   200
#define MICROSTEP             16
#define FULL_STEPS_PER_REV    (STEPS_PER_MOTOR_REV * MICROSTEP)  // 3200

// ─── Flow limits ───────────────────────────────────────────────────────────
#define MAX_FLOW_MLPM   3000.0f   // 50 ml/s × 60
#define MIN_FLOW_MLPM   0.33f     // 0.0055 ml/s × 60

// ─── Motor RPM limits ──────────────────────────────────────────────────────
#define MOTOR_RPM_MIN   1.0f      // низкий предел → допускаются длинные дозы (до 15 мин)
#define MOTOR_RPM_MAX   300.0f

// ─── Timing ────────────────────────────────────────────────────────────────
#define WS_THROTTLE_MS            100    // 10 Hz
#define WS_HEARTBEAT_MS           30000  // 30 s

// ─── Switch debounce ───────────────────────────────────────────────────────
#define SWITCH_DEBOUNCE_MS   30

// ─── Безопасность ───────────────────────────────────────────────────────────
#define STUCK_TIMEOUT_MS   120000   // ход к концевику дольше этого = сбой → STOPPED
#define MIN_CALIB_STEPS    500      // калибровка меньше этого H = брак

// ─── WiFi AP ───────────────────────────────────────────────────────────────
#define WIFI_SSID       "dozator"
#define WIFI_PASSWORD   "dozator123"   // WPA2: минимум 8 символов, иначе AP не стартует

// ─── Settings debounce ─────────────────────────────────────────────────────
#define SETTINGS_SAVE_DELAY_MS  2000

// ─── Sleep default ─────────────────────────────────────────────────────────
#define DEFAULT_SLEEP_SEC   15

// ─── Font default (вестигиальное поле AppState; OLED удалён) ────────────────
#define DEFAULT_FONT_INDEX  1
