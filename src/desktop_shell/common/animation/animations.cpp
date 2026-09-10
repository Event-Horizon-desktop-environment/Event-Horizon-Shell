#include "desktop_shell/common/animation/animations.hpp"

#include "desktop_shell/common/animation/pointer_velocity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <time.h>

namespace eh::shell {

AnimationManager::AnimationManager() { MotionService::instance().register_manager(this); }

AnimationManager::~AnimationManager() { MotionService::instance().unregister_manager(this); }

float apply_easing(Easing easing, float t) {
  t = std::clamp(t, 0.f, 1.f);
  switch (easing) {
  case Easing::Linear:
    return t;
  case Easing::EaseOutQuad:
    return t * (2.f - t);
  case Easing::EaseInOutQuad:
    if (t < 0.5f) return 2.f * t * t;
    return -1.f + (4.f - 2.f * t) * t;
  case Easing::EaseOutCubic: {
    const float f = t - 1.f;
    return f * f * f + 1.f;
  }
  case Easing::EaseInCubic:
    return t * t * t;
  case Easing::EaseInOutCubic:
    if (t < 0.5f) return 4.f * t * t * t;
    {
      const float f = 2.f * t - 2.f;
      return 0.5f * f * f * f + 1.f;
    }
  }
  return t;
}

bool animations_reduced_motion() noexcept {
  if (const char* e = std::getenv("EH_REDUCED_MOTION"); e && e[0] == '1') return true;
  if (const char* e = std::getenv("ACCESSIBILITY_REDUCED_MOTION"); e && e[0] == '1') return true;
  return false;
}

AnimationManager::Id AnimationManager::animate(float from, float to, float duration_ms, Easing easing,
                                                std::function<void(float)> setter,
                                                std::function<void()> on_complete) {
  return animateInternal(from, to, duration_ms, easing, std::move(setter), std::move(on_complete), nullptr, true, true);
}

AnimationManager::Id AnimationManager::animateUnscaled(float from, float to, float duration_ms, Easing easing,
                                                        std::function<void(float)> setter,
                                                        std::function<void()> on_complete) {
  return animateInternal(from, to, duration_ms, easing, std::move(setter), std::move(on_complete), nullptr, false, true);
}

AnimationManager::Id AnimationManager::animateTimer(float from, float to, float duration_ms, Easing easing,
                                                     std::function<void(float)> setter,
                                                     std::function<void()> on_complete) {
  return animateInternal(from, to, duration_ms, easing, std::move(setter), std::move(on_complete), nullptr, false, false);
}

AnimationManager::Id AnimationManager::animateInternal(float from, float to, float duration_ms, Easing easing,
                                                        std::function<void(float)> setter,
                                                        std::function<void()> on_complete, const void* owner,
                                                        bool scale_duration, bool respect_motion) {
  auto& motion = MotionService::instance();

  if (respect_motion && !motion.enabled()) {
    if (setter) setter(to);
    if (on_complete) on_complete();
    return 0;
  }

  const bool reduced = respect_motion && animations_reduced_motion();
  float effective_ms = scale_duration ? duration_ms / motion.speed() : duration_ms;
  if (reduced) effective_ms = 1.f;

  if (effective_ms <= 0.f) {
    if (setter) setter(to);
    if (on_complete) on_complete();
    return 0;
  }

  const Id id = next_id_++;
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const std::uint64_t now_ns = static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
                               static_cast<std::uint64_t>(ts.tv_nsec);

  Entry e{};
  e.id = id;
  e.owner = owner;
  e.respect_motion = respect_motion;
  e.from = reduced ? to : from;
  e.to = to;
  e.duration_ms = effective_ms;
  e.mono_start_ns = reduced ? now_ns - 1000000 : now_ns;
  e.easing = easing;
  e.setter = std::move(setter);
  e.on_complete = std::move(on_complete);
  entries_.push_back(std::move(e));
  return id;
}

void AnimationManager::cancel(Id id) {
  if (id == 0) return;
  std::erase_if(entries_, [id](const Entry& en) { return en.id == id; });
}

void AnimationManager::cancel_all() {
  entries_.clear();
}

void AnimationManager::cancel_for_owner(const void* owner) {
  if (!owner) return;
  std::erase_if(entries_, [owner](const Entry& e) { return e.owner == owner; });
}

void AnimationManager::reduce_motion() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const std::uint64_t now_ns = static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
                               static_cast<std::uint64_t>(ts.tv_nsec);

  for (auto& e : entries_) {
    if (!e.respect_motion || e.finished) continue;
    if (e.setter) e.setter(e.to);
    e.from = e.to;
    e.duration_ms = 1.f;
    e.mono_start_ns = now_ns - 1000000;
  }
}

void AnimationManager::tick() {
  if (entries_.empty()) return;

  timespec mono_ts{};
  clock_gettime(CLOCK_MONOTONIC, &mono_ts);
  const std::uint64_t mono_now_ns =
      static_cast<std::uint64_t>(mono_ts.tv_sec) * 1000000000ull +
      static_cast<std::uint64_t>(mono_ts.tv_nsec);

  std::vector<std::function<void()>> done;

  for (auto& e : entries_) {
    if (e.finished) continue;

    const double elapsed_ns = mono_now_ns > e.mono_start_ns
        ? static_cast<double>(mono_now_ns - e.mono_start_ns)
        : 0.0;
    const float elapsed_ms = static_cast<float>(elapsed_ns / 1000000.0);
    float u = e.duration_ms > 0.f ? elapsed_ms / e.duration_ms : 1.f;
    if (u >= 1.f) u = 1.f;

    const float eased = apply_easing(e.easing, u);
    const float v = e.from + (e.to - e.from) * eased;
    if (e.setter) e.setter(v);

    if (u >= 1.f) {
      e.finished = true;
      if (e.on_complete) done.push_back(std::move(e.on_complete));
    }
  }

  std::erase_if(entries_, [](const Entry& e) { return e.finished; });
  for (auto& cb : done) {
    if (cb) cb();
  }
}

bool AnimationManager::has_active() const noexcept { return !entries_.empty(); }

}
