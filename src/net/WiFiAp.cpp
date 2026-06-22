#include "WiFiAp.h"
#include "../util/Logger.h"

void WiFiAp::begin(const char* ssid, const char* password) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    LOG_INFO("AP started: %s  IP: %s", ssid, WiFi.softAPIP().toString().c_str());
}
