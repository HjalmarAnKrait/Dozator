#include "IScreen.h"
#include "ScreenHelper.h"

// 16×16 checkbox with thick checkmark
static void drawBigCheckbox(U8G2& oled, int x, int y, bool checked) {
    oled.drawFrame(x, y, 16, 16);
    if (checked) {
        // Two-pixel-thick checkmark: (2,8)→(6,12)→(13,4)
        oled.drawLine(x+2,  y+8,  x+6,  y+12);
        oled.drawLine(x+6,  y+12, x+13, y+4);
        oled.drawLine(x+2,  y+9,  x+6,  y+13);
        oled.drawLine(x+6,  y+13, x+13, y+5);
    }
}

void renderCharging(U8G2& oled, const AppState& s) {
    oled.setFont(FONT_MAIN);
    drawCentred(oled, 9, "ЗАРЯДКА");

    // Left half  → A, centered at x=32
    // Right half → B, centered at x=96
    // Label above the box, box 16×16

    // "A" label centred above left box
    oled.drawUTF8(29, 24, "A");  // ~x=32 - half_charW
    // "B" label centred above right box
    oled.drawUTF8(93, 24, "B");  // ~x=96 - half_charW

    // Boxes: left at x=24,y=27  right at x=88,y=27  (16×16 → bottom at y=43)
    drawBigCheckbox(oled, 24, 27, s.switches.a);
    drawBigCheckbox(oled, 88, 27, s.switches.b);

    // Three down-arrows below boxes (FONT_MAIN baseline at y=56, top ≈46, gap=3px)
    int ax = (128 - 3 * CHAR_W - 8) / 2;
    oled.drawUTF8(ax,                   56, "v");
    oled.drawUTF8(ax + CHAR_W + 4,      56, "v");
    oled.drawUTF8(ax + 2*(CHAR_W + 4),  56, "v");

    oled.setFont(FONT_SMALL);
    drawCentred(oled, 63, "Долгий = Назад");
}
