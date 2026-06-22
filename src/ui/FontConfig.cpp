#include "FontConfig.h"

// 4 font tiers in ascending size.
// rows[] = baseline Y positions for up to 8 items on the 128x64 display.
// rowH   = approximate ascent (used for cursor box height).
// ITEMS_PER_PAGE leaves room for the "X/Y" page indicator at y=63 (FONT_SMALL).

const FontTier g_fontTiers[4] = {
    // ── 0: XS ──────────────────────────────────────────────────────────────
    // 4x6 px: very compact, 7 items fit with room for indicator
    {
        u8g2_font_4x6_t_cyrillic,
        "XS 4x6",
        /*charW*/ 4, /*rowH*/ 5, /*itemsPerPage*/ 7,
        {5, 12, 19, 26, 33, 40, 47, 54}
    },
    // ── 1: S (default) ─────────────────────────────────────────────────────
    // 6x13 px: current default, 4 content items + indicator fit
    {
        u8g2_font_6x13_t_cyrillic,
        "S 6x13",
        /*charW*/ 6, /*rowH*/ 10, /*itemsPerPage*/ 4,
        {10, 22, 34, 46, 58, 58, 58, 58}
    },
    // ── 2: M ───────────────────────────────────────────────────────────────
    // 7x13 px: slightly wider, same vertical fit as S
    {
        u8g2_font_7x13_t_cyrillic,
        "M 7x13",
        /*charW*/ 7, /*rowH*/ 10, /*itemsPerPage*/ 4,
        {11, 24, 37, 50, 63, 63, 63, 63}
    },
    // ── 3: L ───────────────────────────────────────────────────────────────
    // 9x15 px: large, 3 content items per page; ROW4 may clip 2-3px on fixed screens
    {
        u8g2_font_9x15_t_cyrillic,
        "L 9x15",
        /*charW*/ 9, /*rowH*/ 12, /*itemsPerPage*/ 3,
        {13, 28, 43, 58, 63, 63, 63, 63}
    },
};
