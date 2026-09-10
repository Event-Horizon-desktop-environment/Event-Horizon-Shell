#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace eh::shell {

enum class Easing : std::uint8_t {
  Linear,
  EaseOutQuad,
  EaseInOutQuad,
  EaseOutCubic,
  EaseInCubic,
  EaseInOutCubic,
};

float apply_easing(Easing easing, float t);

class AnimationManager {
public:
  using Id = std::uint32_t;

  AnimationManager();
  ~AnimationManager();
  AnimationManager(const AnimationManager&) = delete;
  AnimationManager& operator=(const AnimationManager&) = delete;
  AnimationManager(AnimationManager&&) = delete;
  AnimationManager& operator=(AnimationManager&&) = delete;

  Id animate(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
             std::function<void()> on_complete = {});

  Id animateUnscaled(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                     std::function<void()> on_complete = {});

  Id animateTimer(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                  std::function<void()> on_complete = {});

  void cancel(Id id);
  void cancel_all();
  void cancel_for_owner(const void* owner);
  void reduce_motion();

  void tick();
  [[nodiscard]] bool has_active() const noexcept;

private:
  struct Entry {
    Id id = 0;
    const void* owner = nullptr;
    bool respect_motion = true;
    float from = 0.f;
    float to = 0.f;
    float duration_ms = 0.f;
    std::uint64_t mono_start_ns = 0;
    Easing easing = Easing::EaseOutCubic;
    std::function<void(float)> setter;
    std::function<void()> on_complete;
    bool finished = false;
  };

  std::vector<Entry> entries_;
  Id next_id_ = 1;

  Id animateInternal(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                     std::function<void()> on_complete, const void* owner, bool scale_duration, bool respect_motion);
};

[[nodiscard]] bool animations_reduced_motion() noexcept;

}
