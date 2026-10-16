#pragma once

#include <unordered_set>

namespace eh::shell {

class MotionTimeline;

// Global animation pacing: a shared enable switch and speed multiplier
// consulted by every registered MotionTimeline.
class MotionRegistry {
public:
  [[nodiscard]] float speed() const noexcept { return speed_; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  void set_speed(float speed);
  void set_enabled(bool enabled);

  void unregister_manager(MotionTimeline* manager);
  void register_manager(MotionTimeline* manager);

  static MotionRegistry& instance();

  MotionRegistry(const MotionRegistry&) = delete;
  MotionRegistry& operator=(const MotionRegistry&) = delete;

private:
  MotionRegistry();

  std::unordered_set<MotionTimeline*> managers_{};
  float speed_ = 1.f;
  bool enabled_ = true;
};

}
