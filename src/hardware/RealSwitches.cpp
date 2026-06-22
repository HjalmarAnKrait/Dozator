#include "RealSwitches.h"
#include "config.h"

RealSwitches::RealSwitches() {
    pinMode(SW_TOP_PIN, INPUT_PULLUP);
    pinMode(SW_A_PIN,   INPUT_PULLUP);
    pinMode(SW_B_PIN,   INPUT_PULLUP);
    pinMode(SW_BOT_PIN, INPUT_PULLUP);
}

void RealSwitches::tick(uint32_t nowMs) {
    // active-LOW: замкнутый концевик тянет пин на GND.
    m_top.update(digitalRead(SW_TOP_PIN) == LOW, nowMs);
    m_a.update(  digitalRead(SW_A_PIN)   == LOW, nowMs);
    m_b.update(  digitalRead(SW_B_PIN)   == LOW, nowMs);
    m_bot.update(digitalRead(SW_BOT_PIN) == LOW, nowMs);
}

SwitchSnapshot RealSwitches::read() {
    return { m_top.stable(), m_a.stable(), m_b.stable(), m_bot.stable() };
}
