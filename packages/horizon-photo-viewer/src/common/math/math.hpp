#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hpv {
namespace math {

inline float hue_from_rgb(uint32_t rgba) {
    float r = ((rgba >> 24) & 0xFF) / 255.0f;
    float g = ((rgba >> 16) & 0xFF) / 255.0f;
    float b = ((rgba >> 8) & 0xFF) / 255.0f;
    float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx - mn < 0.001f) return 0;
    float d = mx - mn, h;
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    return h / 6.0f;
}

inline uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - std::abs(fmod(hp, 2.0f) - 1.0f));
    float r, g, b;
    int i = (int)hp % 6;
    if (i == 0)      { r = c; g = x; b = 0; }
    else if (i == 1) { r = x; g = c; b = 0; }
    else if (i == 2) { r = 0; g = c; b = x; }
    else if (i == 3) { r = 0; g = x; b = c; }
    else if (i == 4) { r = x; g = 0; b = c; }
    else             { r = c; g = 0; b = x; }
    float m = v - c;
    return ((uint32_t)((r + m) * 255) << 24) |
           ((uint32_t)((g + m) * 255) << 16) |
           ((uint32_t)((b + m) * 255) << 8) |
           0xFF;
}

} // namespace math
} // namespace hpv
