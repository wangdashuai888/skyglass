#include "firmware_log.h"
// LC76G GNSS over the shared I2C bus (Quectel I2C-NMEA protocol).
// Verified on the ESP32-S3-Touch-AMOLED-1.75-G. What this protocol needed (figured out from
// Waveshare's RP2350 `04_GPS_I2C` demo + on-hardware probing):
//   - a ~100 ms settle between EVERY step — crucially BETWEEN reading the queued length and
//     writing the read-data command, or the module NACKs that write (endTransmission == 2);
//   - the read-data command echoes the byte count you then read;
//   - the worker holds the shared-bus mutex for the whole protocol exchange;
//     UI bus clients defer their reads while rendering continues;
//   - 100 kHz + a longer I2C timeout, because the module clock-stretches while preparing data.
// MEMORY: the Wire RX buffer is sized ONCE at begin and never resized afterwards — calling
// Wire.setBufferSize() at runtime fragments the internal heap and kills the mbedTLS handshake
// (the ADS-B HTTPS feed). So we cap each read to one bufferful and let the rest come next poll.
#include "gps.h"
#include "config.h"
#include <Arduino.h>
#include <TinyGPS++.h>
#include "gps_retry.h"
#include "shared_i2c.h"

static bool parser_has_fix();
static uint32_t s_lastByteMs = 0;
static bool s_received = false;
static uint32_t s_recoveryAttempts = 0;
static uint32_t s_resetAttempts = 0, s_resetSuccesses = 0;
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

bool parser_begin() {
    s_ser.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    // Presence is decided by whether NMEA actually turns up, not by the pins existing --
    // a board with nothing plugged into the header must report "no GPS", not "broken GPS".
    // The module emits sentences within a second or two of power-up even without a fix,
    // so parser_poll() promotes s_present the moment it parses a valid one.
    FirmwareLog.printf("[gps] UART%d listening on RX=%d TX=%d @ %d baud\n",
                  (int)GPS_UART_NUM, (int)PIN_GPS_RX, (int)PIN_GPS_TX, (int)GPS_BAUD);
    return true;
}

bool parser_present() { return s_present; }

void parser_poll() {
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
                      (int)parser_has_fix());
    }
}

static bool parser_has_fix() {
    return s_gps.location.isValid() && s_gps.location.age() < GPS_FIX_TTL_MS;
}

bool parser_location(double *lat, double *lon) {
    if (!parser_has_fix()) return false;
    if (lat) *lat = s_gps.location.lat();
    if (lon) *lon = s_gps.location.lng();
    return true;
}

int parser_satellites() {
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
static GpsRetry s_retry;
static GpsResetPolicy s_resetPolicy;
static uint8_t s_recoveryReads = 0;

static bool        s_present = false;
static TinyGPSPlus s_gps;

// Waveshare -G: TCA9554 (0x20) P7 -> LC76G RESET_N.
// Reset is pulled up inside the LC76G to 1.8 V: drive LOW, then release
// to input/high-impedance. Never drive a 3.3 V high onto the reset pin.
static bool expanderRead(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(0x20); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)0x20, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}
static bool expanderWrite(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(0x20); Wire.write(reg); Wire.write(value);
    return Wire.endTransmission() == 0;
}
static bool gpsResetPulse() {
    // Caller owns the shared bus for the complete pulse/release sequence.
    uint8_t config, output;
    if (!expanderRead(3, config) || !expanderRead(1, output)) {
        debugEvent("GPS_RST failed: cannot read TCA9554 registers");
        return false;
    }
    // Preserve all seven unrelated pins. Set the output latch low before enabling output.
    if (!expanderWrite(1, output & 0x7f)) {
        debugEvent("GPS_RST failed: cannot prepare low output latch");
        return false;
    }
    const bool asserted = expanderWrite(3, config & 0x7f);
    // Even an uncertain ACK must be followed by release: the write may have reached the chip.
    delay(150);
    bool released = false;
    for (int attempt = 0; attempt < 3 && !released; ++attempt) {
        uint8_t verified;
        released = expanderWrite(3, config | 0x80) &&
                   expanderRead(3, verified) && verified == uint8_t(config | 0x80);
        if (!released) delay(10);
    }
    if (released) expanderWrite(1, output); // harmless while P7 is high-impedance
    if (!released) {
        debugEvent("GPS_RST ERROR: release could not be verified; check receiver/reset state");
        return false;
    }
    if (!asserted) {
        debugEvent("GPS_RST: released safely, but low assertion was not acknowledged");
        return false;
    }
    debugEvent("GPS_RST: 150ms low pulse complete; reset pin released to input");
    return true;
}

