#include <Arduino.h>
#include <LittleFS.h>

#include "app/AppState.h"
#include "app/StateMachine.h"
#include "app/Calculations.h"
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
// Headless: ни OLED, ни энкодера. Управление и индикация — через веб (WebSocket).
static Settings       g_settings;
static WiFiAp         g_wifiAp;
static CaptivePortal  g_captive;
static WebServer      g_webServer;

static IStepperDriver*  g_stepper  = nullptr;
static ILimitSwitches*  g_switches = nullptr;

// ─── setup ────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    LOG_INFO("=== DOZATOR boot === v" FW_VERSION);

    // 1. LittleFS + Settings
    if (!LittleFS.begin()) {
        LOG_ERROR("LittleFS mount failed – check filesystem image");
    }
    g_settings.load();

    // 2. Hardware (simulated / real)
#if USE_SIMULATED_HW
    g_stepper  = new SimulatedStepper();
    g_switches = new SimulatedSwitches();
#else
    g_stepper  = new RealStepper();   // STEP=D5, DIR=D0, EN=D3 (см. config.h)
    g_switches = new RealSwitches();  // концевиков 4: D1/D2/D6/D7, INPUT_PULLUP
#endif

    // 3. State machine
    g_sm.begin(g_stepper, g_switches, &g_settings);
    g_sm.transitionTo(Screen::PARKING);

    // 4. WiFi AP + captive portal + web server
    g_wifiAp.begin(WIFI_SSID, WIFI_PASSWORD);
    g_captive.begin();
    g_webServer.begin(g_switches, &g_settings, &g_sm);

    g_state.lastInteractionMs = millis();
    LOG_INFO("Heap free: %u bytes", ESP.getFreeHeap());
    LOG_INFO("Ready.");
}

// ─── loop ─────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // DNS / Captive portal
    g_captive.tick();

    // Hardware ticks (читает концевики с дебаунсом, гонит шаги в ISR)
    if (g_switches) g_switches->tick(now);
    if (g_stepper)  g_stepper->tick(now);

    // State machine (переходы по концевикам + сон + прогресс дозирования)
    g_sm.tick(now);

    // Проброс запросов на бродкаст из StateMachine в WsBroadcaster
    if (g_sm.needsBroadcast()) g_webServer.broadcaster().requestBroadcast();

    // Отложенное сохранение настроек
    g_settings.tick(now);

    // WebSocket: троттлинг-бродкаст / heartbeat
    g_webServer.broadcaster().tick(now);
}
