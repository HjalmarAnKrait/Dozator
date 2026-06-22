#pragma once
#include <ESPAsyncWebServer.h>
#include "../app/AppState.h"
#include "../input/EncoderInput.h"
#include "../hardware/ILimitSwitches.h"

class Settings;
class StateMachine;

/** Parses incoming WebSocket frames and dispatches to encoder/state/settings. */
class WsProtocol {
public:
    void begin(EncoderInput* enc, ILimitSwitches* sw, Settings* settings, StateMachine* sm);
    void handleMessage(AsyncWebSocketClient* client, const char* data, size_t len);

    // Build full state JSON into the supplied buffer. Returns bytes written.
    static size_t buildStateJson(char* buf, size_t bufLen);

private:
    EncoderInput*   m_enc      = nullptr;
    ILimitSwitches* m_switches = nullptr;
    Settings*       m_settings = nullptr;
    StateMachine*   m_sm       = nullptr;

    void handleEncoder(const char* action);
    void handleDebugSwitch(const char* sw, bool state);
    void handleDirectSet(const char* field, float value, AsyncWebSocketClient* client);
    void handleCommand(const char* action);
};
