#include "IScreen.h"
#include "ScreenHelper.h"
#include "../../app/Calculations.h"
#include <stdio.h>

void renderCharged(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);
    char buf[32];

    // Row 0 – editable dose time (cursor 0)
    snprintf(buf, sizeof(buf), "Время: %.1f мин", s.doseTimeMin);
    if (s.cursorIndex == 0) {
        drawCursor(oled, ROW0);
        if (s.editing) {
            if (blinkVisible(millis())) drawSelectedRow(oled, 8, ROW0, buf);
            else                        oled.drawUTF8(8, ROW0, buf);
        } else {
            oled.drawUTF8(8, ROW0, buf);
        }
    } else {
        oled.drawUTF8(8, ROW0, buf);
    }

    // Range hint in FONT_SMALL between time and flows
    auto tr = calc::timeRangeMin();
    snprintf(buf, sizeof(buf), "(%.1f – %.1f мин)", tr.minMin, tr.maxMin);
    oled.setFont(FONT_SMALL);
    oled.drawUTF8(8, ROW0 + 8, buf);
    oled.setFont(FONT_MAIN);

    // Row 1 – flow A
    snprintf(buf, sizeof(buf), "A: %.2f мл/мин", calc::flowA());
    oled.drawUTF8(0, ROW2, buf);

    // Row 2 – flow B
    snprintf(buf, sizeof(buf), "B: %.2f мл/мин", calc::flowB());
    oled.drawUTF8(0, ROW3, buf);

    // Row 4 – ПУСК button (cursor 1)
    const char* pusk = "[ ПУСК ]";
    int pw = oled.getUTF8Width(pusk);
    int px = (128 - pw) / 2;
    if (s.cursorIndex == 1) {
        oled.drawBox(px - 1, ROW4 - ROW_H, pw + 2, ROW_H + 2);
        oled.setDrawColor(0);
        oled.drawUTF8(px, ROW4, pusk);
        oled.setDrawColor(1);
    } else {
        oled.drawFrame(px - 1, ROW4 - ROW_H, pw + 2, ROW_H + 2);
        oled.drawUTF8(px, ROW4, pusk);
    }
}
