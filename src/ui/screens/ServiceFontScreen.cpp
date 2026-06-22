#include "IScreen.h"
#include "ScreenHelper.h"
#include "../FontConfig.h"
#include <stdio.h>

void renderServiceFont(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);

    // 5 items: font 0..3 + Назад
    constexpr int TOTAL = 5;
    int page  = s.cursorIndex / ITEMS_PER_PAGE;
    int pages = (TOTAL + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int first = page * ITEMS_PER_PAGE;

    char buf[24];

    for (int row = 0; row < ITEMS_PER_PAGE; row++) {
        int item = first + row;
        if (item >= TOTAL) break;
        uint8_t y = PAGE_ROWS[row];

        if (item < 4) {
            snprintf(buf, sizeof(buf), "[%c] %s",
                     (item == (int)s.fontIndex) ? '*' : ' ',
                     g_fontTiers[item].name);
        } else {
            snprintf(buf, sizeof(buf), "Назад");
        }

        if (s.cursorIndex == item) drawCursor(oled, y);
        oled.drawUTF8(8, y, buf);
    }

    drawPageIndicator(oled, page + 1, pages);
}
