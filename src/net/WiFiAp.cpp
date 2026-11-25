#include "WiFiAp.h"
#include "../util/Logger.h"
#include "RemoteProtocol.h"

void WiFiAp::begin(const char* ssid, const char* password) {
    // AP+STA: так ESP-NOW (пульт) работает рядом с точкой доступа. STA ни к чему не
    // подключается — она нужна только как интерфейс ESP-NOW. Канал фиксирован: пульт
    // шлёт на RP_CHANNEL, и точка доступа должна быть на нём же.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect();
    WiFi.softAP(ssid, password, RP_CHANNEL);
    LOG_INFO("AP started: %s  IP: %s", ssid, WiFi.softAPIP().toString().c_str());
}
