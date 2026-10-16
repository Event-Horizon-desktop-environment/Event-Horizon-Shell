#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace eh::shell {

enum class Easing : std::uint8_t {
  EaseInCubic,
  EaseInOutCubic,
  EaseOutCubic,
  EaseInOutQuad,
  EaseOutQuad,
  Linear,
};

float apply_easing(Easing easing, float t);

// Drives a set of float animations by ID; every registered manager also
// consults the global MotionRegistry for enable/speed.
class MotionTimeline {
public:
  using Id = std::uint32_t;

  MotionTimeline();
  ~MotionTimeline();
  MotionTimeline(const MotionTimeline&) = delete;
  MotionTimeline& operator=(const MotionTimeline&) = delete;
  MotionTimeline(MotionTimeline&&) = delete;
  MotionTimeline& operator=(MotionTimeline&&) = delete;

  void tick();
  [[nodiscard]] bool has_active() const noexcept;

  void reduce_motion();
  void cancel_for_owner(const void* owner);
  void cancel_all();
  void cancel(Id id);

  Id animateTimer(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                  std::function<void()> on_complete = {});
  Id animateUnscaled(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                     std::function<void()> on_complete = {});
  Id animate(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
             std::function<void()> on_complete = {});

private:
  struct Entry {
    bool finished = false;
    bool respect_motion = true;
    Easing easing = Easing::EaseOutCubic;
    float duration_ms = 0.f;
    float to = 0.f;
    float from = 0.f;
    Id id = 0;
    std::function<void()> on_complete;
    std::function<void(float)> setter;
    std::uint64_t mono_start_ns = 0;
    const void* owner = nullptr;
  };

  Id animateInternal(float from, float to, float duration_ms, Easing easing, std::function<void(float)> setter,
                     std::function<void()> on_complete, const void* owner, bool scale_duration, bool respect_motion);

  Id next_id_ = 1;
  std::vector<Entry> entries_;
};

[[nodiscard]] bool animations_reduced_motion() noexcept;

}
