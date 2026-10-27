#include <assert.h>
#include <stdio.h>
#include "power_policy.h"
#include "battery.h"
#include "Wire.h"
int main() {
    using S = PowerState;
    assert(power_decode(0) == S::Running);
    assert(power_decode(1) == S::StopWaiting); // migrate formerly armed shipping mode
    assert(power_decode(2) == S::StopOff);
    assert(power_decode(255) == S::Running);
    assert(power_boot_state(S::Running, true) == S::Running);
    assert(power_boot_state(S::StopWaiting, true) == S::StopWaiting);
    assert(power_boot_state(S::StopOff, false) == S::StopOff);
    assert(power_boot_state(S::StopOff, true) == S::Running);
    assert(power_off_target(S::StopWaiting) == S::StopOff);
    bool usb = true;
    assert(!battery_usb_present(usb)); // no PMIC init -> no shutdown
    assert(!battery_shutdown());
    assert(battery_begin());
    Wire.regs[0] = 0x20;
    assert(battery_usb_present(usb) && usb);
    assert(!battery_shutdown() && Wire.writes == 0); // never cut rails with USB present
    Wire.failRead = true;
    assert(!battery_usb_present(usb));
    assert(!battery_shutdown() && Wire.writes == 0); // I2C failure cannot mean unplugged
    Wire.failRead = false; Wire.regs[0] = 0;
    Wire.regs[0x12] = 0xAB; Wire.regs[0x10] = 0xA0;
    Wire.failWrite = true;
    assert(!battery_shutdown() && Wire.regs[0x10] == 0xA0);
    Wire.failWrite = false;
    assert(battery_shutdown());
    assert(Wire.regs[0x12] == 0xA3); // only off-state BATFET bit changed
    assert(Wire.regs[0x10] == 0xA1); // soft power-off; other configuration preserved
    puts("PASS: shutdown boot states and legacy migration, USB interlock, I2C failures, PMIC register writes");
}
