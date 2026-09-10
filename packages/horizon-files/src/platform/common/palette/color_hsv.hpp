#pragma once

#include <cmath>

namespace eh::shell::color {

inline void hsv_to_rgb(double h_deg, double s, double v, double& out_r, double& out_g, double& out_b) {
  h_deg = std::fmod(h_deg, 360.0);
  if (h_deg < 0) h_deg += 360.0;
  const double hh = h_deg / 60.0;
  const int i = static_cast<int>(hh);
  const double f = hh - static_cast<double>(i);
  const double p = v * (1.0 - s);
  const double q = v * (1.0 - s * f);
  const double t = v * (1.0 - s * (1.0 - f));
  switch (i % 6) {
    case 0: out_r = v; out_g = t; out_b = p; break;
    case 1: out_r = q; out_g = v; out_b = p; break;
    case 2: out_r = p; out_g = v; out_b = t; break;
    case 3: out_r = p; out_g = q; out_b = v; break;
    case 4: out_r = t; out_g = p; out_b = v; break;
    default: out_r = v; out_g = p; out_b = q; break;
  }
}

}
