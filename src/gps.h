#pragma once
// Quectel LC76G GNSS — only present on the Waveshare "-G" board variant. It sits on the
// SAME shared I2C bus as the touch/IMU/RTC, streaming NMEA over a small Quectel I2C
// protocol (write a length-query to 0x50, read the length from 0x54, then read that many
// NMEA bytes). Auto-detected: if the chip doesn't ACK, gps_present() is false and the
// rest is a no-op, so the standard board behaves exactly as before. Device-only; all the
// I2C runs in a background task with exclusive shared-bus access.
#include <stdbool.h>
#include <Arduino.h>

// Thread-safe diagnostic snapshot published by the GPS worker.
String gps_debug_snapshot();

bool gps_begin();                            // probe for the LC76G; false if absent
bool gps_present();
bool gps_start();                            // call once after all shared-bus peripherals are initialized
bool gps_has_fix();                          // a recent, valid position fix
bool gps_location(double *lat, double *lon); // last fix (false if none)
int  gps_satellites();                       // satellites in use/view (0 if none/unknown)
