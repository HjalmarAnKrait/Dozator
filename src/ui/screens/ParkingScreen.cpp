#include "IScreen.h"
#include "ScreenHelper.h"

void renderParking(U8G2& oled, const AppState&) {
    oled.setFont(FONT_MAIN);
    drawCentred(oled, 20, "Парковка");
    drawCentred(oled, 34, "Возврат в 0...");

    // Three up-arrows centred at y=55
    oled.setFont(FONT_MAIN);
    int arrowX = (128 - 3 * CHAR_W - 2 * 4) / 2;
    for (int i = 0; i < 3; i++) {
        oled.drawUTF8(arrowX + i * (CHAR_W + 4), 58, "^");
    }
}