// One blocking, uninterrupted drain of up to GPS_READ_MAX bytes into the parser.
// Returns bytes read (0 if nothing queued or a step failed). Caller has set 100 kHz + long timeout.
static int gps_drain() {
    const uint32_t settle = s_recoveryReads ? 200 : GPS_SETTLE_MS;
    // 0) un-stick the module: other shared-bus traffic (LVGL polls the touch IC continuously
    //    between our calls) can leave the LC76G mid-response and unable to answer at 0x50; a
    //    stray 1-byte read at 0x54/0x58 kicks it back to normal (Quectel quirk). 1-byte reads,
    //    no extra RAM.
    Wire.requestFrom((uint8_t)GPS_ADDR_R, (uint8_t)1); while (Wire.available()) Wire.read();
    Wire.requestFrom((uint8_t)0x58,        (uint8_t)1); while (Wire.available()) Wire.read();

    // 1) query how many bytes are queued — retry, since prior shared-bus traffic can briefly
    //    leave the module unable to answer the first attempt.
    uint32_t avail = 0;
    bool gotLength = false;
    uint8_t queryError = 0;
    int lengthBytes = 0;
    for (int t = 0; t < 6; ++t) {
        Wire.beginTransmission(GPS_ADDR_W); Wire.write(QLEN, sizeof(QLEN));
        queryError = Wire.endTransmission();
        if (queryError != 0) { delay(15); continue; }
        delay(settle);
        lengthBytes = Wire.requestFrom((uint8_t)GPS_ADDR_R, (uint8_t)4);
        if (lengthBytes == 4) {
            avail  = (uint32_t)Wire.read();
            avail |= (uint32_t)Wire.read() << 8;
            avail |= (uint32_t)Wire.read() << 16;
            avail |= (uint32_t)Wire.read() << 24;
            gotLength = true;
            break;
        }
        delay(15);
    }
    if (!gotLength || avail == 0 || avail > 200000) {
        FirmwareLog.printf("[gps %lus] length: valid=%d queued=%lu writeError=%u lengthBytes=%d recovery=%u\n",
            (unsigned long)(millis()/1000), gotLength, (unsigned long)avail,
            queryError, lengthBytes, s_recoveryReads);
        return 0;
    }

    // 2) ask for at most one bufferful; the command echoes the count we will read.
    const uint32_t limit = s_recoveryReads ? 512 : GPS_READ_MAX;
    const uint32_t want = avail < limit ? avail : limit;
    const uint8_t cmd[8] = { 0x00, 0x20, 0x51, 0xAA,
                             (uint8_t)want, (uint8_t)(want >> 8),
                             (uint8_t)(want >> 16), (uint8_t)(want >> 24) };
    delay(settle);                                    // without this gap the cmd write gets NACKed
    Wire.beginTransmission(GPS_ADDR_W); Wire.write(cmd, sizeof(cmd));
    const uint8_t readError = Wire.endTransmission();
    if (readError != 0) {
        FirmwareLog.printf("[gps] read command failed: error=%u requested=%lu\n", readError, (unsigned long)want);
        return 0;
    }

    // 3) read straight into the NMEA parser (no Wire.setBufferSize here — see file header)
    delay(settle);
    const int got = Wire.requestFrom((uint8_t)GPS_ADDR_R, (size_t)want);
    if (got != (int)want) debugEvent("I2C: short data read");
    for (int i = 0; i < got; ++i) receiveChar(s_gps, (char)Wire.read());
    return got;
}

bool parser_begin() {
    // probe with the default buffer first; only bump it on a real -G board (and only once,
    // here at boot before TLS comes up) so non-GPS boards keep their small internal footprint.
    Wire.setClock(GPS_I2C_HZ); Wire.setTimeOut(GPS_TIMEOUT_MS);
    for (int attempt = 0; attempt < 3; ++attempt) {
        Wire.requestFrom((uint8_t)GPS_ADDR_R, (uint8_t)1);
        while (Wire.available()) Wire.read();
        Wire.beginTransmission(GPS_ADDR_W);
        Wire.write(QLEN, sizeof(QLEN));
        s_present = (Wire.endTransmission() == 0);
        if (s_present) break;
        delay(GPS_SETTLE_MS);
    }
    Wire.setTimeOut(BUS_TIMEOUT_MS); Wire.setClock(GPS_BUS_HZ);
    if (s_present) Wire.setBufferSize(GPS_BUF);       // sized ONCE; never resized at runtime
    FirmwareLog.printf("[gps] LC76G %s\n", s_present ? "detected on I2C" : "not present");
    debugEvent(s_present ? "LC76G detected on I2C" : "LC76G not detected on I2C");
    s_retry.start(millis());
    s_resetPolicy.start(millis());
    return s_present;
}

