#pragma once

#include <cstdint>

#include "point.hpp"

namespace blit {

  struct Size {

    int32_t w = 0, h = 0;

    Size() = default;
    constexpr Size(int32_t w, int32_t h) : w(w), h(h) {}

    inline Size& operator*= (const float a) { w = static_cast<int32_t>(w * a); h = static_cast<int32_t>(h * a); return *this; }

    bool empty() { return w <= 0 || h <= 0; }

    int32_t area() { return w * h; }

    bool contains(const Point &p) {
      return p.x >= 0 && p.y >= 0 && p.x < w && p.y < h;
    }

  };

  inline Size operator*  (Size lhs, const float a) { lhs *= a; return lhs; }

}