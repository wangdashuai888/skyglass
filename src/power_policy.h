#pragma once
#include <stdint.h>

// Persisted separately from user settings. No automatic shipping state on flashing:
// factory programming and QA use USB too. Explicitly arm only after final testing.
enum class PowerState : uint8_t { Running = 0, ShipWaiting = 1, ShipOff = 2, StopWaiting = 3, StopOff = 4 };
inline bool power_waiting(PowerState s) { return s == PowerState::ShipWaiting || s == PowerState::StopWaiting; }
inline bool power_off_state(PowerState s) { return s == PowerState::ShipOff || s == PowerState::StopOff; }
inline PowerState power_boot_state(PowerState s, bool usb) {
    if (power_off_state(s) && usb) return PowerState::Running;
    return s;
}
inline PowerState power_off_target(PowerState s) {
    return s == PowerState::ShipWaiting ? PowerState::ShipOff : PowerState::StopOff;
}
