#include "input.h"
#include "remote_config.h"

namespace input {

static volatile int32_t s_raw = 0;
static volatile uint8_t s_prev = 0;
static int32_t  s_consumed = 0;

// Таблица переходов квадратуры: индекс = (prev<<2)|cur, значение = -1/0/+1.
static const int8_t QDEC[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

static void IRAM_ATTR encIsr() {
    uint8_t cur = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    s_raw += QDEC[(s_prev << 2) | cur];
    s_prev = cur;
}

// СТОП ловим прерыванием: кадр экрана держит цикл ~25 мс, и короткое нажатие
// могло проскочить между двумя опросами. Нажатие = спад после ≥50 мс тишины
// (дребезг и дребезг отпускания отсекаются); помеха короче 3 мс не считается.
static constexpr uint32_t STOP_QUIET_US = 50000, STOP_MIN_US = 3000;
static volatile bool     s_stopLatched = false;
static volatile uint32_t s_stopEdgeUs = 0, s_stopPressUs = 0;

static void IRAM_ATTR stopIsr() {
    uint32_t t = micros();
    if (digitalRead(PIN_BTN_STOP) == LOW && t - s_stopEdgeUs > STOP_QUIET_US && !s_stopLatched) {
        s_stopLatched = true;
        s_stopPressUs = t;
    }
    s_stopEdgeUs = t;
}

// Кнопка с дебаунсом: pressed/released — фронты после устаканивания.
struct Btn {
    uint8_t  pin;
    bool     stable = false, pending = false;
    uint32_t changeMs = 0;
    explicit Btn(uint8_t p) : pin(p) {}
    void begin() { stable = pending = digitalRead(pin) == LOW; }
    // 1 — нажали, -1 — отпустили, 0 — без изменений
    int poll(uint32_t now) {
        bool raw = digitalRead(pin) == LOW;
        if (raw != pending) { pending = raw; changeMs = now; }
        if (now - changeMs >= BTN_DEBOUNCE_MS && stable != pending) { stable = pending; return stable ? 1 : -1; }
        return 0;
    }
};
// Нажатие с долгим удержанием: клик — на отпускание, долгое — сразу по порогу.
struct Press {
    Btn      btn;
    uint32_t downMs = 0;
    bool     longFired = false, ignoreUntilRelease = false;
    explicit Press(uint8_t p) : btn(p) {}
    void poll(uint32_t now, Events& e) {
        int ev = btn.poll(now);
        if (ev > 0) { downMs = now; longFired = false; }
        else if (ev < 0) {
            if (ignoreUntilRelease) ignoreUntilRelease = false;
            else if (!longFired) e.click = true;
        }
        if (btn.stable && !longFired && !ignoreUntilRelease && now - downMs >= LONG_PRESS_MS) {
            longFired = true;
            e.longPress = true;
        }
    }
};
static Press    s_enc{PIN_ENC_SW};
static Btn      s_stop{PIN_BTN_STOP};
static bool     s_stopAtStart = false;
static uint32_t s_stopDownMs = 0;
static bool     s_parkFired = false;
static bool     s_parkReal = false;     // парковка именно сработала (а не подавлена)
static uint32_t s_comboMs = 0;          // когда зажали энкодер и СТОП вместе
static bool     s_comboFired = false;
static bool     s_comboBlocked = false; // одна из кнопок уже отработала своё удержание — это не комбо
static bool     s_bootHold = false;     // ручка зажата с самого включения и ещё не отпущена
static bool     s_resetFired = false;

void begin(bool woke, bool wokeByStop) {
    pinMode(PIN_ENC_A, INPUT_PULLUP);
    pinMode(PIN_ENC_B, INPUT_PULLUP);
    pinMode(PIN_ENC_SW, INPUT_PULLUP);
    pinMode(PIN_BTN_STOP, INPUT_PULLUP);
    s_prev = (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encIsr, CHANGE);
    s_stopEdgeUs = micros();
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_STOP), stopIsr, CHANGE);
    s_enc.btn.begin(); s_stop.begin();
    s_enc.ignoreUntilRelease = woke && s_enc.btn.stable;
    // Ручку держат уже при включении — возможно, это сброс настроек: ни «назад», ни клика.
    s_bootHold = s_enc.btn.stable;
    if (s_bootHold) { s_enc.ignoreUntilRelease = true; s_enc.longFired = true; }
    s_stopAtStart = wokeByStop || (woke && s_stop.stable);   // разбудили кнопкой СТОП — это и есть СТОП
    s_stopDownMs = millis();
    s_parkFired = !s_stop.stable;
}

