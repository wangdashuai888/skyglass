#include "firmware_log.h"
// LC76G GNSS over the shared I2C bus (Quectel I2C-NMEA protocol).
// Verified on the ESP32-S3-Touch-AMOLED-1.75-G. What this protocol needed (figured out from
// Waveshare's RP2350 `04_GPS_I2C` demo + on-hardware probing):
//   - a ~100 ms settle between EVERY step — crucially BETWEEN reading the queued length and
//     writing the read-data command, or the module NACKs that write (endTransmission == 2);
//   - the read-data command echoes the byte count you then read;
//   - the whole transaction must run uninterrupted, so it's one blocking call (nothing else
//     on core 1 runs meanwhile);
//   - 100 kHz + a longer I2C timeout, because the module clock-stretches while preparing data.
// MEMORY: the Wire RX buffer is sized ONCE at begin and never resized afterwards — calling
// Wire.setBufferSize() at runtime fragments the internal heap and kills the mbedTLS handshake
// (the ADS-B HTTPS feed). So we cap each read to one bufferful and let the rest come next poll.
#include "gps.h"
#include "config.h"
#include <Arduino.h>
#include <TinyGPS++.h>

static uint32_t s_lastByteMs = 0;
static bool s_received = false;
static void debugEvent(const char *message) {
    FirmwareLog.printf("[gps %lus] %s\n", (unsigned long)(millis() / 1000), message);
}
static void receiveChar(TinyGPSPlus &gps, char c) {
    s_received = true;
    s_lastByteMs = millis();
    // Buffer one raw sentence to avoid taking a cross-core log lock for every byte.
    static char sentence[160];
    static size_t used = 0;
    if (FirmwareLog.enabled()) {
        if (c == '$') used = 0;
        sentence[used++] = c;
        if (c == '\n' || used == sizeof(sentence)) {
            FirmwareLog.write((const uint8_t *)sentence, used);
            used = 0;
        }
    } else used = 0;
    gps.encode(c);
}

// ---------------------------------------------------------------------------------
// Two backends behind one API. A board defines PIN_GPS_RX when it has a plain NMEA
// module on a UART; otherwise it gets the LC76G-over-I2C path below, which is specific
// to the Waveshare "-G" variant. Everything above this file -- the config-page toggle,
// the centre-point logic -- is identical either way.
// ---------------------------------------------------------------------------------
#if defined(PIN_GPS_RX) && (PIN_GPS_RX >= 0)

#define GPS_FIX_TTL_MS  70000      // how long a fix stays "valid" after the last sentence

static bool        s_present = false;
static TinyGPSPlus s_gps;
static HardwareSerial s_ser(GPS_UART_NUM);

bool gps_begin() {
    s_ser.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    // Presence is decided by whether NMEA actually turns up, not by the pins existing --
    // a board with nothing plugged into the header must report "no GPS", not "broken GPS".
    // The module emits sentences within a second or two of power-up even without a fix,
    // so gps_poll() promotes s_present the moment it parses a valid one.
    FirmwareLog.printf("[gps] UART%d listening on RX=%d TX=%d @ %d baud\n",
                  (int)GPS_UART_NUM, (int)PIN_GPS_RX, (int)PIN_GPS_TX, (int)GPS_BAUD);
    return true;
}

bool gps_present() { return s_present; }

void gps_poll() {
    // Non-blocking: drain whatever arrived since the last loop. At 9600 baud that is about
    // one sentence per call, which costs nothing on the render core.
    int guard = 512;                       // never let a noisy line monopolise the loop
    while (s_ser.available() && guard-- > 0) receiveChar(s_gps, (char)s_ser.read());

    // Present = at least one complete, checksum-valid sentence has been parsed. That is
    // true well before there is a position fix, which is what we want: the screen should
    // say "acquiring" rather than "no GPS" while the module is still finding satellites.
    if (!s_present && s_gps.passedChecksum() > 0) {
        s_present = true;
        FirmwareLog.println("[gps] NMEA detected");
    }

    static uint32_t lg = 0;
    const uint32_t now = millis();
    if (now - lg > 8000) { lg = now;
        // Same diagnostic ladder as the I2C path: chars=0 means nothing is arriving (check
        // the wiring or swap TX/RX); chars>0 with fix=0 means valid data but no lock yet.
        FirmwareLog.printf("[gps] chars=%lu sent=%lu csErr=%lu sats=%d fix=%d\n",
                      (unsigned long)s_gps.charsProcessed(), (unsigned long)s_gps.sentencesWithFix(),
                      (unsigned long)s_gps.failedChecksum(), (int)s_gps.satellites.value(),
                      (int)gps_has_fix());
    }
}

