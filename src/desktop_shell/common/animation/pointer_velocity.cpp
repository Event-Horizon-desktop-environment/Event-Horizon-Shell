#include "desktop_shell/common/animation/pointer_velocity.hpp"

#include "desktop_shell/common/animation/animations.hpp"

#include <algorithm>
#include <cstdlib>

namespace eh::shell {

MotionRegistry::MotionRegistry() {
   
  if (const char* e = std::getenv("EH_MOTION_ENABLED"); e && e[0] == '0') enabled_ = false;
  if (const char* e = std::getenv("EH_ANIMATION_SPEED"); e && e[0]) {
    char* end = nullptr;
    const double v = std::strtod(e, &end);
    if (end != e) speed_ = std::clamp(static_cast<float>(v), 0.05f, 4.f);
  }
}

MotionRegistry& MotionRegistry::instance() {
  static MotionRegistry s{};
  return s;
}

void MotionRegistry::register_manager(MotionTimeline* manager) {
   
  if (!manager) return;
  managers_.insert(manager);
}

void MotionRegistry::unregister_manager(MotionTimeline* manager) {
   
  if (!manager) return;
  managers_.erase(manager);
}

void MotionRegistry::set_enabled(bool enabled) {
   
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (!enabled_) {
    for (MotionTimeline* m : managers_) {
      if (m) m->reduce_motion();
    }
  }
}

void MotionRegistry::set_speed(float speed) { speed_ = std::clamp(speed, 0.05f, 4.f); }

}
