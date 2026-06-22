#include "IScreen.h"
#include "ScreenHelper.h"
#include "../../app/Calculations.h"
#include <stdio.h>

static void fmtTime(char* buf, size_t sz, float sec) {
    int m = (int)(sec / 60);
    int s = (int)(sec) % 60;
    snprintf(buf, sz, "%d:%02d", m, s);
}

void renderDosing(U8G2& oled, const AppState& s) {
    char buf[32];

    // Row 0 – label + percent right-aligned
    oled.setFont(FONT_MAIN);
    oled.drawUTF8(0, ROW0, "ДОЗИРОВАНИЕ");
    int pct = (int)(s.dosing.progress * 100.0f + 0.5f);
    snprintf(buf, sizeof(buf), "%d%%", pct);
    int pw = oled.getUTF8Width(buf);
    oled.drawUTF8(128 - pw, ROW0, buf);

    // Progress bar: y=14..22 (8px height between rows 0 and 1)
    oled.drawFrame(0, 14, 128, 8);
    int filled = (int)(124 * s.dosing.progress);
    if (filled > 0) oled.drawBox(2, 16, filled, 4);

    // Row 1 – elapsed → remaining (centered)
    char el[10], rem[10];
    fmtTime(el,  sizeof(el),  s.dosing.elapsedSec);
    fmtTime(rem, sizeof(rem), max(0.0f, s.dosing.totalSec - s.dosing.elapsedSec));
    snprintf(buf, sizeof(buf), "%s -> %s", el, rem);
    drawCentred(oled, ROW1, buf);

    // Row 2 – volume A
    snprintf(buf, sizeof(buf), "A: %.1f / %.0f мл", s.dosing.volumeA, calc::volA());
    oled.drawUTF8(0, ROW2, buf);

    // Row 3 – volume B
    snprintf(buf, sizeof(buf), "B: %.1f / %.0f мл", s.dosing.volumeB, calc::volB());
    oled.drawUTF8(0, ROW3, buf);

    // Row 4 – hint
    oled.setFont(FONT_SMALL);
    drawCentred(oled, ROW4, "Long = СТОП");
    oled.setFont(FONT_MAIN);
}
