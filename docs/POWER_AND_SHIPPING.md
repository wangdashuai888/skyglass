# S3 power and shipping

For SkyGlass on Waveshare ESP32-S3-Touch-AMOLED-1.75 (AXP2101), including
an enclosure without an accessible power button. P4 controls are unchanged.

## After flashing with the USB web flasher

1. Flash and test normally. A fresh flash never automatically arms shipping mode.
2. Leave the battery connected. On the device, swipe to Settings, scroll to
   **Prepare for shipping**, and confirm. Alternatively open the device web settings,
   choose **Shut down / Prepare for shipping**, and select **Prepare for shipping**.
3. Wait for the instruction to unplug USB. Unplug it; firmware powers off after
   stable USB absence. Do not pack the unit unless you verify that it switches off.
4. The customer connects USB-C to wake it and starts the normal Wi-Fi setup flow.
   Battery operation is then available. Shipping mode does not erase Wi-Fi settings;
   do not leave a customer's unit provisioned with your own network credentials.

This explicitly marks the end of factory testing. Detecting the "first USB connection"
would not work: flashing itself requires USB. Shipping state survives software resets;
an armed unit still connected to USB remains armed after a reboot. Only a USB-powered
boot after the power-off stage releases the stored off state. A battery-only wake
reasserts shutdown before the display/network start. A full flash erase clears this state.
This is not a setup-completion lock: USB wake restores normal operation even if the
customer has not yet saved Wi-Fi credentials.

## Everyday shutdown

Select **Shut down** in the device Settings or the web Power page and confirm.
On battery it powers off; with USB connected it waits for unplugging. Pending shutdown
can be cancelled from either interface. Connect USB again to wake it. Touch cannot wake
an unpowered ESP32. With no battery, unplugging USB removes power directly.

## Implementation and verification

Power requests are stored in NVS namespace `skyglass-power`, separate from user settings.
Web changes require POST plus confirmation; GET only displays status. PMIC transactions
run on the main/UI task. Failed status reads never count as USB removal. The firmware
waits at least 1.5 seconds after requesting shutdown and 0.5 seconds of stable USB
absence. It saves the off state, clears REG12[3] (BATFET off during battery-only power-off),
then sets REG10[0] (soft power-off). Charging configuration is not changed.

References: [Waveshare board](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75),
[AXP2101 datasheet, sections 6.5 and registers 0x10/0x12](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf).

Software tests cover boot-state transitions and checked PMIC writes with fake I2C.
The following require a real board before shipping this firmware:

- Both shutdown controls, cancellation, and unplug/replug wake without a power button.
- Shipping state across reboot while USB stays connected; normal wake after unplug/off.
- No-battery and low-battery behaviour, including reconnection after battery depletion.
- Measure off-state current. PMIC/RTC standby remains; this is not physical isolation.
- Verify USB insertion actually starts this board's PMIC configuration. Firmware cannot
  execute to enable USB wake after power has already been cut.

Host tests (from repo root, in an MSVC developer command prompt):

```bat
cl /nologo /std:c++17 /EHsc /Itests/power/stubs /Isrc tests/power/power_test.cpp src/battery.cpp /Fe:build/power-test.exe
build\power-test.exe
```
