#include "RealStepper.h"
#include "config.h"

/**
 * Реализация шагового драйвера на timer1.
 *
 * timer1 @ TIM_DIV16 = 5 тиков/мкс. Используем "toggle"-схему: каждое
 * прерывание переключает STEP. Период таймера = ПОЛОВИНА периода шага,
 * поэтому один полный шаг = два прерывания (фронт вверх = шаг).
 *
 *   half_period_us = 1e6 / speed / 2
 *   ticks          = half_period_us * 5 = 2.5e6 / speed
 *
 * Состояние — в файловой области (один экземпляр; ISR не может быть
 * нестатическим методом). STEP дёргаем напрямую через регистры GPOS/GPOC —
 * digitalWrite() в ISR слишком медленный.
 */

static constexpr uint32_t STEP_MASK = (1u << STEP_STEP_PIN);  // STEP = GPIO14 (0..15)

static volatile int32_t s_pos       = 0;   // текущая позиция в шагах
static volatile int32_t s_remaining = 0;   // сколько шагов осталось сделать
static volatile int8_t  s_dir       = 1;   // +1 / -1
static volatile bool    s_level     = false; // текущий уровень STEP
static volatile bool    s_running   = false;

// Защита min/max интервала таймера (тики @5МГц): ~25 кшаг/с .. ~1.6 с/шаг.
static constexpr uint32_t TICKS_MIN = 200;        // 40 мкс полупериод
static constexpr uint32_t TICKS_MAX = 8000000UL;

static void IRAM_ATTR stepISR() {
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
    pinMode(STEP_EN_PIN, OUTPUT);
    digitalWrite(STEP_EN_PIN, HIGH);   // EN active-LOW → HIGH = выключен

    timer1_isr_init();
    timer1_attachInterrupt(stepISR);
    // таймер не запускаем здесь — только в moveTo()
}

void RealStepper::moveTo(int32_t targetSteps, float speedStepsPerSec) {
    int32_t delta = targetSteps - s_pos;
    if (delta == 0) { stop(); return; }

    int8_t dir = (delta > 0) ? 1 : -1;
    // Конвенция направления: положительные шаги → DIR HIGH.
    // Если мотор крутится не в ту сторону — поменяйте местами провода
    // обмотки ИЛИ инвертируйте эту строку.
    digitalWrite(STEP_DIR_PIN, (dir > 0) ? HIGH : LOW);

    float spd = speedStepsPerSec;
    if (spd < 1.0f) spd = 1.0f;
    uint32_t ticks = (uint32_t)(2500000.0f / spd);
    if (ticks < TICKS_MIN) ticks = TICKS_MIN;
    if (ticks > TICKS_MAX) ticks = TICKS_MAX;

    noInterrupts();
    s_dir       = dir;
    s_remaining = (delta > 0) ? delta : -delta;
    s_level     = false;
    s_running   = true;
    interrupts();

    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_LOOP);
    timer1_write(ticks);
}

void RealStepper::stop() {
    timer1_disable();
    noInterrupts();
    s_remaining = 0;
    s_running   = false;
    s_level     = false;
    interrupts();
    GPOC = STEP_MASK;   // гарантированно опускаем STEP
}

int32_t RealStepper::currentPosition() const {
    noInterrupts();
    int32_t p = s_pos;
    interrupts();
    return p;
}

bool RealStepper::isBusy() const {
    return s_running;
}

void RealStepper::enable() {
    digitalWrite(STEP_EN_PIN, LOW);    // active-LOW
}

void RealStepper::disable() {
    digitalWrite(STEP_EN_PIN, HIGH);
}

void RealStepper::tick(uint32_t nowMs) {
    (void)nowMs;   // движение полностью в ISR
}
