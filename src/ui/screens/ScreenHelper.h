#pragma once
#include <U8g2lib.h>
#include "../Fonts.h"
#include "../../app/AppState.h"

// Items visible per page — depends on active font tier
#define ITEMS_PER_PAGE  (activeTier().itemsPerPage)
// Row baseline array for paginated screens
#define PAGE_ROWS       (activeTier().rows)

// ─── Cursor marker ────────────────────────────────────────────────────────────
inline void drawCursor(U8G2& oled, uint8_t y) {
    uint8_t h = ROW_H;
    oled.drawBox(0, y - h + 1, 3, h - 1);
}

// ─── Centred text ─────────────────────────────────────────────────────────────
inline void drawCentred(U8G2& oled, uint8_t y, const char* str) {
    int w = oled.getUTF8Width(str);
    oled.drawUTF8((128 - w) / 2, y, str);
}

// ─── Selected/highlighted row (inverted) ─────────────────────────────────────
inline void drawSelectedRow(U8G2& oled, uint8_t x, uint8_t y, const char* str) {
    uint8_t h = ROW_H;
    int w = oled.getUTF8Width(str);
    oled.setDrawColor(1);
    oled.drawBox(x, y - h + 1, w + 2, h + 1);
    oled.setDrawColor(0);
    oled.drawUTF8(x + 1, y, str);
    oled.setDrawColor(1);
}

// ─── Blink helper ─────────────────────────────────────────────────────────────
inline bool blinkVisible(uint32_t nowMs) {
    return (nowMs % 1000) < 500;
}

// ─── Page indicator ("X/Y") at bottom-right in FONT_SMALL ────────────────────
inline void drawPageIndicator(U8G2& oled, uint8_t page1, uint8_t totalPages) {
    if (totalPages <= 1) return;
    char buf[6];
    snprintf(buf, sizeof(buf), "%u/%u", page1, totalPages);
    oled.setFont(FONT_SMALL);
    int w = oled.getUTF8Width(buf);
    oled.drawUTF8(128 - w, 63, buf);
    oled.setFont(FONT_MAIN);
}
