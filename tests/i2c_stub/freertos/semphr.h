#pragma once
#include <mutex>
#include <chrono>
using SemaphoreHandle_t = std::recursive_timed_mutex *;
constexpr int pdTRUE = 1;
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return new std::recursive_timed_mutex; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s, uint32_t wait) {
    return (wait ? s->try_lock_for(std::chrono::milliseconds(wait)) : s->try_lock()) ? pdTRUE : 0;
}
inline void xSemaphoreGiveRecursive(SemaphoreHandle_t s) { s->unlock(); }
