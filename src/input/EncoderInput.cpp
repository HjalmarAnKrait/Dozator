#include "EncoderInput.h"
#include "config.h"

void EncoderInput::begin() {
    m_enc = new Encoder(PIN_ENC_CLK, PIN_ENC_DT);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    m_lastEncPos = m_enc->read();
}

void EncoderInput::tick(uint32_t nowMs) {
    // ── Rotary ──────────────────────────────────────────────────────────────
    int32_t pos  = m_enc->read();
    int32_t diff = pos - m_lastEncPos;
    m_lastEncPos = pos;
    m_encAccum  += diff;

    while (m_encAccum >= ENC_STEPS_PER_DETENT) {
        enqueue(EncoderEvent::CCW);
        m_encAccum -= ENC_STEPS_PER_DETENT;
    }
    while (m_encAccum <= -ENC_STEPS_PER_DETENT) {
        enqueue(EncoderEvent::CW);
        m_encAccum += ENC_STEPS_PER_DETENT;
    }

    // ── Button ──────────────────────────────────────────────────────────────
    bool raw = (digitalRead(PIN_ENC_SW) == LOW);  // active LOW (pull-up)
    processButton(raw, nowMs);
}

void EncoderInput::processButton(bool pressed, uint32_t nowMs) {
    // Debounce
    if (pressed != m_btnPending) {
        m_btnPending  = pressed;
        m_btnChangeMs = nowMs;
    }
    bool stable = (nowMs - m_btnChangeMs) >= ENC_DEBOUNCE_MS;
    if (!stable) return;

    bool wasStable = m_btnStable;
    m_btnStable    = m_btnPending;

    bool fell = (!wasStable && m_btnStable);   // LOW edge = press
    bool rose = ( wasStable && !m_btnStable);  // HIGH edge = release

    if (fell && !m_awaitRelease) {
        m_btnPressMs = nowMs;
        m_longFired  = false;
    }

    if (m_btnStable && !m_longFired && !m_awaitRelease) {
        if ((nowMs - m_btnPressMs) >= ENC_LONG_MS) {
            enqueue(EncoderEvent::LONG);
            m_longFired    = true;
            m_awaitRelease = true;
        }
    }

    if (rose) {
        if (!m_longFired) {
            uint32_t held = nowMs - m_btnPressMs;
            if (held >= ENC_DEBOUNCE_MS && held < ENC_CLICK_MAX_MS) {
                enqueue(EncoderEvent::CLICK);
            }
        }
        m_longFired    = false;
        m_awaitRelease = false;
    }
}

EncoderEvent EncoderInput::poll() {
    if (m_head == m_tail) return EncoderEvent::NONE;
    EncoderEvent e = m_queue[m_head];
    m_head = (m_head + 1) % Q_SIZE;
    return e;
}

void EncoderInput::pushEvent(EncoderEvent e) {
    enqueue(e);
}

void EncoderInput::enqueue(EncoderEvent e) {
    uint8_t next = (m_tail + 1) % Q_SIZE;
    if (next == m_head) return;  // queue full – drop
    m_queue[m_tail] = e;
    m_tail = next;
}
