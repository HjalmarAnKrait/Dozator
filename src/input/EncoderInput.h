#pragma once
#include <Arduino.h>
#include <Encoder.h>
#include "config.h"

enum class EncoderEvent : uint8_t {
    NONE,
    CW,
    CCW,
    CLICK,
    LONG
};

/**
 * Reads the rotary encoder (paulstoffregen/Encoder) and the push-button,
 * produces EncoderEvent values and deposits them into a bounded queue.
 *
 * Events can come from two sources:
 *   1. Physical encoder on GPIO
 *   2. Web client via WebSocket → pushEvent()
 * Both are drained from the same queue in loop().
 */
class EncoderInput {
public:
    void begin();
    void tick(uint32_t nowMs);     // call every loop() iteration
    EncoderEvent poll();           // dequeue one event (NONE if empty)
    void pushEvent(EncoderEvent e);// inject event from WebSocket

private:
    static constexpr uint8_t Q_SIZE = ENCODER_QUEUE_MAX;

    Encoder* m_enc = nullptr;

    // Button state machine
    bool     m_btnLastRaw    = true;   // INPUT_PULLUP → idle HIGH
    bool     m_btnStable     = true;
    bool     m_btnPending    = true;
    uint32_t m_btnChangeMs   = 0;
    uint32_t m_btnPressMs    = 0;
    bool     m_longFired     = false;
    bool     m_awaitRelease  = false;

    // Rotary accumulator
    int32_t  m_lastEncPos    = 0;
    int32_t  m_encAccum      = 0;

    // Circular queue
    EncoderEvent m_queue[Q_SIZE];
    uint8_t      m_head = 0;
    uint8_t      m_tail = 0;

    void enqueue(EncoderEvent e);
    void processButton(bool raw, uint32_t nowMs);
};
