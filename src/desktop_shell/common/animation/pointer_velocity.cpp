#include "desktop_shell/common/animation/pointer_velocity.hpp"

#include "desktop_shell/common/animation/animations.hpp"

#include <algorithm>
#include <cstdlib>

namespace eh::shell {

MotionService::MotionService() {
   
  if (const char* e = std::getenv("EH_MOTION_ENABLED"); e && e[0] == '0') enabled_ = false;
  if (const char* e = std::getenv("EH_ANIMATION_SPEED"); e && e[0]) {
    char* end = nullptr;
    const double v = std::strtod(e, &end);
    if (end != e) speed_ = std::clamp(static_cast<float>(v), 0.05f, 4.f);
  }
}

MotionService& MotionService::instance() {
  static MotionService s{};
  return s;
}

void MotionService::register_manager(AnimationManager* manager) {
   
  if (!manager) return;
  managers_.insert(manager);
}

void MotionService::unregister_manager(AnimationManager* manager) {
   
  if (!manager) return;
  managers_.erase(manager);
}

void MotionService::set_enabled(bool enabled) {
   
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (!enabled_) {
    for (AnimationManager* m : managers_) {
      if (m) m->reduce_motion();
    }
  }
}

void MotionService::set_speed(float speed) { speed_ = std::clamp(speed, 0.05f, 4.f); }

}
