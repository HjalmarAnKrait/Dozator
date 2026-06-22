#pragma once
#include <ESP8266WiFi.h>

class WiFiAp {
public:
    void begin(const char* ssid, const char* password);
};
