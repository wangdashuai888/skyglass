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
    GpsResetPolicy reset;
    reset.start(1000);
    assert(!reset.due(30999)); assert(reset.due(31000));
    reset.attempted(31000);
    assert(!reset.due(90999)); assert(reset.due(91000));
    reset.received(91000); assert(!reset.due(120999));
    assert(reset.due(121000));
    reset.start(0xfffffff0u);
    assert(!reset.due(0x10u));
    assert(reset.due(uint32_t(0xfffffff0u + 30000u)));
    for (unsigned t = 0; t < 200000; t += 1000) {
        reset.received(t); assert(!reset.due(t));
    }
}
