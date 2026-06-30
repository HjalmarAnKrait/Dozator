#include "WsProtocol.h"
#include "../app/StateMachine.h"
#include "../app/Calculations.h"
#include "../storage/Settings.h"
#include "../util/Logger.h"
#include "version.h"
#include <ArduinoJson.h>
#include <string.h>

void WsProtocol::begin(ILimitSwitches* sw, Settings* settings, StateMachine* sm) {
    m_switches = sw;
    m_settings = settings;
    m_sm       = sm;
}

void WsProtocol::handleMessage(AsyncWebSocketClient* client, const char* data, size_t len) {
    JsonDocument doc;
    auto err = deserializeJson(doc, data, len);
    if (err) { LOG_WARN("WS: bad JSON"); return; }

    const char* type = doc["type"] | "";

    if (strcmp(type, "debug_switch") == 0) {
        handleDebugSwitch(doc["switch"] | "", doc["state"] | false);
    } else if (strcmp(type, "direct_set") == 0) {
        handleDirectSet(doc["field"] | "", doc["value"] | 0.0f, client);
    } else if (strcmp(type, "set_preset") == 0) {
        handleSetPreset(doc["index"] | -1, doc["vol"] | 0.0f, doc["diam"] | 0.0f);
    } else if (strcmp(type, "command") == 0) {
        handleCommand(doc["action"] | "");
    }
}

void WsProtocol::handleDebugSwitch(const char* sw, bool state) {
    if (!m_switches) return;
    uint8_t idx = 255;
    if      (strcmp(sw, "top") == 0) idx = 0;
    else if (strcmp(sw, "a")   == 0) idx = 1;
    else if (strcmp(sw, "b")   == 0) idx = 2;
    else if (strcmp(sw, "bot") == 0) idx = 3;
    if (idx != 255) m_switches->debugSetSwitch(idx, state);
}

void WsProtocol::handleDirectSet(const char* field, float value, AsyncWebSocketClient* client) {
    bool ok = true;
    if (strcmp(field, "doseTimeMin") == 0 && g_state.screen == Screen::CHARGED) {
        g_state.doseTimeMin = max(0.1f, value);
    } else if (strcmp(field, "syringeA.presetIdx") == 0 && g_state.screen == Screen::PARKED) {
        int idx = (int)value;
        if (idx >= 0 && idx < g_state.presetsCount) {
            g_state.syringeA.presetIdx = idx;
            g_state.syringeA.diameter  = g_state.presets[idx].diam;
        }
    } else if (strcmp(field, "syringeA.diameter") == 0 && g_state.screen == Screen::PARKED) {
        g_state.syringeA.diameter = constrain(value, 1.0f, 150.0f);
    } else if (strcmp(field, "syringeB.presetIdx") == 0 && g_state.screen == Screen::PARKED) {
        int idx = (int)value;
        if (idx >= 0 && idx < g_state.presetsCount) {
            g_state.syringeB.presetIdx = idx;
            g_state.syringeB.diameter  = g_state.presets[idx].diam;
        }
    } else if (strcmp(field, "syringeB.diameter") == 0 && g_state.screen == Screen::PARKED) {
        g_state.syringeB.diameter = constrain(value, 1.0f, 150.0f);
    } else if (strcmp(field, "screwPitch") == 0) {
        g_state.screwPitch = constrain(value, 0.5f, 10.0f);
        m_settings->markDirty();
    } else if (strcmp(field, "sleepTimeout") == 0) {
        g_state.sleepTimeoutSec = (uint16_t)constrain(value, 5.0f, 300.0f);
        m_settings->markDirty();
    } else if (strcmp(field, "parkSpeed") == 0) {
        g_state.parkSpeed = constrain(value, 50.0f, 3000.0f);
        m_settings->markDirty();
    } else if (strcmp(field, "chargeSpeed") == 0) {
        g_state.chargeSpeed = constrain(value, 50.0f, 3000.0f);
        m_settings->markDirty();
    } else {
        ok = false;
    }

    if (!ok && client) {
        char errBuf[64];
        snprintf(errBuf, sizeof(errBuf), "{\"type\":\"error\",\"msg\":\"field not allowed\"}");
        client->text(errBuf);
        return;
    }
    if (m_sm) m_sm->transitionTo(g_state.screen);  // re-broadcast state
}

void WsProtocol::handleSetPreset(int index, float vol, float diam) {
    if (index < 0 || index >= g_state.presetsCount) return;
    g_state.presets[index].vol  = constrain(vol,  1.0f, 500.0f);
    g_state.presets[index].diam = constrain(diam, 1.0f, 150.0f);
    // Если выбранный шприц ссылается на этот пресет — синхронизируем его диаметр.
    if (g_state.syringeA.presetIdx == index) g_state.syringeA.diameter = g_state.presets[index].diam;
    if (g_state.syringeB.presetIdx == index) g_state.syringeB.diameter = g_state.presets[index].diam;
    if (m_settings) m_settings->markDirty();
    if (m_sm) m_sm->requestBroadcast();
}

