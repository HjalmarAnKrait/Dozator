#pragma once
#include "ILimitSwitches.h"
#include "../util/Debounce.h"

/**
 * Реальные концевики: 4 пина напрямую на GPIO (config.h), все INPUT_PULLUP,
 * замыкание на общий GND → активный уровень LOW. Дебаунс 30 мс (ENC_DEBOUNCE_MS).
 *
 * Если у вас нормально-замкнутые (NC) концевики — инвертируйте логику в tick().
 */
class RealSwitches : public ILimitSwitches {
public:
    RealSwitches();
    void           tick(uint32_t nowMs) override;
    SwitchSnapshot read() override;

private:
    // Нажатие — быстро (каретка не проезжает концевик), отпускание — с полным дебаунсом.
    Debounce m_top{SWITCH_PRESS_MS, SWITCH_DEBOUNCE_MS};
    Debounce m_a{SWITCH_PRESS_MS, SWITCH_DEBOUNCE_MS};
    Debounce m_b{SWITCH_PRESS_MS, SWITCH_DEBOUNCE_MS};
    Debounce m_bot{SWITCH_PRESS_MS, SWITCH_DEBOUNCE_MS};
};
