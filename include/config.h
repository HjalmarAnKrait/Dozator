#pragma once
#include <Arduino.h>

// ─── DEAD CODE: OLED (I2C) ──────────────────────────────────────────────────
// Дисплей отключён — пины D1/D2 переназначены на концевики (см. ниже).
// Дефайны оставлены, чтобы старый код OLED компилировался; он не инициализируется.
#define PIN_SCL       5    // D1  [DEAD — теперь SW_TOP_PIN]
#define PIN_SDA       4    // D2  [DEAD — теперь SW_A_PIN]

// ─── DEAD CODE: Encoder ─────────────────────────────────────────────────────
// Энкодер отключён — пины D5/D6/D7 переназначены (см. ниже).
#define PIN_ENC_CLK   14   // D5  [DEAD — теперь STEP_STEP_PIN]
#define PIN_ENC_DT    12   // D6  [DEAD — теперь SW_B_PIN]
#define PIN_ENC_SW    13   // D7  [DEAD — теперь SW_BOT_PIN]

// ─── Отключённая периферия (dead code) ──────────────────────────────────────
// Дисплей и энкодер физически отключены, их пины переназначены. Ставим 0,
// чтобы их код не инициализировался и не трогал новые пины (мотор/концевики).
#define USE_OLED      0
#define USE_ENCODER   0

// ─── Simulation ────────────────────────────────────────────────────────────
#define USE_SIMULATED_HW  0    // 0 = реальное железо (RealStepper/RealSwitches)
#define SIM_TIME_FACTOR   1    // используется только симуляцией

// ─── Real HW pins (активная распиновка) ─────────────────────────────────────
// Без дисплея и энкодера. Карта пинов — см. HARDWARE.md §2.1.
// Концевики: 4 сигнала + общий GND (3.3В к коробке тянуть НЕ нужно).
// Драйвер: 3 сигнала + GND.

// Мотор:
#define STEP_STEP_PIN  14  // D5 – output, чистый пин, для STEP-импульсов
#define STEP_DIR_PIN   16  // D0 – output (направление меняется редко)
#define STEP_EN_PIN    0   // D3 – output, active-LOW; boot-пин: pull-up→HIGH→мотор off при старте

// 4 концевика — все INPUT_PULLUP, замыкание на общий GND, срабатывание = LOW:
#define SW_TOP_PIN     5   // D1
#define SW_A_PIN       4   // D2
#define SW_B_PIN       12  // D6
#define SW_BOT_PIN     13  // D7

// ─── Motor ─────────────────────────────────────────────────────────────────
#define STEPS_PER_MOTOR_REV   200
#define MICROSTEP             16
#define FULL_STEPS_PER_REV    (STEPS_PER_MOTOR_REV * MICROSTEP)  // 3200

// ─── Flow limits ───────────────────────────────────────────────────────────
#define MAX_FLOW_MLPM   3000.0f   // 50 ml/s × 60
#define MIN_FLOW_MLPM   0.33f     // 0.0055 ml/s × 60

// ─── Motor RPM limits ──────────────────────────────────────────────────────
#define MOTOR_RPM_MIN   10.0f
#define MOTOR_RPM_MAX   300.0f

// ─── Timing ────────────────────────────────────────────────────────────────
#define OLED_RENDER_INTERVAL_MS   50     // 20 Hz
#define WS_THROTTLE_MS            100    // 10 Hz
#define WS_HEARTBEAT_MS           30000  // 30 s

// ─── Encoder / button ──────────────────────────────────────────────────────
#define ENC_DEBOUNCE_MS      30
#define ENC_CLICK_MAX_MS     600
#define ENC_LONG_MS          800
#define ENC_STEPS_PER_DETENT 4

// ─── Event queue ───────────────────────────────────────────────────────────
#define ENCODER_QUEUE_MAX 16

// ─── WiFi AP ───────────────────────────────────────────────────────────────
#define WIFI_SSID       "dozator"
#define WIFI_PASSWORD   "dozator123"   // WPA2: минимум 8 символов, иначе AP не стартует

// ─── Settings debounce ─────────────────────────────────────────────────────
#define SETTINGS_SAVE_DELAY_MS  2000

// ─── Sleep default ─────────────────────────────────────────────────────────
#define DEFAULT_SLEEP_SEC   15

// ─── Font default (index into g_fontTiers[]) ───────────────────────────────
#define DEFAULT_FONT_INDEX  1   // S = 6x13, medium