void WsProtocol::handleCommand(const char* action) {
    if (!m_sm) return;
    // Сброс: из ЛЮБОГО состояния — остановиться и уехать на парковку (хоуминг).
    if (strcmp(action, "reset") == 0) {
        m_sm->transitionTo(Screen::PARKING);
        return;
    }
    // Парковка запускается только по команде пользователя (из простоя/после цикла).
    if      (strcmp(action, "park") == 0 &&
             (g_state.screen == Screen::IDLE || g_state.screen == Screen::DONE))
        m_sm->transitionTo(Screen::PARKING);
    else if (strcmp(action, "start_charging") == 0 && g_state.screen == Screen::PARKED)
        m_sm->transitionTo(Screen::CHARGING);
    else if (strcmp(action, "pusk") == 0 && g_state.screen == Screen::CHARGED) {
        m_sm->transitionTo(Screen::DOSING);
    } else if (strcmp(action, "abort") == 0 && g_state.screen == Screen::DOSING) {
        m_sm->transitionTo(Screen::DONE);
    } else if (strcmp(action, "new_cycle") == 0 && g_state.screen == Screen::DONE) {
        m_sm->transitionTo(Screen::IDLE);   // без автопарковки — ждём команду «Парковка»
    }
}

static const char* screenName(Screen s) {
    switch (s) {
        case Screen::IDLE:                 return "IDLE";
        case Screen::PARKING:              return "PARKING";
        case Screen::PARKED:               return "PARKED";
        case Screen::CHARGING:             return "CHARGING";
        case Screen::CHARGED:              return "CHARGED";
        case Screen::DOSING:               return "DOSING";
        case Screen::DONE:                 return "DONE";
        case Screen::SERVICE_MENU:         return "SERVICE_MENU";
        case Screen::SERVICE_PITCH:        return "SERVICE_PITCH";
        case Screen::SERVICE_SLEEP:        return "SERVICE_SLEEP";
        case Screen::SERVICE_SYRINGES:     return "SERVICE_SYRINGES";
        case Screen::SERVICE_SYRINGE_EDIT: return "SERVICE_SYRINGE_EDIT";
        default:                           return "UNKNOWN";
    }
}

size_t WsProtocol::buildStateJson(char* buf, size_t bufLen) {
    JsonDocument doc;
    doc["type"]    = "state";
    doc["screen"]  = screenName(g_state.screen);
    doc["cursor"]  = g_state.cursorIndex;
    doc["editing"] = g_state.editing;

    JsonObject settings = doc["settings"].to<JsonObject>();
    settings["screwPitch"]   = g_state.screwPitch;
    settings["sleepTimeout"] = g_state.sleepTimeoutSec;
    settings["parkSpeed"]    = g_state.parkSpeed;
    settings["chargeSpeed"]  = g_state.chargeSpeed;
    JsonArray presets = settings["presets"].to<JsonArray>();
    for (int i = 0; i < g_state.presetsCount; i++) {
        JsonObject p = presets.add<JsonObject>();
        p["vol"]  = g_state.presets[i].vol;
        p["diam"] = g_state.presets[i].diam;
    }

    JsonObject cycle = doc["cycle"].to<JsonObject>();
    JsonObject sA = cycle["syringeA"].to<JsonObject>();
    sA["presetIdx"] = g_state.syringeA.presetIdx;
    sA["diameter"]  = g_state.syringeA.diameter;
    int aIdx = g_state.syringeA.presetIdx;
    sA["volume"] = (aIdx >= 0 && aIdx < g_state.presetsCount) ? g_state.presets[aIdx].vol : 0.0f;
    JsonObject sB = cycle["syringeB"].to<JsonObject>();
    sB["presetIdx"] = g_state.syringeB.presetIdx;
    sB["diameter"]  = g_state.syringeB.diameter;
    int bIdx = g_state.syringeB.presetIdx;
    sB["volume"] = (bIdx >= 0 && bIdx < g_state.presetsCount) ? g_state.presets[bIdx].vol : 0.0f;
    cycle["doseTimeMin"] = g_state.doseTimeMin;
    cycle["flowA"]       = calc::flowA();
    cycle["flowB"]       = calc::flowB();
    // Физически допустимый диапазон времени дозирования (мин) под текущие
    // объём/диаметр/шаг — пределы потока и оборотов мотора.
    calc::TimeRange tr   = calc::timeRangeMin();
    cycle["timeMin"]     = tr.minMin;
    cycle["timeMax"]     = tr.maxMin;

    JsonObject sw = doc["switches"].to<JsonObject>();
    sw["top"] = g_state.switches.top;
    sw["a"]   = g_state.switches.a;
    sw["b"]   = g_state.switches.b;
    sw["bot"] = g_state.switches.bot;

    // Живые показания концевиков (для постоянной отладочной индикации в UI).
    JsonObject rsw = doc["rawSwitches"].to<JsonObject>();
    rsw["top"] = g_state.rawSwitches.top;
    rsw["a"]   = g_state.rawSwitches.a;
    rsw["b"]   = g_state.rawSwitches.b;
    rsw["bot"] = g_state.rawSwitches.bot;

    JsonObject dos = doc["dosing"].to<JsonObject>();
    dos["elapsedSec"] = g_state.dosing.elapsedSec;
    dos["totalSec"]   = g_state.dosing.totalSec;
    dos["progress"]   = g_state.dosing.progress;
    dos["volumeA"]    = g_state.dosing.volumeA;
    dos["volumeB"]    = g_state.dosing.volumeB;

    JsonObject ui = doc["ui"].to<JsonObject>();
    ui["displaySleeping"]  = g_state.displaySleeping;
    ui["editingPresetIdx"] = g_state.editingPresetIdx;

    JsonArray bc = ui["breadcrumb"].to<JsonArray>();
    for (int i = 0; i < g_state.breadcrumbDepth; i++)
        bc.add(screenName(g_state.breadcrumb[i].screen));

    return serializeJson(doc, buf, bufLen);
}
