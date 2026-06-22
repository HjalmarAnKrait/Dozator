#pragma once
#include <U8g2lib.h>
#include "FontConfig.h"

// Dynamic: all FONT_MAIN / ROW / CHAR_W references resolve to the active tier at runtime.
// Changing g_state.fontIndex switches the tier; all screen renderers pick it up automatically.

#define FONT_MAIN       (activeTier().mainFont)
#define CHAR_W          (activeTier().charW)
#define ROW_H           (activeTier().rowH)

// Row baselines indexed into the active tier
#define ROW0  (activeTier().rows[0])
#define ROW1  (activeTier().rows[1])
#define ROW2  (activeTier().rows[2])
#define ROW3  (activeTier().rows[3])
#define ROW4  (activeTier().rows[4])
#define ROW5  (activeTier().rows[5])
#define ROW6  (activeTier().rows[6])

// Fixed small / large fonts (not user-selectable)
#define FONT_LARGE  u8g2_font_10x20_t_cyrillic
#define FONT_SMALL  u8g2_font_4x6_t_cyrillic
