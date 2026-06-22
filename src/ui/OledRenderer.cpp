#include "OledRenderer.h"
#include "screens/IScreen.h"
#include "Fonts.h"
#include "../util/Logger.h"

void OledRenderer::begin() {
    m_oled.begin();
    m_oled.enableUTF8Print();
    m_oled.setFont(FONT_MAIN);
}

void OledRenderer::render(const AppState& s) {
    if (s.displaySleeping) {
        if (!m_lastSleep) {
            m_oled.setPowerSave(1);
            m_lastSleep = true;
        }
        return;
    }

    if (m_lastSleep) {
        m_oled.setPowerSave(0);
        m_lastSleep = false;
    }

    m_oled.clearBuffer();
    m_oled.setDrawColor(1);
    m_oled.setFont(FONT_MAIN);

    switch (s.screen) {
        case Screen::PARKING:              renderParking(m_oled, s);          break;
        case Screen::PARKED:               renderParked(m_oled, s);           break;
        case Screen::CHARGING:             renderCharging(m_oled, s);         break;
        case Screen::CHARGED:              renderCharged(m_oled, s);          break;
        case Screen::DOSING:               renderDosing(m_oled, s);           break;
        case Screen::DONE:                 renderDone(m_oled, s);             break;
        case Screen::SERVICE_MENU:         renderServiceMenu(m_oled, s);      break;
        case Screen::SERVICE_PITCH:        renderServicePitch(m_oled, s);     break;
        case Screen::SERVICE_SLEEP:        renderServiceSleep(m_oled, s);     break;
        case Screen::SERVICE_SYRINGES:     renderServiceSyringes(m_oled, s);  break;
        case Screen::SERVICE_SYRINGE_EDIT: renderServiceSyringeEdit(m_oled, s); break;
        case Screen::SERVICE_FONT:         renderServiceFont(m_oled, s);        break;
    }

    m_oled.sendBuffer();
}
