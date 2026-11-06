#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>

// Initialize once, before starting the GPS worker. Before that setup is single-task.
bool shared_i2c_init();
class SharedI2cGuard {
    bool held_;
public:
    explicit SharedI2cGuard(TickType_t wait = 0);
    ~SharedI2cGuard();
    explicit operator bool() const { return held_; }
    SharedI2cGuard(const SharedI2cGuard &) = delete;
    SharedI2cGuard &operator=(const SharedI2cGuard &) = delete;
};
