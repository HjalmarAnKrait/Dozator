#pragma once
#include <Arduino.h>

class WsProtocol;
class StateMachine;

/**
 * Канал выносного пульта (ESP-NOW). Протокол — shared/RemoteProtocol.h.
 *
 * Команды пульта идут через те же обработчики, что и веб (WsProtocol), поэтому
 * проверки «какая команда в каком состоянии разрешена» у них общие.
 *
 * Колбэк приёма только кладёт пакет в очередь; разбор и вызовы автомата —
 * в tick() из loop().
 */
class EspNowLink {
public:
    bool begin(WsProtocol* proto, StateMachine* sm);
    void tick(uint32_t nowMs);
    void requestState() { m_statePending = true; }   // состояние поменялось — отправить пораньше
    bool remoteOnline(uint32_t nowMs) const;

private:
    static void onRecv(uint8_t* mac, uint8_t* data, uint8_t len);
    static void onSent(uint8_t* mac, uint8_t status);
    void handle(const uint8_t* mac, const uint8_t* data, uint8_t len, uint32_t nowMs);
    bool dispatch(const uint8_t* data);
    void sendState(uint32_t nowMs);
    void sendAck(uint8_t seq, uint8_t type, bool ok);
    void setPeer(const uint8_t* mac);

    WsProtocol*   m_proto = nullptr;
    StateMachine* m_sm    = nullptr;
    bool     m_ok          = false;
    uint8_t  m_peer[6]     = {0};
    bool     m_hasPeer     = false;
    uint32_t m_lastRxMs    = 0;
    uint32_t m_lastStateMs = 0;
    bool     m_statePending = false;
    uint8_t  m_txSeq       = 0;
    uint8_t  m_helloEcho   = 0;     // seq последнего HELLO — уходит обратно в STATE
    uint16_t m_boot        = 0;     // наш номер загрузки
    uint16_t m_remoteBoot  = 0;     // номер загрузки пульта; сменился — забываем m_seen
    bool     m_wasOnline   = false;
    // Повтор того же пакета (пульт переслал, не дождавшись ACK) — не исполняем второй
    // раз, отвечаем тем же ACK. Помним несколько последних: запоздавший повтор старой
    // команды может прийти уже после следующей.
    struct Seen { uint8_t seq, type; bool ok; uint32_t ms; };
    static constexpr uint8_t SEEN_N = 8;
    Seen     m_seen[SEEN_N] = {};
    uint8_t  m_seenNext    = 0;
};
