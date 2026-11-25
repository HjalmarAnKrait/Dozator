#pragma once
#include <Arduino.h>

// Аккумулятор и глубокий сон.
namespace power {

void  begin();
void  tick(uint32_t nowMs);
float voltage();          // В; 0 — банки нет (питание от USB)
uint8_t percent();        // 0..100
bool  hasBattery();
bool  empty();            // банка села — пора спать
bool  wokeFromSleep();    // этот запуск — пробуждение кнопкой
bool  wokeByStop();       // разбудила именно кнопка СТОП (даже если её уже отпустили)
[[noreturn]] void deepSleep();

}  // namespace power
