#pragma once
#include <ESPAsyncWebServer.h>
#include "WsProtocol.h"
#include "WsBroadcaster.h"

class Settings;
class StateMachine;
class ILimitSwitches;

class WebServer {
public:
    void begin(ILimitSwitches* sw, Settings* settings, StateMachine* sm);
    WsBroadcaster& broadcaster() { return m_broadcaster; }

private:
    AsyncWebServer  m_server{80};
    AsyncWebSocket  m_ws{"/ws"};
    WsProtocol      m_protocol;
    WsBroadcaster   m_broadcaster;
};
