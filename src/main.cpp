#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>

#include "app/AppState.h"
#include "app/StateMachine.h"
#include "app/Calculations.h"
#include "input/EncoderInput.h"
#include "ui/OledRenderer.h"
#include "ui/Fonts.h"
#include "hardware/IStepperDriver.h"
#include "hardware/ILimitSwitches.h"
#include "hardware/SimulatedStepper.h"
#include "hardware/SimulatedSwitches.h"
#include "hardware/RealStepper.h"
#include "hardware/RealSwitches.h"
#include "storage/Settings.h"
#include "net/WiFiAp.h"
#include "net/CaptivePortal.h"
#include "net/WebServer.h"
#include "util/Logger.h"
#include "config.h"
#include "version.h"

// ─── Globals ──────────────────────────────────────────────────────────────
static OledRenderer   g_oled;
static EncoderInput   g_encoder;
static Settings       g_settings;
static WiFiAp         g_wifiAp;
static CaptivePortal  g_captive;
static WebServer      g_webServer;

static IStepperDriver*  g_stepper  = nullptr;
static ILimitSwitches*  g_switches = nullptr;

// ─── Splash ───────────────────────────────────────────────────────────────
#if USE_OLED
static void showSplash() {
    auto& oled = g_oled.oled();
    oled.clearBuffer();
    oled.setFont(u8g2_font_10x20_t_cyrillic);
    int w = oled.getUTF8Width("DOZATOR");
    oled.drawUTF8((128 - w) / 2, 28, "DOZATOR");
    oled.setFont(u8g2_font_6x13_t_cyrillic);
    char ver[24];
    snprintf(ver, sizeof(ver), "v" FW_VERSION);
    w = oled.getUTF8Width(ver);
    oled.drawUTF8((128 - w) / 2, 44, ver);
    oled.sendBuffer();
    delay(1000);  // one-time startup splash – delay() is OK here
}
#endif  // USE_OLED

// ─── setup ────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    LOG_INFO("=== DOZATOR boot ===");

    // 1. I2C + 2. OLED (dead code — дисплей отключён, пины D1/D2 заняты концевиками)
#if USE_OLED
    Wire.begin();
    Wire.setClock(400000);
    g_oled.begin();
    showSplash();
#endif

    // 3. LittleFS + Settings
    if (!LittleFS.begin()) {
        LOG_ERROR("LittleFS mount failed – check filesystem image");
    }
    g_settings.load();

    // 4. Encoder (dead code — физический энкодер отключён, пины заняты мотором/концевиками).
    //    begin() НЕ вызываем (он бы повесил прерывания на GPIO12/13/14).
    //    poll() в loop() оставлен — через него приходят события веб-кнопок (pushEvent).
#if USE_ENCODER
    g_encoder.begin();
#endif

    // 5. Hardware (simulated / real)
#if USE_SIMULATED_HW
    g_stepper  = new SimulatedStepper();
    g_switches = new SimulatedSwitches();
#else
    g_stepper  = new RealStepper();   // STEP=D5, DIR=D0, EN=D3 (см. config.h)
    g_switches = new RealSwitches();  // концевиков 4: D1/D2/D6/D7, INPUT_PULLUP
#endif

    // 6. State machine
    g_sm.begin(g_stepper, g_switches, &g_settings);
    g_sm.transitionTo(Screen::PARKING);

    // 7. WiFi AP
    g_wifiAp.begin(WIFI_SSID, WIFI_PASSWORD);

    // 8. Captive portal
    g_captive.begin();

    // 9. Web server + WebSocket
    g_webServer.begin(&g_encoder, g_switches, &g_settings, &g_sm);

    g_state.lastInteractionMs = millis();
    LOG_INFO("Heap free: %u bytes", ESP.getFreeHeap());
    LOG_INFO("Ready.");
}

// ─── loop ─────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // DNS / Captive portal
    g_captive.tick();

    // Encoder events
#if USE_ENCODER
    g_encoder.tick(now);  // читает физический энкодер (отключён)
#endif
    // poll() остаётся всегда — доставляет события веб-кнопок (WsProtocol::pushEvent)
    for (;;) {
        EncoderEvent ev = g_encoder.poll();
        if (ev == EncoderEvent::NONE) break;
        g_sm.onEvent(ev);
    }

    // Hardware ticks
    if (g_switches) g_switches->tick(now);
    if (g_stepper)  g_stepper->tick(now);

    // State machine (switch-driven transitions + sleep + dosing progress)
    g_sm.tick(now);

    // Propagate broadcast requests from StateMachine to WsBroadcaster
    if (g_sm.needsBroadcast()) g_webServer.broadcaster().requestBroadcast();

    // Settings debounce save
    g_settings.tick(now);

    // OLED render at 20 Hz (dead code — дисплей отключён)
#if USE_OLED
    static uint32_t lastRender = 0;
    if (now - lastRender >= OLED_RENDER_INTERVAL_MS) {
        g_oled.render(g_state);
        lastRender = now;
    }
#endif

    // WebSocket throttled broadcast / heartbeat
    g_webServer.broadcaster().tick(now);
}