Events poll(uint32_t nowMs) {
    Events e;

    noInterrupts();
    int32_t raw = s_raw;
    interrupts();
    // Деление с округлением вниз: щелчки симметричны по обе стороны от нуля.
    int32_t q = raw >= 0 ? raw / ENC_STEPS_PER_DETENT : -((-raw + ENC_STEPS_PER_DETENT - 1) / ENC_STEPS_PER_DETENT);
    int32_t det = q - s_consumed;
    s_enc.poll(nowMs, e);
    // Щелчки при зажатой ручке не считаем: ручка проворачивается от самого нажатия,
    // и такой щелчок мог бы, например, перевести диалог на «ДА» перед отпусканием.
    if (det) {
        s_consumed += det;
        if (ENC_REVERSE) det = -det;
        if (!s_enc.btn.stable && !s_enc.btn.pending) e.detents = (int8_t)constrain(det, -100, 100);
    }

    // Само нажатие СТОП — из защёлки прерывания; дебаунс-опрос нужен только для удержания.
    if (s_stop.poll(nowMs) > 0) { s_stopDownMs = nowMs; s_parkFired = false; s_parkReal = false; }
    if (s_stopLatched) {
        noInterrupts();
        uint32_t press = s_stopPressUs, edge = s_stopEdgeUs;
        bool low = digitalRead(PIN_BTN_STOP) == LOW;
        bool real = low || edge - press >= STOP_MIN_US;   // держат или держали дольше помехи
        s_stopLatched = false;
        interrupts();
        if (real) { e.stop = true; e.stopWithEnc = s_enc.btn.stable; }
    }
    if (s_stopAtStart) { e.stop = true; s_stopAtStart = false; }

    // Энкодер + СТОП вместе — вход в обновление. Пока держат вместе, ни парковки
    // (удержание СТОП), ни «назад» (удержание энкодера), ни клика на отпускании.
    // Комбо считается, только если ни одна кнопка ещё не отработала своё удержание:
    // энкодер уже дал «назад» или СТОП уже дал парковку — это не попытка комбо.
    if (s_enc.btn.stable && s_stop.stable) {
        if (!s_comboMs && !s_comboBlocked) {
            if (s_enc.longFired || s_parkReal) s_comboBlocked = true;
            else s_comboMs = nowMs ? nowMs : 1;
        }
        s_parkFired = true;
        s_enc.longFired = true;
        s_enc.ignoreUntilRelease = true;
        if (s_comboMs && !s_comboFired && nowMs - s_comboMs >= OTA_HOLD_MS) { e.ota = true; s_comboFired = true; }
    } else {
        s_comboMs = 0;
        s_comboFired = false;
        s_comboBlocked = false;
    }
    if (s_stop.stable && !s_parkFired && nowMs - s_stopDownMs >= PARK_HOLD_MS) { e.park = true; s_parkFired = s_parkReal = true; }
    // Сброс настроек: ручку зажали до включения и держат RESET_HOLD_MS (СТОП не трогали).
    if (s_bootHold && (!s_enc.btn.stable || s_stop.stable)) s_bootHold = false;
    if (s_bootHold && !s_resetFired && nowMs >= RESET_HOLD_MS) { e.reset = true; s_resetFired = true; }
    return e;
}

uint32_t resetHeldMs(uint32_t nowMs) { return (s_bootHold && !s_resetFired) ? nowMs : 0; }

uint32_t comboHeldMs(uint32_t nowMs) {
    return (s_comboMs && !s_comboFired) ? nowMs - s_comboMs : 0;
}

uint32_t stopHeldMs(uint32_t nowMs) {
    return (s_stop.stable && !s_parkFired) ? nowMs - s_stopDownMs : 0;
}

}  // namespace input
