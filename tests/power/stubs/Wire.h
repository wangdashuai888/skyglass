#pragma once
#include <stdint.h>
struct TwoWire {
    uint8_t regs[256] = {}; int reg = 0, writes = 0, count = 0; uint8_t value = 0;
    bool failRead = false, failWrite = false;
    void beginTransmission(uint8_t) { count = 0; }
    void write(uint8_t v) { if (count++ == 0) reg = v; else value = v; }
    int endTransmission(bool = true) {
        if (count > 1) { if (failWrite) return 4; regs[reg] = value; ++writes; }
        return 0;
    }
    int requestFrom(uint8_t, uint8_t) { return failRead ? 0 : 1; }
    int read() { return regs[reg]; }
};
inline TwoWire Wire;
