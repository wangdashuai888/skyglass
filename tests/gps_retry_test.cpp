#include "../src/gps_retry.h"
#include <cassert>
int main() {
    GpsRetry retry;
    retry.start(1000);
    assert(!retry.due(15999));
    assert(retry.due(16000));
    retry.attempted(16000);
    assert(!retry.due(16001));
    assert(retry.due(31000));
    retry.received(31000);
    assert(!retry.due(45999));
    assert(retry.due(46000));
    // A stream of no-fix but valid NMEA messages keeps recovery inactive.
    for (unsigned t = 46000; t < 100000; t += 1000) {
        retry.received(t); assert(!retry.due(t));
    }
    retry.start(0xfffffff0u);
    assert(!retry.due(0x10u));
    assert(retry.due(uint32_t(0xfffffff0u + 15000u)));
}
