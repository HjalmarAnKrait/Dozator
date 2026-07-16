#pragma once
#include "IStepperDriver.h"

/**
 * Real stepper driver for a STEP/DIR/EN board (A4988 / DRV8825 и т.п.).
 *
 * Шаги генерируются аппаратным таймером timer1 (ISR в IRAM) — на ESP8266
 * программная генерация в loop() не тянет высокие частоты шагов.
 *
 * Пины — из config.h: STEP_STEP_PIN, STEP_DIR_PIN (EN не заведён на МК).
 * Только один экземпляр (timer1 один на чип).
 */
class RealStepper : public IStepperDriver {
public:
    RealStepper();                      // настраивает пины и timer1

    void    moveTo(int32_t targetSteps, float speedStepsPerSec) override;
    void    stop() override;
    int32_t currentPosition() const override;
    bool    isBusy() const override;
    void    zero() override;
    void    enable()  override;
    void    disable() override;
    void    tick(uint32_t nowMs) override;   // no-op: всё в ISR
};
