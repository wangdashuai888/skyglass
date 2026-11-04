#pragma once
#include <stddef.h>
#include <string.h>

// Caller provides synchronization. Keeps the newest bytes when full.
template<size_t Capacity> class LogBuffer {
    char bytes_[Capacity];
    size_t head_ = 0, size_ = 0;
public:
    void clear() { head_ = size_ = 0; }
    void append(const char *data, size_t length) {
        if (length >= Capacity) { data += length - Capacity; length = Capacity; }
        const size_t first = length < Capacity - head_ ? length : Capacity - head_;
        memcpy(bytes_ + head_, data, first);
        memcpy(bytes_, data + first, length - first);
        head_ = (head_ + length) % Capacity;
        size_ = size_ + length < Capacity ? size_ + length : Capacity;
    }
    size_t copy(char *out) const {
        const size_t start = (head_ + Capacity - size_) % Capacity;
        const size_t first = size_ < Capacity - start ? size_ : Capacity - start;
        memcpy(out, bytes_ + start, first);
        memcpy(out + first, bytes_, size_ - first);
        out[size_] = 0;
        return size_;
    }
};