bool parser_present() { return s_present; }

void parser_poll() {
    if (!s_present) return;
    SharedI2cGuard bus;
    if (!bus) return;
    static uint32_t last = 0;
    const uint32_t now = millis();
    if (last != 0 && now - last < GPS_POLL_MS) return;
    last = now;

    if (s_resetPolicy.due(now)) {
        s_resetPolicy.attempted(now);
        ++s_resetAttempts;
        FirmwareLog.printf("[gps %lus] GPS_RST attempt %lu: no valid NMEA for 30s (60s reset cooldown)\n",
            (unsigned long)(now / 1000), (unsigned long)s_resetAttempts);
        Wire.setClock(GPS_I2C_HZ); Wire.setTimeOut(BUS_TIMEOUT_MS);
        const bool resetOk = gpsResetPulse();
        Wire.setTimeOut(BUS_TIMEOUT_MS); Wire.setClock(GPS_BUS_HZ);
        if (resetOk) {
            ++s_resetSuccesses;
            s_gps = TinyGPSPlus(); // discard pre-reset partial sentences and stale position
            s_received = false; s_lastByteMs = 0;
            s_recoveryReads = 0;
            s_retry.start(millis());
        }
        // Give the receiver until the next one-second poll to restart; UI task stays free.
        last = millis();
        return;
    }
    if (s_retry.due(now)) {
        s_retry.attempted(now);
        ++s_recoveryAttempts;
        s_recoveryReads = 5;
        FirmwareLog.printf("[gps %lus] recovery attempt %lu: no valid NMEA for 15s; trying 50kHz, longer settling and smaller reads\n",
            (unsigned long)(now/1000), (unsigned long)s_recoveryAttempts);
    }
    const uint32_t validBefore = s_gps.passedChecksum();
    Wire.setClock(s_recoveryReads ? 50000 : GPS_I2C_HZ); Wire.setTimeOut(GPS_TIMEOUT_MS);
    gps_drain();
    Wire.setTimeOut(BUS_TIMEOUT_MS); Wire.setClock(GPS_BUS_HZ);   // hand the shared bus back to touch/IMU/RTC/PMIC
    if (s_gps.passedChecksum() != validBefore) {
        s_retry.received(millis());
        s_resetPolicy.received(millis());
        if (s_recoveryReads) debugEvent("valid NMEA resumed; returning to normal 1-second reads");
        s_recoveryReads = 0;
    } else if (s_recoveryReads) {
        --s_recoveryReads;
        if (!s_recoveryReads) debugEvent("recovery window ended; will retry if valid data remains absent");
    }

    // Diagnostic ladder (every ~8 s): chars=0 -> no NMEA arriving; sent>0 but fix=0 -> valid
    // data, just no satellite lock yet (needs clear sky / a few min on a cold start).
    static uint32_t lg = 0;
    if (now - lg > 8000) { lg = now;
        FirmwareLog.printf("[gps] chars=%lu sent=%lu csErr=%lu sats=%d fix=%d\n",
                      (unsigned long)s_gps.charsProcessed(), (unsigned long)s_gps.sentencesWithFix(),
                      (unsigned long)s_gps.failedChecksum(), (int)s_gps.satellites.value(),
                      (int)parser_has_fix());
    }
}

static bool parser_has_fix() {
    return s_gps.location.isValid() && s_gps.location.age() < GPS_FIX_TTL_MS;
}

bool parser_location(double *lat, double *lon) {
    if (!parser_has_fix()) return false;
    if (lat) *lat = s_gps.location.lat();
    if (lon) *lon = s_gps.location.lng();
    return true;
}

int parser_satellites() {
    return s_gps.satellites.isValid() ? (int)s_gps.satellites.value() : 0;
}

#endif  // PIN_GPS_RX

