#include "ota.h"
#include "remote_config.h"
#include "power.h"
#include "version.h"
#include "ota_page.h"
#include "RemoteProtocol.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

namespace ota {

static WebServer* s_srv = nullptr;
static bool       s_active = false;
static Status     s_st;
static uint32_t   s_total = 0;
static bool       s_uploadOk = false;
static String     s_err;                 // для страницы
static uint32_t   s_lastActivityMs = 0;
static uint32_t   s_restartAtMs = 0;
static uint32_t   s_errorAtMs = 0;

static void fail(const char* screenMsg, const String& pageMsg) {
    s_uploadOk = false;
    s_err = pageMsg;
    s_st.phase = Phase::ERROR;
    strlcpy(s_st.msg, screenMsg, sizeof(s_st.msg));
    s_errorAtMs = millis();
    Serial.printf("[W] OTA: %s\n", pageMsg.c_str());
}

static const char* busyReason() {
    if (power::hasBattery() && power::percent() < OTA_MIN_BATTERY_PCT) return "Аккумулятор пульта разряжен — заряди.";
    return "";
}

static void logReq() {
    Serial.printf("[I] OTA http %s %s\n", s_srv->method() == HTTP_GET ? "GET" : "POST", s_srv->uri().c_str());
}

static void sendPage() {
    s_lastActivityMs = millis();
    logReq();
    s_srv->send_P(200, "text/html; charset=utf-8", OTA_PAGE);
}

static void onInfo() {
    s_lastActivityMs = millis();
    logReq();
    char buf[200];
    snprintf(buf, sizeof(buf), "{\"device\":\"%s\",\"title\":\"Пульт\",\"version\":\"%s\",\"busy\":\"%s\"}",
             kFwName, kFwVersion, busyReason());
    s_srv->send(200, "application/json", buf);
}

static void onUploadDone() {
    bool ok = s_uploadOk && !Update.hasError();
    s_srv->sendHeader("Connection", "close");
    s_srv->send(ok ? 200 : 500, "text/plain; charset=utf-8", ok ? String("OK") : s_err);
    if (ok) {
        s_st.phase = Phase::DONE;
        s_st.percent = 100;
        s_restartAtMs = millis() + 2500;     // ответ уходит, на экране «ГОТОВО»
        Serial.println("[I] OTA: done");
    }
}

static void onUploadChunk() {
    HTTPUpload& up = s_srv->upload();
    s_lastActivityMs = millis();
    switch (up.status) {
        case UPLOAD_FILE_START: {
            s_uploadOk = true;
            s_st.phase = Phase::UPLOAD;
            s_st.percent = 0;
            s_total = s_srv->arg("size").toInt();
            const char* busy = busyReason();
            if (*busy) { fail("ЗАРЯДИ ПУЛЬТ", busy); return; }
            if (!Update.begin(s_total > 0 ? s_total : UPDATE_SIZE_UNKNOWN))
                fail("НЕТ МЕСТА", String("Не начать запись: ") + Update.errorString());
            Serial.printf("[I] OTA: start %s, %u bytes\n", up.filename.c_str(), (unsigned)s_total);
            break;
        }
        case UPLOAD_FILE_WRITE:
            if (!s_uploadOk) return;
            if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                fail("ОШИБКА ЗАПИСИ", String("Ошибка записи: ") + Update.errorString());
                Update.abort();
                return;
            }
            if (s_total) s_st.percent = (uint8_t)min<uint32_t>(99, Update.progress() * 100 / s_total);
            break;
        case UPLOAD_FILE_END:
            if (!s_uploadOk) return;
            // end() проверяет образ целиком (в т.ч. что он для ESP32-C3) и переключает загрузку на него.
            if (!Update.end(true)) fail("ФАЙЛ НЕ ПРИНЯТ", String("Файл не принят: ") + Update.errorString());
            break;
        case UPLOAD_FILE_ABORTED:
            Update.abort();
            fail("ЗАГРУЗКА ПРЕРВАНА", "Загрузка прервана.");
            break;
    }
}

// Пометка «был в режиме обновления» переживает перезагрузку (не питание): если пульт
// перезагрузился сам посреди OTA, после старта видно, что дело было в обновлении.
static RTC_NOINIT_ATTR uint32_t s_otaMark;
static constexpr uint32_t OTA_MARK = 0x0DA7A0A1u;

