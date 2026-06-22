#include "CaptivePortal.h"
#include <ESP8266WiFi.h>

void CaptivePortal::begin() {
    m_dns.start(53, "*", WiFi.softAPIP());
}

void CaptivePortal::tick() {
    m_dns.processNextRequest();
}
