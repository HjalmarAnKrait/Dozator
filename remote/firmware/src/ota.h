#pragma once
#include <Arduino.h>

// Режим обновления прошивки пульта: своя сеть WiFi и страница загрузки .bin
// (shared/ota_page.h — та же, что у дозатора). ESP-NOW на это время выключен.
namespace ota {

enum class Phase : uint8_t { WAIT, UPLOAD, DONE, ERROR };
struct Status {
    Phase   phase = Phase::WAIT;
    uint8_t percent = 0;
    char    msg[32] = "";          // для экрана: причина ошибки
};

void begin();                      // поднять сеть и сервер (rlink уже выключен)
bool active();
void tick(uint32_t nowMs);         // сервер, таймауты, перезагрузка после записи
const Status& status();
[[noreturn]] void exitToNormal(const char* why);  // перезагрузка в обычный режим; why — в лог и на экран после старта
bool wasActiveBeforeReset();      // пульт перезагрузился сам, не выйдя из режима обновления (читать один раз)
const char* lastExitReason();     // почему вышли из обновления в прошлый раз (или nullptr), читать один раз

}  // namespace ota
