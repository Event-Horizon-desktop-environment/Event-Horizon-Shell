#pragma once

#include <unordered_set>

namespace eh::shell {

class AnimationManager;

class MotionService {
public:
  static MotionService& instance();

  MotionService(const MotionService&) = delete;
  MotionService& operator=(const MotionService&) = delete;

  void register_manager(AnimationManager* manager);
  void unregister_manager(AnimationManager* manager);

  void set_enabled(bool enabled);
  void set_speed(float speed);

  [[nodiscard]] bool enabled() const noexcept { return enabled_; }
  [[nodiscard]] float speed() const noexcept { return speed_; }

private:
  MotionService();

  bool enabled_ = true;
  float speed_ = 1.f;
  std::unordered_set<AnimationManager*> managers_{};
};

}
