#pragma once
#include <stdint.h>

enum class SleepMode : uint8_t { Awake, Normal, Deep };
// Both timeouts are measured from the last interaction, not from entry to normal sleep.
// Manual sleep lasts until touch/wake; normal may progress to deep automatically.
struct SleepPolicy {
    SleepMode mode = SleepMode::Awake;
    uint32_t lastActivity = 0;
    uint32_t normalMs = 0, deepMs = 0;
    bool swallow = false;
    void wake(uint32_t now) { mode = SleepMode::Awake; lastActivity = now; }
    void enter(SleepMode next, uint32_t now) { mode = next; lastActivity = now; }
    bool touch(bool pressed, uint32_t now) {
        if (!pressed) { swallow = false; return false; }
        if (mode != SleepMode::Awake) { wake(now); swallow = true; }
        lastActivity = now;
        return swallow;
    }
    void tick(uint32_t now) {
        const uint32_t idle = now - lastActivity;
        if (deepMs && idle >= deepMs) mode = SleepMode::Deep;
        else if (normalMs && idle >= normalMs && mode == SleepMode::Awake) mode = SleepMode::Normal;
    }
};
inline bool sleep_timeouts_valid(uint32_t normalSec, uint32_t deepSec) {
    return normalSec <= 86400 && deepSec <= 86400 &&
           (!normalSec || !deepSec || deepSec >= normalSec);
}
