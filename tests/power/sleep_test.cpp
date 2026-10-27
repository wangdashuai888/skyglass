#include <assert.h>
#include <stdio.h>
#include "sleep_policy.h"
int main() {
    using M = SleepMode;
    SleepPolicy s;
    s.wake(100);
    s.tick(10000000); assert(s.mode == M::Awake); // disabled defaults
    s.normalMs = 60000; s.deepMs = 300000; s.wake(100);
    s.tick(60099); assert(s.mode == M::Awake);
    s.tick(60100); assert(s.mode == M::Normal);
    s.tick(300099); assert(s.mode == M::Normal); // both relative to last activity
    s.tick(300100); assert(s.mode == M::Deep);
    assert(s.touch(true, 300200) && s.mode == M::Awake);
    assert(s.touch(true, 300300)); // held wake finger never clicks a widget
    assert(!s.touch(false, 300400));
    assert(!s.touch(true, 300500)); // next touch operates normally
    s.tick(360499); assert(s.mode == M::Awake); // activity resets both timers
    s.tick(360500); assert(s.mode == M::Normal);
    assert(s.touch(true, 360501) && s.mode == M::Awake); // normal also wakes
    s.touch(false, 360502);
    s.normalMs = 0; s.deepMs = 1000; s.wake(0);
    s.tick(1000); assert(s.mode == M::Deep); // direct automatic deep sleep
    s.normalMs = 1000; s.deepMs = 0; s.wake(0);
    s.tick(100000); assert(s.mode == M::Normal); // no automatic deep mode
    s.normalMs = s.deepMs = 0;
    s.enter(M::Deep, 0); s.tick(100000); assert(s.mode == M::Deep); // manual is sticky
    s.wake(100001); assert(s.mode == M::Awake);
    s.normalMs = 1000; s.wake(0xFFFFFF00U); s.tick(744); assert(s.mode == M::Normal); // millis wrap
    assert(sleep_timeouts_valid(0, 0));
    assert(sleep_timeouts_valid(0, 60));
    assert(sleep_timeouts_valid(60, 0));
    assert(sleep_timeouts_valid(60, 60));
    assert(sleep_timeouts_valid(60, 300));
    assert(!sleep_timeouts_valid(300, 60));
    assert(!sleep_timeouts_valid(86401, 0));
    puts("PASS: automatic/manual modes, timeout boundaries, timer reset, touch consumption, rollover and validation");
}
