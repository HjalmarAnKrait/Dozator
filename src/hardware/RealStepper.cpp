#include "RealStepper.h"
#include "config.h"
#include "../util/Logger.h"

/**
 * Реализация шагового драйвера на timer1 с плавным разгоном и торможением.
 *
 * timer1 @ TIM_DIV16 = 5 тиков/мкс. Используем "toggle"-схему: каждое
 * прерывание переключает STEP. Период таймера = ПОЛОВИНА периода шага,
 * поэтому один полный шаг = два прерывания (фронт вверх = шаг).
 *
 *   half_period_us = 1e6 / speed / 2
 *   ticks          = half_period_us * 5 = 2.5e6 / speed
 *
 * Разгон считает tick() из loop() (в ISR нет плавающей точки и деления — они могут
 * лежать во flash): по прошедшему времени меняет скорость и кладёт новый период в
 * s_nextTicks, а ISR подхватывает его на ближайшем прерывании. Писать период из loop()
 * напрямую нельзя: запись в таймер перезапускает отсчёт, и медленный шаг не наступил бы.
 *
 *   до vStart (MOTOR_START_RPM)   — без разгона, сразу на заданной скорости;
 *   выше                          — ускорение MOTOR_ACCEL_RPS2 до цели и так же
 *                                   торможение, когда до цели осталось v²/2a шагов.
 *
 * Новая цель на ходу в ту же сторону — едем дальше без остановки. В обратную —
 * сначала тормозим до vStart, потом трогаемся назад. stop() — мгновенный (концевик, СТОП).
 *
 * Состояние — в файловой области (один экземпляр; ISR не может быть
 * нестатическим методом). STEP дёргаем напрямую через регистры GPOS/GPOC —
 * digitalWrite() в ISR слишком медленный.
 */

static constexpr uint32_t STEP_MASK = (1u << STEP_STEP_PIN);  // STEP = GPIO4 (0..15)

static volatile int32_t  s_pos       = 0;   // текущая позиция в шагах
static volatile int32_t  s_remaining = 0;   // сколько шагов осталось сделать
static volatile int8_t   s_dir       = 1;   // +1 / -1
static volatile bool     s_level     = false; // текущий уровень STEP
static volatile bool     s_running   = false;
static volatile uint32_t s_nextTicks = 0;   // период от tick(); ISR применяет его сам
static volatile uint32_t s_curTicks  = 0;

// Защита min/max интервала таймера (тики @5МГц). TICKS_MIN=120 → до ~20800 шаг/с
// (с запасом над MOTOR_SPS_MAX). TICKS_MAX → ~1.6 с/шаг.
static constexpr uint32_t TICKS_MIN = 120;        // ~24 мкс полупериод
static constexpr uint32_t TICKS_MAX = 8000000UL;

static constexpr float V_START = MOTOR_START_RPM * FULL_STEPS_PER_REV / 60.0f;   // шаг/с
static constexpr float ACCEL   = MOTOR_ACCEL_RPS2 * FULL_STEPS_PER_REV;          // шаг/с²
// Торможение начинаем с запасом на задержку loop() (WiFi может держать его десятки мс).
static constexpr float BRAKE_LOOKAHEAD_S = 0.03f;

// Состояние разгона — только из loop().
static float    s_v      = 0;      // текущая скорость, шаг/с
static float    s_vmax   = 0;      // заданная скорость хода
static uint32_t s_lastUs = 0;
static bool     s_pending = false; // ждёт старта после торможения (смена направления)
static int32_t  s_pendTarget = 0;
static float    s_pendSpeed  = 0;

static uint32_t ticksFor(float spd) {
    if (spd < 1.0f) spd = 1.0f;
    uint32_t t = (uint32_t)(2500000.0f / spd);
    if (t < TICKS_MIN) t = TICKS_MIN;
    if (t > TICKS_MAX) t = TICKS_MAX;
    return t;
}

static float clampSpeed(float spd) {
    if (spd < 1.0f) spd = 1.0f;
    if (spd > MOTOR_SPS_MAX) spd = MOTOR_SPS_MAX;
    return spd;
}

static void IRAM_ATTR stepISR() {
    uint32_t nt = s_nextTicks;
    if (nt != s_curTicks) { s_curTicks = nt; timer1_write(nt); }   // новый период от разгона
    if (s_level) {
        // STEP был HIGH → опускаем (завершаем импульс)
        GPOC = STEP_MASK;
        s_level = false;
        if (s_remaining <= 0) {
            timer1_disable();
            s_running = false;
        }
    } else {
        if (s_remaining <= 0) {
            timer1_disable();
            s_running = false;
            return;
        }
        // фронт вверх = один шаг
        GPOS = STEP_MASK;
        s_level = true;
        s_pos += s_dir;
        s_remaining--;
    }
}

RealStepper::RealStepper() {
    pinMode(STEP_STEP_PIN, OUTPUT);
    digitalWrite(STEP_STEP_PIN, LOW);
    pinMode(STEP_DIR_PIN, OUTPUT);
    digitalWrite(STEP_DIR_PIN, LOW);
    // EN не трогаем: он посажен на GND (драйвер всегда включён), а пин D3/GPIO0
    // занят физической кнопкой СТОП. enable()/disable() — no-op.

    timer1_isr_init();
    timer1_attachInterrupt(stepISR);
    // таймер не запускаем здесь — только в moveTo()
}

