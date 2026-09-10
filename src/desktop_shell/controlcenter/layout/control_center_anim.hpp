#pragma once

#include <algorithm>

namespace eh::shell::dock::control_center {

inline double cc_ease01(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

}
