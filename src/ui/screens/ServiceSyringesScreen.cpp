#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

void renderServiceSyringes(U8G2& oled, const AppState& s) {
    // Items: presets[0..presetsCount-1] + "+ Новый" + "Назад"
    int total = s.presetsCount + 2;
    int page  = s.cursorIndex / ITEMS_PER_PAGE;
    int pages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int first = page * ITEMS_PER_PAGE;

    // Title with FONT_SMALL to save space, then separator line
    char title[24];
    snprintf(title, sizeof(title), "ШПРИЦЫ (%u)", s.presetsCount);
    oled.setFont(FONT_SMALL);
    int tw = oled.getUTF8Width(title);
    oled.drawUTF8((128 - tw) / 2, 6, title);
    oled.drawHLine(0, 8, 128);

    // 4 content rows below separator: baselines at 18, 30, 42, 54
    const uint8_t CY[4] = {18, 30, 42, 54};
    oled.setFont(FONT_MAIN);
    char buf[28];

    for (int row = 0; row < ITEMS_PER_PAGE; row++) {
        int item = first + row;
        if (item >= total) break;
        uint8_t y = CY[row];

        if (item < s.presetsCount) {
            snprintf(buf, sizeof(buf), "%d: %.0fмл D%.1fмм",
                     item + 1, s.presets[item].vol, s.presets[item].diam);
        } else if (item == s.presetsCount) {
            snprintf(buf, sizeof(buf), "+ Новый");
        } else {
            snprintf(buf, sizeof(buf), "Назад");
        }

        if (s.cursorIndex == item) drawCursor(oled, y);
        oled.drawUTF8(8, y, buf);
    }

    drawPageIndicator(oled, page + 1, pages);
}
