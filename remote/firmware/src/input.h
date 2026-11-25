#pragma once
#include <Arduino.h>

// Энкодер (квадратура на прерываниях), кнопка энкодера с долгим нажатием,
// кнопка СТОП на панели (нажатие — стоп, удержание — парковка).
namespace input {

struct Events {
    int8_t   detents = 0;      // щелчков с прошлого опроса (+ по часовой); при зажатой ручке — 0
    bool     click = false;    // короткое нажатие энкодера
    bool     longPress = false; // удержание энкодера — «назад»
    bool     stop = false;     // кнопка СТОП: срабатывает на нажатие, не на отпускание
    bool     stopWithEnc = false; // СТОП нажали, когда ручка уже зажата — возможно, это начало комбо
    bool     park = false;     // СТОП удержан PARK_HOLD_MS — парковка
    bool     ota = false;      // энкодер и СТОП удержаны вместе OTA_HOLD_MS — режим обновления
    bool     reset = false;    // ручку держат с самого включения RESET_HOLD_MS — сброс настроек пульта
    bool     any() const { return detents || click || longPress || stop || park || ota || reset; }
};

// woke = true после пробуждения: кнопку энкодера, которой разбудили, не считаем
// нажатием, а СТОП, которым разбудили, — считаем.
void begin(bool woke, bool wokeByStop);
Events poll(uint32_t nowMs);
// Сколько СТОП удерживается к этому моменту (0 — не нажат или парковка уже сработала).
uint32_t stopHeldMs(uint32_t nowMs);
// Сколько энкодер и СТОП удерживаются вместе (0 — не вместе или режим уже включён).
uint32_t comboHeldMs(uint32_t nowMs);
// Сколько ручку держат с самого включения (0 — отпустили или сброс уже сработал).
uint32_t resetHeldMs(uint32_t nowMs);

}  // namespace input
