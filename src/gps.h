#pragma once
// Quectel LC76G GNSS — only present on the Waveshare "-G" board variant. It sits on the
// SAME shared I2C bus as the touch/IMU/RTC, streaming NMEA over a small Quectel I2C
// protocol (write a length-query to 0x50, read the length from 0x54, then read that many
// NMEA bytes). Auto-detected: if the chip doesn't ACK, gps_present() is false and the
// rest is a no-op, so the standard board behaves exactly as before. Device-only; all the
// I2C here runs on core 1 (loop), like the other shared-bus devices.
#include <stdbool.h>
#include <Arduino.h>

// RAM-only diagnostic snapshot, read on the same loop task as GPS polling.
String gps_debug_snapshot();

bool gps_begin();                            // probe for the LC76G; false if absent
bool gps_present();
void gps_poll();                             // call every loop; I2C reads block, throttled to 1 Hz
bool gps_has_fix();                          // a recent, valid position fix
bool gps_location(double *lat, double *lon); // last fix (false if none)
int  gps_satellites();                       // satellites in use/view (0 if none/unknown)
