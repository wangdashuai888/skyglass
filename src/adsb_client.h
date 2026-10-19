#pragma once
// Fetches nearby aircraft from local receivers or airplanes.live / adsb.fi / adsb.lol and parses
// the readsb JSON into a vector<Aircraft>. See docs/DATA_SOURCE.md.
#include <vector>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "aircraft.h"
#include "adsb_pacing.h"

class AdsbClient {
public:
    // What the feed is actually being asked for. Exposed because the displayed centre and
    // the queried centre are two different things, and when they disagree the scope looks
    // empty for no visible reason -- /diag reports both so the disagreement is checkable.
    double queryLat() const { return _lat; }
    double queryLon() const { return _lon; }
    void begin(double homeLat, double homeLon, float rangeKm);
    void setHome(double lat, double lon) { _lat = lat; _lon = lon; }
    void setRange(float km) { _rangeKm = km; }
    void setHideGround(bool h) { _hideGround = h; }   // skip on-ground aircraft during parse
    void setMinAltFt(float ft) { _minAltFt = ft; }    // skip aircraft below this altitude (0 = off)
    void setMilitaryOnly(bool m) { _milOnly = m; }    // keep only military-flagged aircraft

    // A dump1090/readsb/tar1090 receiver on the local network. When set it is polled
    // first and the public APIs become the fallback -- your own antenna, no internet.
    // Empty string disables it.
    void setLocalHost(const char* h) { _localHost = h ? h : ""; }
    // 0 = auto (local first, public feeds if it does not answer)
    // 1 = local only  -- shows exactly what your antenna hears, never masked by the APIs
    // 2 = public only -- ignore the receiver even when it is up
    void setSourceMode(int m) { _srcMode = m; }
    int  sourceMode() const { return _srcMode; }
    const char* localHost() const { return _localHost.c_str(); }
    bool lastPollWasLocal() const { return _lastWasLocal; }

    // Fetch + parse. Returns true on success and fills `out` (replaces contents).
    // On failure, leaves `out` untouched and returns false (caller keeps last good).
    bool poll(std::vector<Aircraft>& out);

    uint32_t lastOkMs() const { return _lastOkMs; }
    bool lastPollSkipped() const { return _lastPollSkipped; }
    uint32_t lastResponseMs() const { return _lastResponseMs; }
    const char* lastHost() const { return _lastHost.c_str(); }
    // Local-only mode must never inherit public-provider cooldowns.
    bool allParked() const { return _srcMode != 1 && _pacer.allParked(millis()); }

private:
    // One host, one attempt. `local` swaps the HTTPS point-query for a plain-HTTP
    // fetch of the receiver's aircraft.json.
    bool fetchFrom(const char* host, std::vector<Aircraft>& out, bool local = false, int slot = -1);

    double _lat = 0, _lon = 0;
    float  _rangeKm = 15.0f;
    bool   _hideGround = false;
    float  _minAltFt = 0.0f;
    bool   _milOnly = false;
    uint32_t _lastOkMs = 0;

    AdsbPacer _pacer;
    bool _lastPollSkipped = false;
    uint32_t _lastResponseMs = 0;
    String _lastHost;
    String _localHost;
    int    _srcMode = 0;
    bool   _lastWasLocal = false;
};
