#pragma once
#include <ESPAsyncWebServer.h>
#include "../app/AppState.h"
#include "../hardware/ILimitSwitches.h"

class Settings;
class StateMachine;

/** Parses incoming WebSocket frames and dispatches to state/settings. */
class WsProtocol {
public:
    void begin(ILimitSwitches* sw, Settings* settings, StateMachine* sm);
    void handleMessage(AsyncWebSocketClient* client, const char* data, size_t len);

    // Build full state JSON into the supplied buffer. Returns bytes written.
    static size_t buildStateJson(char* buf, size_t bufLen);

private:
    ILimitSwitches* m_switches = nullptr;
    Settings*       m_settings = nullptr;
    StateMachine*   m_sm       = nullptr;

    void handleDebugSwitch(const char* sw, bool state);
    void handleDirectSet(const char* field, float value, AsyncWebSocketClient* client);
    void handleSetPreset(int index, float vol, float diam);
    void handleCommand(const char* action);
};
