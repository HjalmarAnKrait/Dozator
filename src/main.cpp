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
#include "net/EspNowLink.h"
#include "util/Logger.h"
#include "util/Debounce.h"
#include "config.h"
#include "version.h"
#include <Updater.h>

// ─── Globals ──────────────────────────────────────────────────────────────
// Headless: ни OLED, ни энкодера. Управление и индикация — через веб (WebSocket).
static Settings       g_settings;
static WiFiAp         g_wifiAp;
static CaptivePortal  g_captive;
static WebServer      g_webServer;
static EspNowLink     g_remote;      // выносной пульт (ESP-NOW)

static IStepperDriver*  g_stepper  = nullptr;
static ILimitSwitches*  g_switches = nullptr;

// ─── setup ────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    LOG_INFO("=== DOZATOR boot === v%s", kFwVersion);
    LOG_INFO("%s", kFwMarker);   // ссылка держит метку в образе (страница обновления)

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
    g_stepper  = new RealStepper();   // STEP=D2, DIR=D1 (EN на GND)
    g_switches = new RealSwitches();  // концевики TOP=D5, A=RX, B=D7, BOT=D6, INPUT_PULLUP
#endif

    // Микрошаг A4988 зашит перемычками на плате (MS1/2/3 → +5V = 1/16), МК не управляет.

    // Физическая кнопка СТОП (D3/GPIO0, INPUT_PULLUP, на GND).
    pinMode(BTN_STOP_PIN, INPUT_PULLUP);

    // 3. State machine
    g_sm.begin(g_stepper, g_switches, &g_settings);
    g_sm.transitionTo(Screen::IDLE);   // при включении ничего не двигаем; парковка — по команде

    // 4. WiFi AP + captive portal + web server
    g_wifiAp.begin(WIFI_SSID, WIFI_PASSWORD);
    g_captive.begin();
    g_webServer.begin(g_switches, &g_settings, &g_sm);
    g_remote.begin(&g_webServer.protocol(), &g_sm);   // после WiFi: нужен поднятый интерфейс

    g_state.lastInteractionMs = millis();
    LOG_INFO("Heap free: %u bytes", ESP.getFreeHeap());
    LOG_INFO("Ready.");
}

// ─── loop ─────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // DNS / Captive portal
    g_captive.tick();

    // Физическая кнопка СТОП (active-LOW, на GND) с дебаунсом — по фронту нажатия.
    static Debounce btnStop;
    if (btnStop.update(digitalRead(BTN_STOP_PIN) == LOW, now) && btnStop.stable()) {
        g_sm.stopButtonPress();
    }

    // Hardware ticks (читает концевики с дебаунсом, гонит шаги в ISR)
    if (g_switches) g_switches->tick(now);
    if (g_stepper)  g_stepper->tick(now);

    // State machine (переходы по концевикам + сон + прогресс дозирования)
    g_sm.tick(now);

    // Команды пульта (ESP-NOW) — до проброса бродкаста, чтобы их эффект ушёл сразу
    g_remote.tick(now);

    // Проброс запросов на бродкаст из StateMachine в WsBroadcaster и пульт
    if (g_sm.needsBroadcast()) {
        g_webServer.broadcaster().requestBroadcast();
        g_remote.requestState();
    }

    // Отложенное сохранение настроек. Запись во флеш блокирует цикл на десятки мс —
    // во время движения это опрос концевиков и команды СТОП, поэтому ждём остановки.
    if (!g_sm.motorBusy()) g_settings.tick(now);

    // WebSocket: троттлинг-бродкаст / heartbeat
    g_webServer.broadcaster().tick(now);

    // Обновление прошивки: записана — сохранить настройки и перезагрузиться;
    // загрузка оборвалась (закрыли страницу, ушли из сети) — бросить и снова принимать команды.
    if (g_state.otaRestartAtMs && (int32_t)(now - g_state.otaRestartAtMs) >= 0) {
        LOG_INFO("OTA: restart");
        g_settings.save();
        delay(100);
        ESP.restart();
    }
    if (g_state.otaActive && !g_state.otaRestartAtMs && (int32_t)(now - g_state.otaLastDataMs) > 15000) {   // со знаком: метку ставит async-обработчик, она бывает позже now
        LOG_WARN("OTA: upload stalled, aborted");
        Update.end(false);
        g_state.otaActive = false;
        g_sm.requestBroadcast();
    }
}
