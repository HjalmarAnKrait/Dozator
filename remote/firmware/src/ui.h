#pragma once
#include <Arduino.h>
#include "input.h"

// Меню пульта на OLED 0.96" 128x64 — по прототипу remote/prototype.html.
namespace ui {

void begin();
void handle(const input::Events& e, uint32_t nowMs);
void tick(uint32_t nowMs);              // реакция на состояние дозатора, ACK, сон; отрисовка
bool awake();                           // экран включён
bool wantsDeepSleep(uint32_t nowMs);
void showMessage(const char* text, uint32_t ms);
void alert(const char* text, uint32_t ms);   // то же, но и разбудить экран
void prepareSleep();                    // погасить экран перед глубоким сном

// Режим обновления прошивки (энкодер + СТОП 10 с).
bool otaRequested();                    // комбинация сработала (сбрасывается при чтении)
void enterOta(uint32_t nowMs);          // подготовить экран режима обновления
void tickOta(const input::Events& e, uint32_t nowMs);   // экран и выход из режима

}  // namespace ui
