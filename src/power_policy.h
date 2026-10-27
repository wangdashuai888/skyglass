#pragma once
#include <stdint.h>
// Values 1/2 belonged to the former shipping action; normalize them on boot.
enum class PowerState : uint8_t { Running = 0, StopWaiting = 3, StopOff = 4 };
inline PowerState power_decode(uint8_t state) {
    if (state == 1 || state == 3) return PowerState::StopWaiting;
    if (state == 2 || state == 4) return PowerState::StopOff;
    return PowerState::Running;
}
inline bool power_waiting(PowerState s) { return s == PowerState::StopWaiting; }
inline bool power_off_state(PowerState s) { return s == PowerState::StopOff; }
inline PowerState power_boot_state(PowerState s, bool usb) {
    return power_off_state(s) && usb ? PowerState::Running : s;
}
inline PowerState power_off_target(PowerState) { return PowerState::StopOff; }
