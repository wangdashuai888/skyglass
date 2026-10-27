#pragma once
#include <stdint.h>
struct TestSerial { void println(const char*) {} };
inline TestSerial Serial;
