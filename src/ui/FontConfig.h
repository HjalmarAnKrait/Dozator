#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "../app/AppState.h"

struct FontTier {
    const uint8_t* mainFont;
    const char*    name;
    uint8_t        charW;       // nominal glyph width (px)
    uint8_t        rowH;        // ascent in px (for cursor/box sizing)
    uint8_t        itemsPerPage;
    uint8_t        rows[8];     // baseline Y positions, indexed 0..7
};

// Defined in FontConfig.cpp
extern const FontTier g_fontTiers[4];

inline const FontTier& activeTier() {
    uint8_t i = (g_state.fontIndex < 4) ? g_state.fontIndex : DEFAULT_FONT_INDEX;
    return g_fontTiers[i];
}
