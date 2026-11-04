#include "../src/log_buffer.h"
#include <cassert>
#include <cstring>
int main() {
    LogBuffer<8> ring;
    char out[9];
    assert(ring.copy(out) == 0 && out[0] == 0);
    ring.append("abc", 3);
    ring.append("defghi", 6);
    assert(ring.copy(out) == 8 && strcmp(out, "bcdefghi") == 0);
    ring.append("0123456789", 10);
    assert(ring.copy(out) == 8 && strcmp(out, "23456789") == 0);
    ring.clear();
    assert(ring.copy(out) == 0 && out[0] == 0);
    ring.append("new", 3);
    assert(ring.copy(out) == 3 && strcmp(out, "new") == 0);
}
