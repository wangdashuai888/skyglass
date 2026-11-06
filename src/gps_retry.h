#pragma once
#include <stdint.h>

// Absence of valid NMEA triggers recovery, not absence of a satellite fix.
class GpsRetry {
    uint32_t lastValid_ = 0, lastAttempt_ = 0;
    bool attempted_ = false;
public:
    void start(uint32_t now) { lastValid_ = now; attempted_ = false; }
    void received(uint32_t now) { lastValid_ = now; }
    bool due(uint32_t now) const {
        return uint32_t(now - lastValid_) >= 15000 &&
               (!attempted_ || uint32_t(now - lastAttempt_) >= 15000);
    }
    void attempted(uint32_t now) { lastAttempt_ = now; attempted_ = true; }
};

class GpsResetPolicy {
    uint32_t lastValid_ = 0, lastReset_ = 0;
    bool attempted_ = false;
public:
    void start(uint32_t now) { lastValid_ = now; attempted_ = false; }
    void received(uint32_t now) { lastValid_ = now; }
    bool due(uint32_t now) const {
        return uint32_t(now - lastValid_) >= 30000 &&
               (!attempted_ || uint32_t(now - lastReset_) >= 60000);
    }
    void attempted(uint32_t now) { lastReset_ = now; attempted_ = true; }
};
