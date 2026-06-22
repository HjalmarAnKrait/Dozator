#pragma once
#include <DNSServer.h>

class CaptivePortal {
public:
    void begin();
    void tick();

private:
    DNSServer m_dns;
};
