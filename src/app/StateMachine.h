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
 * Управление — через transitionTo() (команды с веба и пульта) и через tick()
 * (переходы по концевикам).
 *
 * transitionTo() — это ВХОД в состояние: запускает движение, сбрасывает таймеры.
 * Для простого обновления UI после правки значений — requestBroadcast(), не
 * transitionTo(g_state.screen).
 */
class StateMachine {
public:
    void begin(IStepperDriver* stepper, ILimitSwitches* switches, Settings* settings);
    void transitionTo(Screen next);
    void tick(uint32_t nowMs);
    void stopButtonPress();   // физическая кнопка / глоб. UI: двухфазный СТОП→Парковка
    void jog(int dir);        // ручной режим: сдвинуть на jogSteps (dir<0 вверх, dir>0 вниз)
    void jogBy(int32_t steps);// ручной режим: сдвиг на steps от текущей цели (пульт, щелчки энкодера)
    void jogHold(int dir);    // ручной режим: движение, пока повторяется команда (deadman)
    void jogStop();           // ручной режим: остановить движение
    bool jogHolding() const { return m_jogHolding; }   // идёт удержание ▲/▼ из веба
    bool motorBusy() const { return m_stepper && m_stepper->isBusy(); }
    int32_t motorTarget() const { return m_stepper ? m_stepper->targetPosition() : 0; }

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
    uint32_t m_moveStartMs       = 0;   // старт хода-к-концевику (для сторожа застревания)
    uint32_t m_moveTimeoutMs     = 0;   // допустимая длительность этого хода
    bool     m_jogHolding        = false;
    uint32_t m_enteredMs         = 0;     // когда вошли в текущее состояние
    uint32_t m_jogHoldUntilMs    = 0;

    static bool sleepAllowed(Screen s);
    uint8_t  m_calibStep         = 0;     // фаза 1 калибровки: 0 быстро к BOT, 1 отъезд, 2 медленно к BOT

    void startMove(int32_t target, float speed, uint32_t nowMs);   // ход к концевику со сторожем
    static int32_t mmToSteps(float mm);
    int32_t travelLimit() const;      // сколько шагов ехать к концевику, прежде чем признать сбой
    bool moveStuck(uint32_t nowMs);   // ход к концевику: таймаут или доезд до цели без концевика
    void jogTo(int32_t target);
};

extern StateMachine g_sm;
