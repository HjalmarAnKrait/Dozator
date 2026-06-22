#pragma once
#include <Arduino.h>
#include "../app/AppState.h"

class Settings {
public:
    bool load();       // Read from LittleFS; writes defaults if file missing/corrupt
    bool save();       // Write to LittleFS immediately
    void markDirty();  // Schedule a deferred save
    void tick(uint32_t nowMs);  // Flush after SETTINGS_SAVE_DELAY_MS debounce

private:
    bool     m_dirty     = false;
    uint32_t m_dirtyAtMs = 0;
};
