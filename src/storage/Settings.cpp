#include "Settings.h"
#include "config.h"
#include "../util/Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static const char* SETTINGS_PATH = "/settings.json";

bool Settings::load() {
    if (!LittleFS.exists(SETTINGS_PATH)) {
        LOG_INFO("Settings: no file, using defaults");
        initDefaultPresets(g_state);
        save();
        return false;
    }

    File f = LittleFS.open(SETTINGS_PATH, "r");
    if (!f) {
        LOG_ERROR("Settings: open failed");
        initDefaultPresets(g_state);
        return false;
    }

    JsonDocument doc;
    auto err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOG_ERROR("Settings: JSON parse error: %s", err.c_str());
        initDefaultPresets(g_state);
        save();
        return false;
    }

    g_state.screwPitch      = doc["screwPitch"]    | 2.0f;
    g_state.sleepTimeoutSec = doc["sleepTimeout"]  | (uint16_t)DEFAULT_SLEEP_SEC;
    g_state.fontIndex       = doc["fontIndex"]     | (uint8_t)DEFAULT_FONT_INDEX;
    if (g_state.fontIndex >= 4) g_state.fontIndex = DEFAULT_FONT_INDEX;
    g_state.circularNav     = doc["circularNav"]   | true;

    JsonArray presets = doc["presets"].as<JsonArray>();
    if (presets) {
        g_state.presetsCount = 0;
        for (JsonObject p : presets) {
            if (g_state.presetsCount >= MAX_PRESETS) break;
            g_state.presets[g_state.presetsCount].vol  = p["vol"]  | 10.0f;
            g_state.presets[g_state.presetsCount].diam = p["diam"] | 14.5f;
            g_state.presetsCount++;
        }
        if (g_state.presetsCount == 0) initDefaultPresets(g_state);
    } else {
        initDefaultPresets(g_state);
    }

    // Clamp syringe preset indices
    if (g_state.syringeA.presetIdx >= g_state.presetsCount) g_state.syringeA.presetIdx = 0;
    if (g_state.syringeB.presetIdx >= g_state.presetsCount) g_state.syringeB.presetIdx = 0;

    LOG_INFO("Settings loaded: pitch=%.1f sleep=%us presets=%u",
             g_state.screwPitch, g_state.sleepTimeoutSec, g_state.presetsCount);
    return true;
}

bool Settings::save() {
    File f = LittleFS.open(SETTINGS_PATH, "w");
    if (!f) {
        LOG_ERROR("Settings: write failed");
        return false;
    }

    JsonDocument doc;
    doc["version"]      = 1;
    doc["screwPitch"]   = g_state.screwPitch;
    doc["sleepTimeout"] = g_state.sleepTimeoutSec;
    doc["fontIndex"]    = g_state.fontIndex;
    doc["circularNav"]  = g_state.circularNav;

    JsonArray arr = doc["presets"].to<JsonArray>();
    for (int i = 0; i < g_state.presetsCount; i++) {
        JsonObject p = arr.add<JsonObject>();
        p["vol"]  = g_state.presets[i].vol;
        p["diam"] = g_state.presets[i].diam;
    }

    serializeJson(doc, f);
    f.close();
    LOG_INFO("Settings saved");
    return true;
}

void Settings::markDirty() {
    m_dirty     = true;
    m_dirtyAtMs = millis();
}

void Settings::tick(uint32_t nowMs) {
    if (m_dirty && (nowMs - m_dirtyAtMs) >= SETTINGS_SAVE_DELAY_MS) {
        m_dirty = false;
        save();
    }
}