// Parser and diagnostic counters are owned solely by the GPS task after startup.
// Publish a small value snapshot; readers never wait for a GPS transaction.
struct GpsSnapshot {
    bool present = false, valid = false, received = false;
    uint32_t captured = 0, positionAge = UINT32_MAX, lastByte = 0;
    uint32_t chars = 0, passed = 0, failed = 0, fixes = 0, retries = 0;
    uint32_t resets = 0, resetPulses = 0;
    int satellites = 0;
    double lat = 0, lon = 0;
};
static GpsSnapshot s_snapshot;
static portMUX_TYPE s_snapshotMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_worker = nullptr;

static void publishSnapshot() {
    GpsSnapshot next;
    next.present = parser_present();
    next.valid = s_gps.location.isValid();
    next.received = s_received;
    next.captured = millis();
    next.positionAge = s_gps.location.age();
    next.lastByte = s_lastByteMs;
    next.chars = s_gps.charsProcessed(); next.passed = s_gps.passedChecksum();
    next.failed = s_gps.failedChecksum(); next.fixes = s_gps.sentencesWithFix();
    next.retries = s_recoveryAttempts;
    next.resets = s_resetAttempts; next.resetPulses = s_resetSuccesses;
    next.satellites = parser_satellites();
    next.lat = s_gps.location.lat(); next.lon = s_gps.location.lng();
    portENTER_CRITICAL(&s_snapshotMux);
    s_snapshot = next;
    portEXIT_CRITICAL(&s_snapshotMux);
}
static GpsSnapshot readSnapshot() {
    portENTER_CRITICAL(&s_snapshotMux);
    const GpsSnapshot result = s_snapshot;
    portEXIT_CRITICAL(&s_snapshotMux);
    return result;
}
static uint64_t positionAge(const GpsSnapshot &s) {
    return s.valid ? uint64_t(s.positionAge) + uint32_t(millis() - s.captured) : UINT32_MAX;
}

bool gps_begin() {
    const bool ok = parser_begin();
    publishSnapshot();
    return ok;
}
bool gps_start() {
    if (s_worker) return true;
#if !defined(PIN_GPS_RX) || (PIN_GPS_RX < 0)
    if (!gps_present()) return false;
#endif
    if (!shared_i2c_init()) return false;
    const BaseType_t result = xTaskCreatePinnedToCore([](void *) {
        for (;;) {
            parser_poll();
            publishSnapshot();
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }, "gps", 6144, nullptr, 1, &s_worker, 0);
    if (result != pdPASS) { s_worker = nullptr; return false; }
    FirmwareLog.println("[gps] background task started on core 0");
    return true;
}
bool gps_present() { return readSnapshot().present; }
bool gps_has_fix() { const auto s = readSnapshot(); return s.valid && positionAge(s) < GPS_FIX_TTL_MS; }
bool gps_location(double *lat, double *lon) {
    const auto s = readSnapshot();
    if (!s.valid || positionAge(s) >= GPS_FIX_TTL_MS) return false;
    if (lat) *lat = s.lat;
    if (lon) *lon = s.lon;
    return true;
}
int gps_satellites() { return readSnapshot().satellites; }
String gps_debug_snapshot() {
    const auto s = readSnapshot();
    char stats[768];
    snprintf(stats, sizeof(stats),
        "SkyGlass %s | uptime: %lus\nGPS worker: %s | detected: %s | recent fix: %s\n"
        "Characters: %lu | valid checksums: %lu | failed checksums: %lu\n"
        "Sentences with fix: %lu | satellites used: %d\n"
        "Data received: %s | last byte age: %lu ms | position age: %llu ms\n"
        "Position (last parsed): %.6f, %.6f\nRecovery attempts: %lu\n",
        FW_VERSION, (unsigned long)(millis() / 1000), s_worker ? "running" : "not running",
        s.present ? "yes" : "no", s.valid && positionAge(s) < GPS_FIX_TTL_MS ? "yes" : "no",
        (unsigned long)s.chars, (unsigned long)s.passed, (unsigned long)s.failed,
        (unsigned long)s.fixes, s.satellites, s.received ? "yes" : "no",
        s.received ? (unsigned long)(millis() - s.lastByte) : 0UL,
        (unsigned long long)positionAge(s), s.lat, s.lon, (unsigned long)s.retries);
    return String(stats) + "GPS_RST attempts: " + String(s.resets) +
           " | completed pulses: " + String(s.resetPulses) + "\n";
}
