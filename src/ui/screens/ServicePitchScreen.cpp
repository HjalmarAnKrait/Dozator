#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

void renderServicePitch(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);
    drawCentred(oled, ROW0, "ШАГ ВИНТА");

    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f мм", s.screwPitch);
    oled.setFont(FONT_LARGE);
    if (blinkVisible(millis())) {
        drawCentred(oled, 36, buf);
    } else {
        // Draw box then invert (blinking highlight)
        int w = oled.getUTF8Width(buf);
        oled.drawBox((128 - w) / 2 - 1, 36 - 16, w + 2, 18);
        oled.setDrawColor(0);
        drawCentred(oled, 36, buf);
        oled.setDrawColor(1);
    }

    oled.setFont(FONT_SMALL);
    drawCentred(oled, 48, "диапазон 0.5 - 10 мм");
    drawCentred(oled, 62, "^ изм  |  <- назад");
}
