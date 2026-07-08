#pragma once
#include <Arduino.h>
#include "AppState.h"
#include "../hardware/IStepperDriver.h"
#include "../hardware/ILimitSwitches.h"

class Settings;

/**
 * Central state machine. Owns all transition logic and side effects.
 * Reads/writes g_state directly. Does NOT render or broadcast – callers do.
 *
 * Управление — через transitionTo() (вызывается WsProtocol по командам с веба)
 * и через tick() (переходы по концевикам). Энкодер/OLED удалены.
 */
class StateMachine {
public:
    void begin(IStepperDriver* stepper, ILimitSwitches* switches, Settings* settings);
    void transitionTo(Screen next);
    void tick(uint32_t nowMs);
    void stopButtonPress();   // физическая кнопка / глоб. UI: двухфазный СТОП→Парковка

    bool needsBroadcast() {
        bool b = m_broadcastPending;
        m_broadcastPending = false;
        return b;
    }
    void requestBroadcast() { m_broadcastPending = true; }

private:
    IStepperDriver*  m_stepper  = nullptr;
    ILimitSwitches*  m_switches = nullptr;
    Settings*        m_settings = nullptr;

    bool     m_broadcastPending = false;
    uint32_t m_dosingStartMs    = 0;
    int32_t  m_dosingStartPos   = 0;    // позиция заряда (2), шагов от дома
    int32_t  m_dosingTargetSteps = 0;   // L2 = H - L, ход дозы 2→3, шагов
    uint8_t  m_calibPhase        = 0;   // калибровка: 0 = хоуминг, 1 = спуск к концу
    uint32_t m_moveStartMs       = 0;   // старт хода-к-концевику (для сторожа застревания)

    static bool sleepAllowed(Screen s);
    bool moveStuck(uint32_t nowMs);   // ход к концевику: таймаут или доезд до предела
};

extern StateMachine g_sm;
