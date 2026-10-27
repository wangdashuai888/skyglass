#pragma once
#include "Wire.h"
#define AXP2101_SLAVE_ADDRESS 0x34
struct XPowersPMU {
    bool begin(TwoWire&, int, int, int) { return true; }
    void enableBattDetection() {} void enableBattVoltageMeasure() {}
    bool isBatteryConnect() { return true; } int getBatteryPercent() { return 50; }
    bool isCharging() { return false; } void setALDO1Voltage(int) {} void enableALDO1() {}
};