bool gps_has_fix() {
    return s_gps.location.isValid() && s_gps.location.age() < GPS_FIX_TTL_MS;
}

bool gps_location(double *lat, double *lon) {
    if (!gps_has_fix()) return false;
    if (lat) *lat = s_gps.location.lat();
    if (lon) *lon = s_gps.location.lng();
    return true;
}

int gps_satellites() {
    return s_gps.satellites.isValid() ? (int)s_gps.satellites.value() : 0;
}

#else   // ---------------- LC76G over the shared I2C bus (-G board variant) -------------

#include <Wire.h>

#define GPS_ADDR_W      0x50
#define GPS_ADDR_R      0x54
#define GPS_SETTLE_MS   100        // settle between each protocol step (LC76G is fussy)
#define GPS_READ_MAX    4096       // drain the NMEA queue before it backs up (upstream issue #24)
#define GPS_BUF         4096       // allocate once at startup, before TLS; never resize at runtime
#define GPS_POLL_MS     1000       // keep draining every second, including after acquiring a fix
#define GPS_FIX_TTL_MS  70000      // maximum age of a usable position
#define GPS_I2C_HZ      100000     // the read protocol is unreliable at the 400 kHz bus default
#define GPS_BUS_HZ      400000     // restore the shared bus to this after each GPS transaction
#define GPS_TIMEOUT_MS  1000       // the module clock-stretches while preparing data; 50 ms (default) times out
#define BUS_TIMEOUT_MS  50         // restore the snappy default for touch/IMU/etc.

static const uint8_t QLEN[8] = { 0x08, 0x00, 0x51, 0xAA, 0x04, 0x00, 0x00, 0x00 };  // "how much is queued?"

static bool        s_present = false;
static TinyGPSPlus s_gps;

// One blocking, uninterrupted drain of up to GPS_READ_MAX bytes into the parser.
// Returns bytes read (0 if nothing queued or a step failed). Caller has set 100 kHz + long timeout.
static int gps_drain() {
    // 0) un-stick the module: other shared-bus traffic (LVGL polls the touch IC continuously
    //    between our calls) can leave the LC76G mid-response and unable to answer at 0x50; a
    //    stray 1-byte read at 0x54/0x58 kicks it back to normal (Quectel quirk). 1-byte reads,
    //    no extra RAM.
    Wire.requestFrom((uint8_t)GPS_ADDR_R, (uint8_t)1); while (Wire.available()) Wire.read();
    Wire.requestFrom((uint8_t)0x58,        (uint8_t)1); while (Wire.available()) Wire.read();

    // 1) query how many bytes are queued — retry, since prior shared-bus traffic can briefly
    //    leave the module unable to answer the first attempt.
    uint32_t avail = 0;
    for (int t = 0; t < 6; ++t) {
        Wire.beginTransmission(GPS_ADDR_W); Wire.write(QLEN, sizeof(QLEN));
        if (Wire.endTransmission() != 0) { delay(15); continue; }
        delay(GPS_SETTLE_MS);
        if (Wire.requestFrom((uint8_t)GPS_ADDR_R, (uint8_t)4) == 4) {
            avail  = (uint32_t)Wire.read();
            avail |= (uint32_t)Wire.read() << 8;
            avail |= (uint32_t)Wire.read() << 16;
            avail |= (uint32_t)Wire.read() << 24;
            break;
        }
        delay(15);
    }
    if (avail == 0 || avail > 200000) {
        debugEvent("I2C: no queued bytes, failed length query, or invalid length");
        return 0;
    }

    // 2) ask for at most one bufferful; the command echoes the count we will read.
    const uint32_t want = avail < GPS_READ_MAX ? avail : GPS_READ_MAX;
    const uint8_t cmd[8] = { 0x00, 0x20, 0x51, 0xAA,
                             (uint8_t)want, (uint8_t)(want >> 8),
                             (uint8_t)(want >> 16), (uint8_t)(want >> 24) };
    delay(GPS_SETTLE_MS);                              // <-- without this gap the cmd write gets NACKed
    Wire.beginTransmission(GPS_ADDR_W); Wire.write(cmd, sizeof(cmd));
    if (Wire.endTransmission() != 0) {
        debugEvent("I2C: read-data command failed");
        return 0;
    }

    // 3) read straight into the NMEA parser (no Wire.setBufferSize here — see file header)
    delay(GPS_SETTLE_MS);
    const int got = Wire.requestFrom((uint8_t)GPS_ADDR_R, (size_t)want);
    if (got != (int)want) debugEvent("I2C: short data read");
    for (int i = 0; i < got; ++i) receiveChar(s_gps, (char)Wire.read());
    return got;
}

