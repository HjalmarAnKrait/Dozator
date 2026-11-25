#pragma once
#include <Arduino.h>

// ─── Simulation ────────────────────────────────────────────────────────────
#define USE_SIMULATED_HW  0    // 0 = реальное железо (RealStepper/RealSwitches)
#define SIM_TIME_FACTOR   1    // используется только симуляцией

// ─── Real HW pins (активная распиновка) ─────────────────────────────────────
// Headless: без дисплея и энкодера. Карта пинов — см. HARDWARE.md §2.1.
// Концевики: 4 сигнала + общий GND (3.3В к коробке тянуть НЕ нужно).
// Драйвер: 3 сигнала + GND.

// Мотор. A4988: EN→GND (всегда вкл.), RST+SLP→+5V, MS1/2/3→+5V (1/16, перемычки — не МК).
#define STEP_STEP_PIN  4   // D2 → A4988 STEP
#define STEP_DIR_PIN   5   // D1 → A4988 DIR

// Концевики + кнопка — ВСЕ INPUT_PULLUP, замыкание на GND, срабатывание = LOW.
#define SW_TOP_PIN     14  // D5
#define SW_A_PIN       3   // RX/GPIO3 — на GND (теряется Serial RX)
#define SW_B_PIN       13  // D7
#define SW_BOT_PIN     12  // D6

// Физическая кнопка СТОП (двухфазная) — D3/GPIO0, INPUT_PULLUP, на GND (есть подтяжка 10к).
// ⚠️ boot-пин: не держать нажатой при включении (кнопка в покое разомкнута — норма).
#define BTN_STOP_PIN   0   // D3

// ─── Motor ─────────────────────────────────────────────────────────────────
#define STEPS_PER_MOTOR_REV   200
#define MICROSTEP             16
#define FULL_STEPS_PER_REV    (STEPS_PER_MOTOR_REV * MICROSTEP)  // 3200

// ─── Flow limits ───────────────────────────────────────────────────────────
#define MAX_FLOW_MLPM   3000.0f   // 50 ml/s × 60

// ─── Скорость и разгон мотора ───────────────────────────────────────────────
// Пределы — по мотору (об/мин), а не по винту: NEMA17 + A4988 с винтом от принтера.
// 120 об/мин = 6400 шаг/с = 16 мм/с при Tr8x8 (4 мм/с при Tr8x2). Быстрее мотор под
// нагрузкой шприца срывается, а пропуск шагов прошивка не видит.
#define MOTOR_RPM_MAX        120.0f
#define MOTOR_SPS_MIN        50.0f     // нижний предел настроек скорости (парковка/зарядка/ручная), шаг/с
#define MOTOR_SPS_MAX        (MOTOR_RPM_MAX * FULL_STEPS_PER_REV / 60.0f)   // 6400 шаг/с
// Разгон: до MOTOR_START_RPM мотор трогается сразу, выше — разгоняется с ускорением
// MOTOR_ACCEL_RPS2 и так же тормозит перед целью. На концевике и по СТОП — мгновенный стоп.
#define MOTOR_START_RPM      15.0f     // 800 шаг/с
#define MOTOR_ACCEL_RPS2     2.0f      // об/с² → 6400 шаг/с²: от 800 до 6400 шаг/с за ~0,9 с
// Доза: самая медленная скорость, на которой ещё считаем время. Шаг 1/16 при 1 шаг/с — это
// 2,5 мкм/с при Tr8x8, мотор идёт ровно; ограничение времени дозы даёт DOSE_TIME_MAX_MIN.
#define DOSE_SPS_MIN         1.0f
#define DOSE_TIME_MAX_MIN    20.0f     // как в вебе и на пульте

// ─── Timing ────────────────────────────────────────────────────────────────
#define WS_THROTTLE_MS            100    // 10 Hz
#define WS_HEARTBEAT_MS           30000  // 30 s

// ─── Switch debounce ───────────────────────────────────────────────────────
#define SWITCH_DEBOUNCE_MS   30   // отпускание концевика
#define SWITCH_PRESS_MS      5    // нажатие концевика: быстро, чтобы не проезжать точку срабатывания

// ─── Безопасность ───────────────────────────────────────────────────────────
// Ход к концевику ограничен расстоянием: доехали до предела, а концевик не сработал — сбой.
// Предел — H с запасом (TRAVEL_SPARE_*), до калибровки — MAX_TRAVEL_MM. Таймаут по времени —
// страховка: ход/скорость × STUCK_TIME_FACTOR + STUCK_EXTRA_MS, но не меньше STUCK_MIN_MS.
#define STUCK_TIME_FACTOR  1.5f
#define STUCK_EXTRA_MS     5000
#define STUCK_MIN_MS       10000
#define MAX_TRAVEL_MM      200.0f   // ход каретки сверху, пока H не откалиброван (≈ 100 с при 2 мм/с)
#define TRAVEL_SPARE_PCT   30       // калиброванный H + 30 %, но не меньше чем +TRAVEL_SPARE_MM
#define TRAVEL_SPARE_MM    20.0f
#define FAR_MOVE_STEPS     5000000  // ручной режим, удержание: цель заведомо дальше любого хода
#define MIN_CALIB_STEPS    500      // калибровка меньше этого H = брак
// Калибровка BOT: быстро до концевика, отъезд CALIB_BACKOFF_MM, медленный подход
// CALIB_SLOW_MM_S — точка срабатывания не зависит от выбега на скорости парковки.
#define CALIB_BACKOFF_MM   2.0f
#define CALIB_SLOW_MM_S    0.5f
// H хранится на столько короче точки BOT: доза кончается по ходу, концевик — страховка.
// Запас покрывает и разницу выбега на TOP при парковке на другой скорости.
#define CALIB_END_MARGIN_MM 0.5f
#define JOG_HOLD_DEADMAN_MS 1000    // удержание в ручном режиме: без повтора команды дольше этого — стоп

// ─── Выносной пульт (ESP-NOW) ───────────────────────────────────────────────
// Канал WiFi фиксирован: пульт шлёт ESP-NOW на этом канале, точка доступа на нём же.
// Сам канал и ключ пары — в shared/RemoteProtocol.h (общий файл с прошивкой пульта).
#define REMOTE_LINK_TIMEOUT_MS  3000   // пульт молчит дольше — перестаём слать ему STATE
#define REMOTE_STATE_MOVING_MS  100    // STATE при движении (10 Гц)
#define REMOTE_STATE_IDLE_MS    500    // STATE в покое (2 Гц)
#define REMOTE_STATE_MIN_MS     50     // не чаще, даже при изменениях (20 Гц)
#define STOP_PARK_GUARD_MS      1500   // вторая фаза кнопки СТОП (парковка) — не раньше, чем через столько после стопа

// ─── WiFi AP ───────────────────────────────────────────────────────────────
#define WIFI_SSID       "dozator"
#define WIFI_PASSWORD   "dozator123"   // WPA2: минимум 8 символов, иначе AP не стартует

// ─── Settings debounce ─────────────────────────────────────────────────────
#define SETTINGS_SAVE_DELAY_MS  2000

// ─── Sleep default ─────────────────────────────────────────────────────────
#define DEFAULT_SLEEP_SEC   15

// ─── Font default (вестигиальное поле AppState; OLED удалён) ────────────────
#define DEFAULT_FONT_INDEX  1