// Остановить таймер и довести начатый импульс. Иначе сброс s_level при высоком STEP
// засчитал бы шаг без фронта, а DIR менялся бы посреди импульса.
static void haltTimer() {
    timer1_disable();
    noInterrupts();
    s_remaining = 0;
    s_running   = false;
    interrupts();
    if (s_level) {
        delayMicroseconds(2);   // A4988: импульс STEP ≥ 1 мкс
        GPOC = STEP_MASK;
        s_level = false;
    }
}

// Старт с места: первая скорость — vStart (или ниже, если ход медленный).
static void startFrom(int32_t targetSteps, float spd) {
    int32_t delta = targetSteps - s_pos;
    if (delta == 0) return;
    int8_t dir = (delta > 0) ? 1 : -1;
    // Конвенция направления: положительные шаги → DIR HIGH.
    digitalWrite(STEP_DIR_PIN, (dir > 0) ? HIGH : LOW);
    delayMicroseconds(1);   // A4988: установка DIR до фронта STEP ≥ 200 нс

    s_vmax   = spd;
    s_v      = min(spd, V_START);
    s_lastUs = micros();
    uint32_t ticks = ticksFor(s_v);

    noInterrupts();
    s_dir       = dir;
    s_remaining = (delta > 0) ? delta : -delta;
    s_level     = false;
    s_nextTicks = ticks;
    s_curTicks  = ticks;
    s_running   = true;
    interrupts();

    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(ticks);
}

void RealStepper::moveTo(int32_t targetSteps, float speedStepsPerSec) {
    float spd = clampSpeed(speedStepsPerSec);

    if (s_running) {
        noInterrupts();
        int32_t delta = targetSteps - s_pos;
        bool sameDir = delta != 0 && ((delta > 0) == (s_dir > 0));
        if (sameDir) s_remaining = (delta > 0) ? delta : -delta;   // продлеваем/укорачиваем ход
        interrupts();
        if (sameDir) {
            s_pending = false;
            s_vmax = spd;
            LOG_INFO("Stepper retarget=%ld v=%.0f", (long)targetSteps, s_v);
            return;
        }
        if (s_v > V_START) {
            // Разворот на скорости: сначала тормозим (tick), потом трогаемся назад.
            s_pending    = true;
            s_pendTarget = targetSteps;
            s_pendSpeed  = spd;
            LOG_INFO("Stepper brake, then target=%ld", (long)targetSteps);
            return;
        }
        haltTimer();
    }

    s_pending = false;
    if (targetSteps == s_pos) { stop(); return; }
    startFrom(targetSteps, spd);
    LOG_INFO("Stepper moveTo target=%ld pos=%ld v=%.0f->%.0f",
             (long)targetSteps, (long)s_pos, s_v, s_vmax);
}

void RealStepper::stop() {
    timer1_disable();
    noInterrupts();
    s_remaining = 0;
    s_running   = false;
    s_level     = false;
    interrupts();
    GPOC = STEP_MASK;   // гарантированно опускаем STEP
    s_pending = false;
    s_v = 0;
}

int32_t RealStepper::currentPosition() const {
    noInterrupts();
    int32_t p = s_pos;
    interrupts();
    return p;
}

int32_t RealStepper::targetPosition() const {
    noInterrupts();
    int32_t t = s_running ? s_pos + s_dir * s_remaining : s_pos;
    interrupts();
    return t;
}

bool RealStepper::isBusy() const {
    return s_running || s_pending;
}

void RealStepper::zero() {
    noInterrupts();
    s_pos = 0;
    interrupts();
}

void RealStepper::enable() {
    // no-op: EN на GND (драйвер всегда включён).
}

void RealStepper::disable() {
    // no-op: EN на GND.
}

void RealStepper::tick(uint32_t nowMs) {
    (void)nowMs;
    if (!s_running) {
        if (s_pending) {                 // доторозили до конца хода — трогаемся в новую сторону
            s_pending = false;
            if (s_pendTarget != s_pos) startFrom(s_pendTarget, s_pendSpeed);
        }
        return;
    }

    uint32_t nowUs = micros();
    float dt = (nowUs - s_lastUs) * 1e-6f;
    s_lastUs = nowUs;
    if (dt > 0.05f) dt = 0.05f;          // loop долго стоял — не прыгаем скоростью

    if (s_vmax <= V_START && !s_pending) {   // медленный ход: без разгона
        if (s_v != s_vmax) { s_v = s_vmax; s_nextTicks = ticksFor(s_v); }
        return;
    }

    noInterrupts();
    int32_t rem = s_remaining;
    interrupts();

    float brakeSteps = (s_v * s_v - V_START * V_START) / (2.0f * ACCEL) + s_v * BRAKE_LOOKAHEAD_S;
    float v = s_v;
    if (s_pending || rem <= brakeSteps) {
        v = max(V_START, v - ACCEL * dt);
        if (s_pending && v <= V_START) {
            // Доторозили — обрываем ход здесь; tick() следующего цикла тронет в новую сторону.
            noInterrupts();
            s_remaining = 0;
            interrupts();
        }
    } else if (v < s_vmax) {
        v = min(s_vmax, v + ACCEL * dt);
    } else if (v > s_vmax) {
        v = max(s_vmax, v - ACCEL * dt);
    }
    if (v != s_v) {
        s_v = v;
        s_nextTicks = ticksFor(v);
    }
}
