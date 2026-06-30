#pragma once
#include "IStepperDriver.h"

class SimulatedStepper : public IStepperDriver {
public:
    void    moveTo(int32_t targetSteps, float speedStepsPerSec) override;
    void    stop() override;
    int32_t currentPosition() const override { return m_pos; }
    bool    isBusy() const override          { return m_pos != m_target; }
    void    zero() override                  { m_pos = 0; m_target = 0; }
    void    enable()  override { m_enabled = true;  }
    void    disable() override { m_enabled = false; }
    void    tick(uint32_t nowMs) override;

private:
    int32_t  m_pos     = 0;
    int32_t  m_target  = 0;
    float    m_speed   = 0.0f;   // steps/sec
    uint32_t m_lastMs  = 0;
    bool     m_enabled = false;
    float    m_accumFrac = 0.0f;
};
