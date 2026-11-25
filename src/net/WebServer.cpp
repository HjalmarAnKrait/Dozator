#include "WebServer.h"
#include "../util/Logger.h"
#include <ESP8266WiFi.h>
#include <Updater.h>
#include "../app/StateMachine.h"
#include "../app/AppState.h"
#include "version.h"
#include "ota_page.h"
#include "web_assets_gen.h"     // веб-интерфейс из data/, вшитый при сборке (tools/embed_web.py)

static void sendAsset(AsyncWebServerRequest* req, const WebAsset& a) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, a.type, a.gz, a.len);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-cache");   // после обновления прошивки — сразу новый веб
    req->send(r);
}

// Почему сейчас нельзя обновляться ("" — можно).
static const char* otaBusyReason(StateMachine* sm) {
    Screen s = g_state.screen;
    bool moving = s == Screen::PARKING || s == Screen::CHARGING || s == Screen::DOSING ||
                  s == Screen::CALIBRATING || (sm && sm->motorBusy());
    if (moving) return "Мотор движется — сначала останови.";
    return "";
}

void WebServer::begin(ILimitSwitches* sw, Settings* settings, StateMachine* sm) {
    m_protocol.begin(sw, settings, sm);
    m_broadcaster.begin(&m_ws);

    // ── WebSocket event handler ──────────────────────────────────────────────
    m_ws.onEvent([this, sm](AsyncWebSocket*, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            LOG_INFO("WS: client #%u connected", client->id());
            m_broadcaster.sendTo(client);
        } else if (type == WS_EVT_DISCONNECT) {
            LOG_INFO("WS: client #%u disconnected", client->id());
            // Клиент пропал посреди удержания ▲/▼ — не ждём deadman, сразу стоп.
            // Толчок с пульта (не удержание) не трогаем: телефон мог просто уснуть.
            if (sm && sm->jogHolding()) sm->jogStop();
        } else if (type == WS_EVT_DATA) {
            auto* info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                // Safe to cast: we added the null terminator below
                if (len < 512) {
                    char tmp[513];
                    memcpy(tmp, data, len);
                    tmp[len] = '\0';
                    m_protocol.handleMessage(client, tmp, len);
                }
            }
        }
    });
    m_server.addHandler(&m_ws);

    // ── Веб-интерфейс (вшит в прошивку, gzip) ───────────────────────────────
    for (const WebAsset& a : WEB_ASSETS) {
        const WebAsset* ap = &a;
        m_server.on(a.path, HTTP_GET, [ap](AsyncWebServerRequest* req) { sendAsset(req, *ap); });
    }
    m_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { sendAsset(req, WEB_ASSETS[0]); });

    // ── Обновление прошивки (страница общая с пультом: shared/ota_page.h) ───
    m_server.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html; charset=utf-8", OTA_PAGE);
    });
    m_server.on("/info", HTTP_GET, [sm](AsyncWebServerRequest* req) {
        char buf[160];
        snprintf(buf, sizeof(buf), "{\"device\":\"%s\",\"title\":\"Дозатор\",\"version\":\"%s\",\"busy\":\"%s\"}",
                 kFwName, kFwVersion, g_state.otaActive ? "Идёт обновление." : otaBusyReason(sm));
        req->send(200, "application/json", buf);
    });
    m_server.on("/update", HTTP_POST,
        [this](AsyncWebServerRequest* req) {           // загрузка закончилась
            bool ok = m_otaOk && !Update.hasError();
            AsyncWebServerResponse* r = req->beginResponse(ok ? 200 : 500, "text/plain; charset=utf-8",
                                                           ok ? String("OK") : m_otaError);
            r->addHeader("Connection", "close");
            req->send(r);
            if (ok) g_state.otaRestartAtMs = millis() + 1500;   // ответ должен успеть уйти
        },
        [this, sm](AsyncWebServerRequest* req, const String& filename, size_t index,
                   uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                m_otaOk = false;
                const char* busy = g_state.otaActive ? "Уже идёт обновление." : otaBusyReason(sm);
                if (*busy) { m_otaError = busy; return; }
                size_t space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                Update.runAsync(true);
                if (!Update.begin(space, U_FLASH)) {
                    m_otaError = "Не начать запись: " + Update.getErrorString();
                    return;
                }
                g_state.otaActive = true;
                g_state.otaLastDataMs = millis();
                m_otaOk = true;
                LOG_INFO("OTA: start %s", filename.c_str());
            }
            if (!g_state.otaActive || !m_otaOk) return;
            g_state.otaLastDataMs = millis();
            if (Update.write(data, len) != len) {
                m_otaOk = false;
                m_otaError = "Ошибка записи: " + Update.getErrorString();
            }
            if (final) {
                if (m_otaOk && Update.end(true)) {
                    LOG_INFO("OTA: done, %u bytes", (unsigned)(index + len));
                } else {
                    if (m_otaOk) m_otaError = "Файл не принят: " + Update.getErrorString();
                    m_otaOk = false;
                    Update.end(false);
                    g_state.otaActive = false;
                    LOG_WARN("OTA: failed: %s", m_otaError.c_str());
                }
            }
        });

    // ── Captive portal ───────────────────────────────────────────────────────
    // ВАЖНО: НЕ редиректим (302 на 192.168.4.1 давал ERR_TOO_MANY_REDIRECTS,
    // особенно в Safari/iOS). На любой неизвестный путь и на детект-эндпоинты
    // отдаём саму страницу приложения (HTTP 200). Если ФС не залита (нет
    // index.html) — отдаём подсказку, а не петлю.
    auto serveApp = [](AsyncWebServerRequest* req) { sendAsset(req, WEB_ASSETS[0]); };
    m_server.on("/generate_204",        HTTP_GET, serveApp);  // Android
    m_server.on("/gen_204",             HTTP_GET, serveApp);  // Android
    m_server.on("/hotspot-detect.html", HTTP_GET, serveApp);  // iOS / macOS
    m_server.on("/ncsi.txt",            HTTP_GET, serveApp);  // Windows
    m_server.on("/connecttest.txt",     HTTP_GET, serveApp);  // Windows
    m_server.on("/redirect",            HTTP_GET, serveApp);

    m_server.onNotFound(serveApp);

    m_server.begin();
    LOG_INFO("HTTP server started");
}
