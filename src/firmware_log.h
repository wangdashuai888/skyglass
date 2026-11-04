#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <atomic>

class FirmwareLogger : public Print {
    std::atomic<bool> enabled_{false};
public:
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    void setEnabled(bool enabled);
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t size) override;
    template<typename... Args> size_t printf(const char *format, Args... args) {
        return enabled() ? Print::printf(format, args...) : 0;
    }
    template<typename... Args> size_t print(Args... args) {
        return enabled() ? Print::print(args...) : 0;
    }
    template<typename... Args> size_t println(Args... args) {
        return enabled() ? Print::println(args...) : 0;
    }
    String snapshot();
};
extern FirmwareLogger FirmwareLog;
void firmware_log_begin();
#endif
