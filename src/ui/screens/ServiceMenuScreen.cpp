#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

void renderServiceMenu(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);

    // 6 items: 0=Шаг, 1=Шприцы, 2=Сон, 3=Шрифт, 4=Кольцо (toggle), 5=Назад
    constexpr int TOTAL = 6;
    int page  = s.cursorIndex / ITEMS_PER_PAGE;
    int pages = (TOTAL + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int first = page * ITEMS_PER_PAGE;

    char buf[28];

    for (int row = 0; row < ITEMS_PER_PAGE; row++) {
        int item = first + row;
        if (item >= TOTAL) break;
        uint8_t y = PAGE_ROWS[row];

        switch (item) {
            case 0: snprintf(buf, sizeof(buf), "Шаг винта  %.1f мм", s.screwPitch);    break;
            case 1: snprintf(buf, sizeof(buf), "Шприцы  (%u)", s.presetsCount);         break;
            case 2: snprintf(buf, sizeof(buf), "Сон экрана %u с", s.sleepTimeoutSec);   break;
            case 3: snprintf(buf, sizeof(buf), "Шрифт");                                break;
            case 4: snprintf(buf, sizeof(buf), "Кольцо: %s", s.circularNav ? "Вкл" : "Выкл"); break;
            case 5: snprintf(buf, sizeof(buf), "Назад");                                break;
            default: break;
        }

        if (s.cursorIndex == item) drawCursor(oled, y);
        oled.drawUTF8(8, y, buf);
    }

    drawPageIndicator(oled, page + 1, pages);
}
