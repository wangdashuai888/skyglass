#pragma once
#include <algorithm>
#include <cmath>

namespace sweep_damage {
struct Rect { int x1, y1, x2, y2; };
// Cover both old and new beams in short radial sections. A diagonal line's
// single bounding rectangle would otherwise repaint most of a quadrant.
template<class Emit>
void regions(int cx, int cy, int oldX, int oldY, int newX, int newY,
             int segments, int pad, Emit emit) {
    for (int i = 0; i < segments; ++i) {
        const float a = float(i) / segments, b = float(i + 1) / segments;
        const float xs[] = {cx + (oldX-cx)*a, cx + (oldX-cx)*b,
                            cx + (newX-cx)*a, cx + (newX-cx)*b};
        const float ys[] = {cy + (oldY-cy)*a, cy + (oldY-cy)*b,
                            cy + (newY-cy)*a, cy + (newY-cy)*b};
        emit(Rect{int(std::floor(*std::min_element(xs,xs+4)))-pad,
                  int(std::floor(*std::min_element(ys,ys+4)))-pad,
                  int(std::ceil(*std::max_element(xs,xs+4)))+pad,
                  int(std::ceil(*std::max_element(ys,ys+4)))+pad});
    }
}
}