// Причина выхода — тоже в RTC: после перезагрузки показываем её на экране.
static RTC_NOINIT_ATTR char     s_exitWhy[40];   // кириллица — 2 байта на букву
static RTC_NOINIT_ATTR uint32_t s_exitMark;

const char* lastExitReason() {
    if (s_exitMark != OTA_MARK) return nullptr;
    s_exitMark = 0;
    s_exitWhy[sizeof(s_exitWhy) - 1] = 0;
    return s_exitWhy;
}

bool wasActiveBeforeReset() {
    bool r = s_otaMark == OTA_MARK;
    s_otaMark = 0;
    return r;
}

void begin() {
    s_active = true;
    s_st = Status{};
    s_lastActivityMs = millis();
    s_otaMark = OTA_MARK;
    // Мощность ставим до старта точки доступа: на полной мощности (20 дБм) подключение
    // телефона даёт всплеск тока, и пульт от банки перезагружается по просадке питания.
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(OTA_TX_POWER);
    WiFi.softAP(OTA_SSID, OTA_PASS, RP_CHANNEL);
    WiFi.setTxPower(OTA_TX_POWER);
    WiFi.onEvent([](WiFiEvent_t ev, WiFiEventInfo_t) {
        if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED)    Serial.println("[I] OTA wifi: client connected");
        if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) Serial.println("[I] OTA wifi: client disconnected");
        if (ev == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED)   Serial.println("[I] OTA wifi: client got IP");
    });
    s_srv = new WebServer(80);
    s_srv->on("/", HTTP_GET, sendPage);
    s_srv->on("/update", HTTP_GET, sendPage);
    s_srv->on("/info", HTTP_GET, onInfo);
    s_srv->on("/update", HTTP_POST, onUploadDone, onUploadChunk);
    s_srv->onNotFound(sendPage);
    s_srv->begin();
    Serial.printf("[I] OTA mode: WiFi \"%s\", http://%s\n", OTA_SSID, WiFi.softAPIP().toString().c_str());
}

bool active() { return s_active; }
const Status& status() { return s_st; }

void exitToNormal(const char* why) {
    s_otaMark = 0;
    strlcpy(s_exitWhy, why, sizeof(s_exitWhy));
    s_exitMark = OTA_MARK;
    Serial.printf("[I] OTA: exit (%s), restart\n", why);
    Serial.flush();
    delay(100);
    ESP.restart();
    while (true) {}
}

void tick(uint32_t nowMs) {
    if (!s_active) return;
    static uint32_t s_logMs = 0;
    if (nowMs - s_logMs > 2000) {
        s_logMs = nowMs;
        Serial.printf("[I] OTA t=%lus phase=%d %u%% bat=%.2fV%s heap=%u clients=%d\n",
                      (unsigned long)(nowMs / 1000), (int)s_st.phase, s_st.percent, power::voltage(),
                      power::empty() ? " EMPTY" : "", (unsigned)ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    }
    s_srv->handleClient();
    // Обработчики выше ставят метки через millis() — они позже nowMs, снятого в начале
    // цикла, и беззнаковая разность с nowMs ушла бы в 4 млрд («10 мин без файла» сразу
    // после первого запроса). Поэтому время — заново, а сравнение — со знаком.
    nowMs = millis();
    auto since = [nowMs](uint32_t t) { int32_t d = (int32_t)(nowMs - t); return d > 0 ? (uint32_t)d : 0u; };
    if (s_restartAtMs && (int32_t)(nowMs - s_restartAtMs) >= 0) exitToNormal("ЗАПИСАНО");
    // Ошибку показываем 6 с, потом снова «жду файл» — можно повторить.
    if (s_st.phase == Phase::ERROR && since(s_errorAtMs) > 6000) { s_st = Status{}; }
    // Связь оборвалась посреди загрузки, а сервер этого не заметил.
    if (s_st.phase == Phase::UPLOAD && since(s_lastActivityMs) > 30000) {
        Update.abort();
        fail("ЗАГРУЗКА ПРЕРВАНА", "Загрузка прервана.");
    }
    if (s_st.phase != Phase::UPLOAD && since(s_lastActivityMs) > OTA_IDLE_MS) exitToNormal("10 МИН БЕЗ ФАЙЛА");
}

}  // namespace ota
