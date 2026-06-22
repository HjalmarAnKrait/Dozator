#include "IScreen.h"
#include "ScreenHelper.h"
#include "../../app/Calculations.h"
#include <stdio.h>

void renderParked(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);

    // 6 cursor items total → 2 pages of 4 + 2
    // 0: A Объём, 1: A D, 2: B Объём, 3: B D, 4: Старт зарядки, 5: Сервис
    constexpr uint8_t TOTAL = 6;
    uint8_t pages = (TOTAL + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    uint8_t page  = s.cursorIndex / ITEMS_PER_PAGE;
    uint8_t first = page * ITEMS_PER_PAGE;

    char buf[24];

    for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
        uint8_t item = first + row;
        if (item >= TOTAL) break;
        uint8_t y = PAGE_ROWS[row];

        switch (item) {
            case 0: {
                float v = (s.syringeA.presetIdx >= 0) ? s.presets[s.syringeA.presetIdx].vol : 0.0f;
                snprintf(buf, sizeof(buf), "A Объём: %.0f мл", v);
                break;
            }
            case 1:
                snprintf(buf, sizeof(buf), "A D: %.1f мм", s.syringeA.diameter);
                break;
            case 2: {
                float v = (s.syringeB.presetIdx >= 0) ? s.presets[s.syringeB.presetIdx].vol : 0.0f;
                snprintf(buf, sizeof(buf), "B Объём: %.0f мл", v);
                break;
            }
            case 3:
                snprintf(buf, sizeof(buf), "B D: %.1f мм", s.syringeB.diameter);
                break;
            case 4:
                snprintf(buf, sizeof(buf), "Старт зарядки");
                break;
            case 5:
                snprintf(buf, sizeof(buf), "> Сервис");
                break;
            default: break;
        }

        bool isCursor = (s.cursorIndex == item);
        if (isCursor) drawCursor(oled, y);

        bool editable = (item <= 3);
        if (isCursor && s.editing && editable) {
            if (blinkVisible(millis())) drawSelectedRow(oled, 8, y, buf);
            else                        oled.drawUTF8(8, y, buf);
        } else {
            oled.drawUTF8(8, y, buf);
        }
    }

    drawPageIndicator(oled, page + 1, pages);
}
