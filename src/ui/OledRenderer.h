#pragma once
#include <U8g2lib.h>
#include <Wire.h>
#include "../app/AppState.h"

class OledRenderer {
public:
    void begin();
    void render(const AppState& s);

    // Access to the underlying U8G2 object (e.g. for splash screen in main.cpp)
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C& oled() { return m_oled; }

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C m_oled{U8G2_R0, U8X8_PIN_NONE};
    bool m_lastSleep = false;
};
