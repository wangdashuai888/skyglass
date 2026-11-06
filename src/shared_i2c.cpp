#ifdef ARDUINO
#include "shared_i2c.h"
static SemaphoreHandle_t busMutex = nullptr;
bool shared_i2c_init() {
    if (!busMutex) busMutex = xSemaphoreCreateRecursiveMutex();
    return busMutex != nullptr;
}
SharedI2cGuard::SharedI2cGuard(TickType_t wait)
    : held_(!busMutex || xSemaphoreTakeRecursive(busMutex, wait) == pdTRUE) {}
SharedI2cGuard::~SharedI2cGuard() {
    if (held_ && busMutex) xSemaphoreGiveRecursive(busMutex);
}
#endif
