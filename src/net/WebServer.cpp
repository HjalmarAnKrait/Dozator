#include "WebServer.h"
#include "../util/Logger.h"
#include <LittleFS.h>
#include <ESP8266WiFi.h>

void WebServer::begin(ILimitSwitches* sw, Settings* settings, StateMachine* sm) {
    m_protocol.begin(sw, settings, sm);
    m_broadcaster.begin(&m_ws);

    // ── WebSocket event handler ──────────────────────────────────────────────
    m_ws.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                         AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            LOG_INFO("WS: client #%u connected", client->id());
            m_broadcaster.sendTo(client);
        } else if (type == WS_EVT_DISCONNECT) {
            LOG_INFO("WS: client #%u disconnected", client->id());
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

    // ── Static files from LittleFS ───────────────────────────────────────────
    m_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // ── Captive portal ───────────────────────────────────────────────────────
    // ВАЖНО: НЕ редиректим (302 на 192.168.4.1 давал ERR_TOO_MANY_REDIRECTS,
    // особенно в Safari/iOS). На любой неизвестный путь и на детект-эндпоинты
    // отдаём саму страницу приложения (HTTP 200). Если ФС не залита (нет
    // index.html) — отдаём подсказку, а не петлю.
    auto serveApp = [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html",
                "<html><body><h2>Dozator</h2>"
                "<p>Web-files not uploaded. Run: <code>pio run -t uploadfs</code></p>"
                "</body></html>");
        }
    };
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
