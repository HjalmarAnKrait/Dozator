#pragma once
#include <ESPAsyncWebServer.h>
#include "../app/AppState.h"

class WsBroadcaster {
public:
    void begin(AsyncWebSocket* ws);
    void requestBroadcast();
    void tick(uint32_t nowMs);
    void broadcastNow();                          // immediate (on new client connect)
    void sendTo(AsyncWebSocketClient* client);    // on connect: send to single client

private:
    AsyncWebSocket* m_ws         = nullptr;
    bool            m_pending    = false;
    uint32_t        m_lastSentMs = 0;

    static constexpr size_t BUF_SIZE = 1200;
    char m_buf[BUF_SIZE];
};