bool gps_begin() {
    // probe with the default buffer first; only bump it on a real -G board (and only once,
    // here at boot before TLS comes up) so non-GPS boards keep their small internal footprint.
    Wire.beginTransmission(GPS_ADDR_W);
    Wire.write(QLEN, sizeof(QLEN));
    s_present = (Wire.endTransmission() == 0);        // 0x50 ACKs only on the -G variant
    if (s_present) Wire.setBufferSize(GPS_BUF);       // sized ONCE; never resized at runtime
    FirmwareLog.printf("[gps] LC76G %s\n", s_present ? "detected on I2C" : "not present");
    debugEvent(s_present ? "LC76G detected on I2C" : "LC76G not detected on I2C");
    return s_present;
}

bool gps_present() { return s_present; }

void gps_poll() {
    if (!s_present) return;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (last != 0 && now - last < GPS_POLL_MS) return;
    last = now;

    Wire.setClock(GPS_I2C_HZ); Wire.setTimeOut(GPS_TIMEOUT_MS);   // LC76G read path: 100 kHz + tolerate clock-stretch
    gps_drain();
    Wire.setTimeOut(BUS_TIMEOUT_MS); Wire.setClock(GPS_BUS_HZ);   // hand the shared bus back to touch/IMU/RTC/PMIC

    // Diagnostic ladder (every ~8 s): chars=0 -> no NMEA arriving; sent>0 but fix=0 -> valid
    // data, just no satellite lock yet (needs clear sky / a few min on a cold start).
    static uint32_t lg = 0;
    if (now - lg > 8000) { lg = now;
        FirmwareLog.printf("[gps] chars=%lu sent=%lu csErr=%lu sats=%d fix=%d\n",
                      (unsigned long)s_gps.charsProcessed(), (unsigned long)s_gps.sentencesWithFix(),
                      (unsigned long)s_gps.failedChecksum(), (int)s_gps.satellites.value(),
                      (int)gps_has_fix());
    }
}

bool gps_has_fix() {
    return s_gps.location.isValid() && s_gps.location.age() < GPS_FIX_TTL_MS;
}

bool gps_location(double *lat, double *lon) {
    if (!gps_has_fix()) return false;
    if (lat) *lat = s_gps.location.lat();
    if (lon) *lon = s_gps.location.lng();
    return true;
}

int gps_satellites() {
    return s_gps.satellites.isValid() ? (int)s_gps.satellites.value() : 0;
}

#endif  // PIN_GPS_RX

String gps_debug_snapshot() {
    char stats[640];
    snprintf(stats, sizeof(stats),
        "SkyGlass %s | uptime: %lus\nGPS detected: %s | recent fix: %s\n"
        "Characters: %lu | valid checksums: %lu | failed checksums: %lu\n"
        "Sentences with fix: %lu | satellites used: %d\n"
        "Data received: %s | last byte age: %lu ms | position age: %lu ms\n"
        "Position (last parsed): %.6f, %.6f\n",
        FW_VERSION, (unsigned long)(millis() / 1000),
        gps_present() ? "yes" : "no", gps_has_fix() ? "yes" : "no",
        (unsigned long)s_gps.charsProcessed(), (unsigned long)s_gps.passedChecksum(),
        (unsigned long)s_gps.failedChecksum(), (unsigned long)s_gps.sentencesWithFix(),
        gps_satellites(), s_received ? "yes" : "no",
        s_received ? (unsigned long)(millis() - s_lastByteMs) : 0UL,
        (unsigned long)s_gps.location.age(), s_gps.location.lat(), s_gps.location.lng());
    return String(stats);
}
