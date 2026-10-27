# S3 power and sleep

For SkyGlass on Waveshare ESP32-S3-Touch-AMOLED-1.75 (AXP2101), including
an enclosure without an accessible power button. P4 power controls are unchanged.

## Controls

The device Settings menu has **Shut down**, **Normal sleep**, and **Deep sleep**.
The web settings link **Power, sleep and automatic timeouts** opens `/power`, with
the same actions, a **Wake up** button and two saved inactivity timers.

| Mode | Display | Aircraft checks | Wake |
| --- | --- | --- | --- |
| Awake | Normal | Running | Already awake |
| Normal sleep | Brightness zero | Running | Touch or web Wake up |
| Deep sleep | Brightness zero | Paused | Touch or web Wake up |
| Shut down | Main power off | Off | Connect USB |

Sleep keeps the touch controller, ESP32 and Wi-Fi running so touch and web access
work. "Deep sleep" here is an application mode, **not ESP32 hardware deep sleep**.
It pauses the background network worker (aircraft, weather, map, route/photo lookups,
AIS pumping and automatic update checks), parks the radar sweep and suppresses new
aircraft sounds. A request already in flight may finish; the worker parks at its
next checkpoint. Wake schedules a fresh aircraft poll without bypassing provider
rate-limit cooldowns. No claim is made for measured battery life.

The first touch wakes either sleep mode and is consumed, including a held finger,
so it cannot also select an aircraft or activate a setting. Touch-wake overrides
face-down and quiet-hours darkness for 15 seconds. Existing idle dimming/quiet hours
remain available; neither of those by itself pauses the feed.

## Automatic sleep

Both timers count from the last touch, on battery **and** USB power. Set normal sleep
to 1 minute and deep sleep to 5 minutes for a progression from screen-off to feed-off.
Each timer accepts 0-1440 whole minutes; 0 disables that stage. Both default to 0.
When both are enabled, deep must be at or after normal. Equal timeouts enter deep
directly. Saving the settings wakes the device and restarts inactivity timing.
Values are stored together in NVS (`skyglass-sleep/timeouts`) and survive reboot;
the active sleep mode itself is not persisted. Pending shutdown suspends auto-sleep.

## Shut down and packing

1. Flash using USB/web flasher and test normally. Flashing does not arm shutdown.
2. Select **Shut down** on the touchscreen (available without Wi-Fi), or on the
   normal web settings page when network access is available, then confirm.
3. With USB connected, wait for the instruction to unplug it. Unplug; firmware
   powers off after stable USB absence. On battery alone, shutdown proceeds directly.
4. Connect USB to start again. Touch cannot wake a fully powered-off ESP32.

Use this same action before shipping with the battery connected. There is no separate
shipping button or setup-completion lock. Wi-Fi and other settings are kept; do not
leave your own Wi-Fi credentials on customer units. Cancel a pending shutdown from
either interface while power remains on. Without a battery, unplugging USB directly
removes power and no pending shutdown is saved.

Shutdown state lives in `skyglass-power/state`. Old shipping states migrate to the
corresponding shutdown states. A reset while waiting for USB removal stays pending.
A USB boot after the off stage clears it; a battery-only wake reasserts power-off early.
A full flash erase clears stored settings and state.

PMIC commands use checked I2C transactions on the UI task. Failed reads never mean
USB removal. After a 1.5-second response grace and 0.5-second stable USB absence,
firmware saves the off state, clears REG12[3] (BATFET off during battery-only power-off),
then sets REG10[0]. Charging settings are unchanged. PMIC/RTC standby is not zero.

References: [Waveshare board](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75),
[AXP2101 datasheet](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf).

## Validation

Host tests cover shutdown migration, USB interlock, I2C failures, timeout boundaries,
manual and automatic sleep, touch consumption, wake timer reset and millis rollover.
Compile from the repo root in an MSVC developer prompt (create `build` first):

```bat
cl /nologo /std:c++17 /EHsc /Itests/power/stubs /Isrc tests/power/power_test.cpp src/battery.cpp /Fo:build/ /Fe:build/power-test.exe
build\power-test.exe
cl /nologo /std:c++17 /EHsc /Isrc tests/power/sleep_test.cpp /Fo:build/ /Fe:build/sleep-test.exe
build\sleep-test.exe
```

Before shipping firmware, validate on the real S3 board:

- Touch-wake from both modes; no accidental underlying button or pinch activation.
- Normal sleep continues feed polling; deep stops new polls without feed-watchdog reboots.
- Web access during sleep, manual wake, persisted timeouts and fresh polling on wake.
- Quiet-hours/face-down wake grace and no auto-sleep during pending shutdown.
- Shutdown/cancel and USB replug wake without a power button, including no/low battery.
- Off-state and both sleep currents; USB-start behaviour depends on PMIC configuration.
