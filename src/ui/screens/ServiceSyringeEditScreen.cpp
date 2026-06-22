#include "IScreen.h"
#include "ScreenHelper.h"
#include <stdio.h>

static void drawEditRow(U8G2& oled, uint8_t y, const char* buf, bool cursor, bool editing) {
    if (cursor) drawCursor(oled, y);
    if (cursor && editing) {
        if (blinkVisible(millis())) drawSelectedRow(oled, 8, y, buf);
        else                        oled.drawUTF8(8, y, buf);
    } else {
        oled.drawUTF8(8, y, buf);
    }
}

void renderServiceSyringeEdit(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);

    int idx = s.editingPresetIdx;
    if (idx < 0 || idx >= s.presetsCount) idx = 0;

    // Row 0 – title
    char title[24];
    snprintf(title, sizeof(title), "Шприц #%d", idx + 1);
    drawCentred(oled, ROW0, title);

    // Determine cursor layout
    bool hasDelete = (s.presetsCount > 1);
    // cursor: 0=vol, 1=diam, 2=delete(if exists)/back, 3=back(if delete exists)
    uint8_t backIdx = hasDelete ? 3 : 2;

    // Row 1 – volume (cursor 0)
    char buf[24];
    snprintf(buf, sizeof(buf), "Объём: %.0f мл", s.presets[idx].vol);
    drawEditRow(oled, ROW1, buf, s.cursorIndex == 0, s.editing);

    // Row 2 – diameter (cursor 1)
    snprintf(buf, sizeof(buf), "D ext: %.1f мм", s.presets[idx].diam);
    drawEditRow(oled, ROW2, buf, s.cursorIndex == 1, s.editing);

    // Row 3 – delete (cursor 2, only if >1 preset)
    if (hasDelete) {
        if (s.cursorIndex == 2) drawCursor(oled, ROW3);
        oled.drawUTF8(8, ROW3, "[X] Удалить");
    }

    // Row 4 – back (cursor 2 or 3)
    if (s.cursorIndex == (int)backIdx) drawCursor(oled, ROW4);
    oled.drawUTF8(8, ROW4, "Назад");
}
