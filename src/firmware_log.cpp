#ifdef ARDUINO
#include "firmware_log.h"
#include "log_buffer.h"
#include <esp_log.h>
#include <stdarg.h>
#include <stdlib.h>

FirmwareLogger FirmwareLog;
static LogBuffer<16384> history;
static portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

void FirmwareLogger::setEnabled(bool enabled) {
    portENTER_CRITICAL(&logMux);
    enabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) history.clear();
    portEXIT_CRITICAL(&logMux);
}

size_t FirmwareLogger::write(const uint8_t *data, size_t size) {
    if (!enabled()) return size;
    portENTER_CRITICAL(&logMux);
    if (enabled()) history.append(reinterpret_cast<const char *>(data), size);
    portEXIT_CRITICAL(&logMux);
    // Avoid waiting for an absent/full USB serial connection.
    if (Serial && Serial.availableForWrite() >= (int)size) Serial.write(data, size);
    return size;
}

String FirmwareLogger::snapshot() {
    char *copy = static_cast<char *>(malloc(16385));
    if (!copy) return "Not enough memory to read log.\n";
    portENTER_CRITICAL(&logMux);
    history.copy(copy);
    portEXIT_CRITICAL(&logMux);
    String result(copy);
    free(copy);
    return result;
}

static int captureEspLog(const char *format, va_list args) {
    if (!FirmwareLog.enabled()) return 0;
    char line[512];
    va_list countArgs;
    va_copy(countArgs, args);
    const int length = vsnprintf(line, sizeof(line), format, countArgs);
    va_end(countArgs);
    if (length <= 0) return length;
    if ((size_t)length < sizeof(line)) {
        FirmwareLog.write((const uint8_t *)line, length);
    } else {
        char *full = static_cast<char *>(malloc(length + 1));
        if (full) {
            vsnprintf(full, length + 1, format, args);
            FirmwareLog.write((const uint8_t *)full, length);
            free(full);
        }
    }
    return length;
}

void firmware_log_begin() { esp_log_set_vprintf(captureEspLog); }
#endif
