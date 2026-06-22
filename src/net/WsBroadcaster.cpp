#include "WsBroadcaster.h"
#include "WsProtocol.h"
#include "config.h"

void WsBroadcaster::begin(AsyncWebSocket* ws) {
    m_ws = ws;
}

void WsBroadcaster::requestBroadcast() {
    m_pending = true;
}

void WsBroadcaster::broadcastNow() {
    if (!m_ws) return;
    size_t n = WsProtocol::buildStateJson(m_buf, BUF_SIZE);
    if (n > 0) m_ws->textAll(m_buf, n);
    m_pending    = false;
    m_lastSentMs = millis();
}

void WsBroadcaster::sendTo(AsyncWebSocketClient* client) {
    size_t n = WsProtocol::buildStateJson(m_buf, BUF_SIZE);
    if (n > 0) client->text(m_buf, n);
}

void WsBroadcaster::tick(uint32_t nowMs) {
    bool throttleOk = (nowMs - m_lastSentMs) >= WS_THROTTLE_MS;
    bool heartbeat  = (nowMs - m_lastSentMs) >= WS_HEARTBEAT_MS;

    if ((m_pending && throttleOk) || heartbeat) {
        broadcastNow();
    }
}
