#include "power.h"
#include "remote_config.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

namespace power {

static float    s_v = 0.0f;
static uint32_t s_lastMs = 0;
static uint32_t s_lowSinceMs = 0;
static bool     s_woke = false;
static bool     s_wokeByStop = false;

static float readV() {
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BAT_ADC);
    return mv / 8.0f / 1000.0f * BAT_DIVIDER;
}

void begin() {
    s_woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
    // Загрузка занимает сотни мс — к опросу кнопки короткое нажатие уже кончилось.
    // Какой пин разбудил, помнит аппаратура.
    s_wokeByStop = s_woke && (esp_sleep_get_gpio_wakeup_status() & (1ULL << PIN_BTN_STOP));
    analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
    s_v = readV();
}

void tick(uint32_t nowMs) {
    if (nowMs - s_lastMs < 2000) return;
    s_lastMs = nowMs;
    s_v = s_v * 0.7f + readV() * 0.3f;           // сглаживание: просадки при передаче
    if (hasBattery() && s_v < BAT_EMPTY_V) {
        if (!s_lowSinceMs) s_lowSinceMs = nowMs;
    } else {
        s_lowSinceMs = 0;
    }
}

bool  hasBattery() { return s_v >= BAT_NONE_V && s_v <= BAT_MAX_V; }
float voltage()    { return hasBattery() ? s_v : 0.0f; }
bool  empty()      { return s_lowSinceMs && millis() - s_lowSinceMs > 10000; }
bool  wokeFromSleep() { return s_woke; }
bool  wokeByStop()    { return s_wokeByStop; }

uint8_t percent() {
    if (!hasBattery()) return 100;
    // Грубая кривая разряда Li-ion под нагрузкой ~100 мА.
    static const float V[] = {3.35f, 3.55f, 3.65f, 3.72f, 3.78f, 3.85f, 3.95f, 4.05f, 4.15f};
    static const uint8_t P[] = {0, 8, 18, 30, 42, 55, 70, 85, 100};
    if (s_v <= V[0]) return 0;
    for (int i = 1; i < 9; i++)
        if (s_v < V[i]) return P[i - 1] + (uint8_t)((s_v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
    return 100;
}

void deepSleep() {
    Serial.println("[I] deep sleep");
    Serial.flush();
    // Будят кнопка энкодера, СТОП и поворот энкодера (LOW). Внешние подтяжки 10к
    // держат уровень во сне, внутренние включаем на всякий случай.
    uint64_t mask = 0;
    for (int pin : {PIN_ENC_SW, PIN_BTN_STOP, PIN_ENC_A, PIN_ENC_B}) {
        gpio_pullup_en((gpio_num_t)pin);
        gpio_pulldown_dis((gpio_num_t)pin);
        // Контакт энкодера, замкнутый прямо сейчас, разбудил бы сразу — его не берём.
        bool encPin = pin == PIN_ENC_A || pin == PIN_ENC_B;
        if (!encPin || digitalRead(pin) == HIGH) mask |= 1ULL << pin;
    }
    esp_deep_sleep_enable_gpio_wakeup(mask, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

}  // namespace power
