#pragma once
#include <cmath>
// Angular afterglow starts at the beam crossing and fades behind it.
inline float sweep_glow_fraction(float sweep, float aircraft, float width) {
    if (width <= 0) return 0;
    float delta = std::fmod(sweep - aircraft + 360.0f, 360.0f);
    if (delta < 0) delta += 360.0f;
    return delta < width ? 1.0f - delta / width : 0.0f;
}
