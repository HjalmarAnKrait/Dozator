#pragma once
#include <Arduino.h>
#include "RemoteProtocol.h"

// Связь с дозатором по ESP-NOW (сторона пульта).
namespace rlink {

enum class AckResult : uint8_t { NONE, OK, REJECTED, NO_ANSWER };

void begin();
void tick(uint32_t nowMs);               // HELLO шлём всегда, пока пульт не спит глубоко
void shutdown();                          // перед глубоким сном

// Связь в порядке, только если работают оба направления:
bool downlinkOk(uint32_t nowMs);          // пульт слышит дозатор (свежий STATE)
bool uplinkOk(uint32_t nowMs);            // дозатор слышит пульт (вернул эхо свежего HELLO)
bool online(uint32_t nowMs);              // оба направления
bool heartbeat();                         // переключается на каждый принятый STATE — мигалка на экране
uint8_t protoMismatch(uint32_t nowMs);    // ≠0 — рядом дозатор с другой версией протокола (её номер)
void dozatorFw(char* out, size_t n);      // версия прошивки дозатора ("--" — ещё не слышали)
int8_t rssi();                            // уровень сигнала дозатора, дБм (0 — ещё не измерен)
uint32_t rttMs();                         // время HELLO → ответ, мс
bool everConnected();
uint32_t stateAgeMs(uint32_t nowMs);
const RpStateMsg& state();
const uint8_t* peerMac();                 // MAC дозатора (если найден)

// expect — экран, на котором нажали (RpScreen). Дозатор отклонит команду, если он
// уже в другом состоянии. RPS_ANY — для СТОП/парковки, которые годятся везде.
// RPC_HALT и RPC_HOME идут вне очереди: сбрасывают ждущие команды и уходят первыми.
void sendCmd(uint8_t cmd, uint8_t expect = RPS_ANY);
void sendSet(uint8_t field, float value);
void sendPreset(uint8_t index, float vol, float diam);
void sendJog(int32_t steps);

// Результат последней команды; после чтения сбрасывается в NONE.
AckResult takeAck();
uint8_t lastAckType();                    // RpType команды, на которую пришёл последний ACK
bool takeHaltFailed();                    // СТОП не дошёл (нет ответа) — один раз true
bool takeSettingDropped();                // СТОП/парковка сбросили очередь, в ней была настройка — один раз true

}  // namespace rlink
