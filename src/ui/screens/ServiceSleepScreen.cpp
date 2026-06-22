#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

void renderServiceSleep(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);
    drawCentred(oled, ROW0, "СОН ЭКРАНА");

    char buf[16];
    snprintf(buf, sizeof(buf), "%u с", s.sleepTimeoutSec);
    oled.setFont(FONT_LARGE);
    if (blinkVisible(millis())) {
        drawCentred(oled, 36, buf);
    } else {
        int w = oled.getUTF8Width(buf);
        oled.drawBox((128 - w) / 2 - 1, 36 - 16, w + 2, 18);
        oled.setDrawColor(0);
        drawCentred(oled, 36, buf);
        oled.setDrawColor(1);
    }

    oled.setFont(FONT_SMALL);
    drawCentred(oled, 48, "диапазон 5 - 300 с");
    drawCentred(oled, 62, "^ изм  |  <- назад");
}
