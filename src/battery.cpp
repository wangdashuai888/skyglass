// AXP2101 battery readout via XPowersLib. Shares the I2C bus with touch/IMU; only
// read it from the LVGL loop (core 1), not the network task.
#include "battery.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static XPowersPMU PMU;
static bool s_ok = false;

bool battery_begin() {
    s_ok = PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL);
    if (s_ok) {
        PMU.enableBattDetection();
        PMU.enableBattVoltageMeasure();
        Serial.println("[batt] AXP2101 ready");
    } else {
        Serial.println("[batt] AXP2101 not found");
    }
    return s_ok;
}

bool battery_present()  { return s_ok && PMU.isBatteryConnect(); }
int  battery_percent()  { return s_ok ? PMU.getBatteryPercent() : -1; }
bool battery_charging() { return s_ok && PMU.isCharging(); }

void battery_enable_codec_rail() {
    if (!s_ok) return;
    PMU.setALDO1Voltage(3300);   // ES8311 analog supply (the reference board enables this)
    PMU.enableALDO1();
    Serial.println("[batt] ALDO1 (codec AVDD) enabled @3.3V");
}

// Use checked transactions: a failed status read must never be treated as USB removal.
static bool pmic_read(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)AXP2101_SLAVE_ADDRESS, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}
static bool pmic_write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);
    Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
}
bool battery_usb_present(bool &present) {
    uint8_t status;
    if (!s_ok || !pmic_read(0x00, status)) return false;
    present = (status & 0x20) != 0; // VBUS_GOOD, AXP2101 datasheet REG00[5]
    return true;
}
bool battery_shutdown() {
    bool usb;
    if (!battery_usb_present(usb) || usb) return false;
    uint8_t batfet, common, verified;
    if (!pmic_read(0x12, batfet) || !pmic_read(0x10, common)) return false;
    // REG12[3]=0: disable battery-to-system FET in battery-only POWEROFF.
    // Do not change charger limits, rail voltages, or USB wake configuration.
    if (!pmic_write(0x12, batfet & ~0x08) || !pmic_read(0x12, verified) || (verified & 0x08)) return false;
    return pmic_write(0x10, common | 0x01); // soft PWROFF, main rails off
}
