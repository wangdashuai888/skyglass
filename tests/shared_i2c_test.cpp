#include "../src/shared_i2c.h"
#include <cassert>
#include <atomic>
#include <thread>
int main() {
    { SharedI2cGuard setup; assert(setup); }
    assert(shared_i2c_init());
    std::atomic<bool> owned{false}, release{false};
    std::thread gps([&] {
        SharedI2cGuard worker;
        assert(worker);
        owned = true;
        while (!release) std::this_thread::yield();
    });
    while (!owned) std::this_thread::yield();
    // Display task must be able to proceed instead of waiting for GPS's delays.
    { SharedI2cGuard touch; assert(!touch); }
    { SharedI2cGuard battery; assert(!battery); }
    release = true;
    gps.join();
    { SharedI2cGuard shutdown; assert(shutdown);
      SharedI2cGuard nestedRegister; assert(nestedRegister); }
    { SharedI2cGuard nextPoll; assert(nextPoll); }
}
