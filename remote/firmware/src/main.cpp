/**
 * Выносной пульт дозатора: ESP32-C3 SuperMini, энкодер с кнопкой, кнопка СТОП, OLED 0.96" 128x64.
 * Связь с дозатором — ESP-NOW (../../shared/RemoteProtocol.h), меню — ui.cpp
 * (повторяет прототип remote/prototype.html).
 */
#include <Arduino.h>
#include "remote_config.h"
#include "input.h"
#include "link.h"
#include "power.h"
#include "ui.h"
#include "version.h"
#include "ota.h"
#include <esp_ota_ops.h>

// Откат прошивки: новая прошивка после записи по воздуху считается непроверенной.
// Если она упадёт или зависнет раньше, чем loop() проработает 15 с, загрузчик
// при следующем запуске вернёт старую (CONFIG_APP_ROLLBACK_ENABLE в ядре включён).
extern "C" bool verifyRollbackLater() { return true; }

// Перезагрузка не по нашей воле — пишем причину на экран: иначе пульт просто
// «вышел в меню», и не понять, просело питание или упала прошивка.
static const char* resetText(esp_reset_reason_t r, bool inOta) {
    switch (r) {
        case ESP_RST_BROWNOUT: return inOta ? "OTA: ПРОСЕЛО ПИТАНИЕ" : "СБРОС: ПРОСЕЛО ПИТАНИЕ";
        case ESP_RST_PANIC:    return inOta ? "OTA: СБОЙ ПРОШИВКИ" : "СБРОС: СБОЙ ПРОШИВКИ";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return inOta ? "OTA: ЗАВИС" : "СБРОС: ЗАВИС";
        default:               return inOta ? "OTA: ПЕРЕЗАГРУЗКА" : nullptr;
    }
}

void setup() {
    Serial.begin(115200);
    esp_reset_reason_t rr = esp_reset_reason();
    bool inOta = ota::wasActiveBeforeReset() && rr != ESP_RST_POWERON;
    power::begin();
    input::begin(power::wokeFromSleep(), power::wokeByStop());   // кнопка энкодера, которой разбудили, не жмёт пункт меню
    ui::begin();
    rlink::begin();
    Serial.printf("[I] reset reason %d%s\n", (int)rr, inOta ? " (during OTA)" : "");
    static char why[40];
    if (const char* w = ota::lastExitReason()) {
        snprintf(why, sizeof(why), "OTA: %s", w);
        Serial.printf("[I] last OTA exit: %s\n", w);
        ui::alert(why, 5000);
    } else if (const char* t = resetText(rr, inOta)) ui::alert(t, 5000);
    if (power::hasBattery()) Serial.printf("[I] Dozator remote v%s, battery %.2f V\n", kFwVersion, power::voltage());
    else Serial.printf("[I] Dozator remote v%s, USB power (no battery / divider)\n", kFwVersion);
    Serial.printf("[I] %s\n", kFwMarker);   // ссылка держит метку в образе (страница обновления)
}

void loop() {
    uint32_t now = millis();

    static bool appValid = false;
    if (!appValid && now > 15000) { esp_ota_mark_app_valid_cancel_rollback(); appValid = true; }

    input::Events e = input::poll(now);
    if (ota::active()) {                    // режим обновления: только сеть, страница и экран
        ota::tick(now);
        ui::tickOta(e, now);
        power::tick(now);
        // Банка села, пока ждём файл, — выходим (перезагрузка в обычный режим уложит
        // пульт спать). Посреди записи не трогаем: откат вернёт старую прошивку.
        if (power::empty() && ota::status().phase != ota::Phase::UPLOAD) ota::exitToNormal("БАНКА СЕЛА");
        delay(2);
        return;
    }
    rlink::tick(now);
    ui::handle(e, now);
    if (ui::otaRequested()) {
        rlink::shutdown();                  // ESP-NOW выключаем: радио отдаём сети обновления
        Serial.println("[I] OTA requested");
        ota::begin();
        ui::enterOta(now);
        return;
    }
    ui::tick(now);
    power::tick(now);

    // Банка села: показываем предупреждение и через EMPTY_SLEEP_DELAY_MS засыпаем.
    // Цикл при этом идёт как обычно — СТОП, нажатый в эти секунды, уходит.
    static uint32_t emptySince = 0;
    bool emptyNow = false;
    if (power::empty()) {
        if (!emptySince) { emptySince = now ? now : 1; ui::alert("ЗАРЯДИ АККУМУЛЯТОР", EMPTY_SLEEP_DELAY_MS); }
        emptyNow = now - emptySince >= EMPTY_SLEEP_DELAY_MS;
    } else {
        emptySince = 0;
    }
    if (emptyNow || ui::wantsDeepSleep(now)) {
        ui::prepareSleep();
        rlink::shutdown();
        power::deepSleep();
    }
    delay(2);
}
