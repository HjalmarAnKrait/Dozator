#pragma once
#include <Arduino.h>

/** Hardware abstraction for a stepper motor driver. */
class IStepperDriver {
public:
    virtual ~IStepperDriver() = default;

    /** Move to absolute step position at given speed (steps/sec). */
    virtual void moveTo(int32_t targetSteps, float speedStepsPerSec) = 0;
    virtual void stop() = 0;

    virtual int32_t currentPosition() const = 0;
    virtual bool    isBusy() const = 0;

    /** Принять текущую позицию за 0 (после хоуминга по концевику). */
    virtual void    zero() = 0;

    virtual void enable()  = 0;
    virtual void disable() = 0;

    /** Must be called from loop() every cycle. */
    virtual void tick(uint32_t nowMs) = 0;
};
