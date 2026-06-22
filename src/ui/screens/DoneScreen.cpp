#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

static void fmtTime(char* buf, size_t sz, float sec) {
    int m = (int)(sec / 60);
    int s = (int)(sec) % 60;
    snprintf(buf, sz, "%d:%02d", m, s);
}

void renderDone(U8G2& oled, const AppState& s) {
    // ── Inverted status bar at very top ───────────────────────────────────────
    // Occupies y=0..12 (13px). Leaves ROW1..ROW4 for stats + button.
    oled.setDrawColor(1);
    oled.drawBox(0, 0, 128, 13);
    oled.setDrawColor(0);
    const char* hdr = "ВЫПОЛНЕНО";
    oled.setFont(FONT_MAIN);
    int hw = oled.getUTF8Width(hdr);
    oled.drawUTF8((128 - hw) / 2, 11, hdr);
    oled.setDrawColor(1);

    // ── Stats in ROW1–ROW3 ────────────────────────────────────────────────────
    char buf[28];
    snprintf(buf, sizeof(buf), "A: %.1f мл", s.dosing.volumeA);
    oled.drawUTF8(0, ROW1, buf);

    snprintf(buf, sizeof(buf), "B: %.1f мл", s.dosing.volumeB);
    oled.drawUTF8(0, ROW2, buf);

    char timebuf[10];
    fmtTime(timebuf, sizeof(timebuf), s.dosing.elapsedSec);
    snprintf(buf, sizeof(buf), "Время: %s", timebuf);
    oled.drawUTF8(0, ROW3, buf);

    // ── "Новый цикл" button at ROW4 (cursor 0) ────────────────────────────────
    const char* btn = "[ Новый цикл ]";
    int bw = oled.getUTF8Width(btn);
    int bx = (128 - bw) / 2;
    if (s.cursorIndex == 0) {
        oled.drawBox(bx - 1, ROW4 - ROW_H, bw + 2, ROW_H + 2);
        oled.setDrawColor(0);
        oled.drawUTF8(bx, ROW4, btn);
        oled.setDrawColor(1);
    } else {
        oled.drawFrame(bx - 1, ROW4 - ROW_H, bw + 2, ROW_H + 2);
        oled.drawUTF8(bx, ROW4, btn);
    }
}
