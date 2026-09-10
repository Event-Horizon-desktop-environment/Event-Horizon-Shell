#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#include "m3/tokens/motion.hpp"

namespace m3 {

// Evaluate an M3 easing curve at t ∈ [0,1] using cubic bezier approximation.
inline float evaluateEasing(Easing e, float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;

  // Newton-Raphson to find x for given t on cubic bezier x(t)
  auto cubicBezier = [](float p0, float p1, float p2, float p3, float t_) {
    const float u = 1.0f - t_;
    return u * u * u * p0 + 3.0f * u * u * t_ * p1 + 3.0f * u * t_ * t_ * p2 + t_ * t_ * t_ * p3;
  };

  auto sampleCurveX = [&](float t_) {
    return cubicBezier(0.0f, e.x1, e.x2, 1.0f, t_);
  };

  auto sampleCurveY = [&](float t_) {
    return cubicBezier(0.0f, e.y1, e.y2, 1.0f, t_);
  };

  auto sampleCurveDerivativeX = [&](float t_) {
    const float u = 1.0f - t_;
    return 3.0f * u * u * (e.x1 - 0.0f) +
           6.0f * u * t_ * (e.x2 - e.x1) +
           3.0f * t_ * t_ * (1.0f - e.x2);
  };

  float x = t;
  for (int i = 0; i < 8; ++i) {
    const float xEst = sampleCurveX(x) - t;
    const float deriv = sampleCurveDerivativeX(x);
    if (std::abs(deriv) < 1e-6f) break;
    x -= xEst / deriv;
  }

  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;

  return sampleCurveY(x);
}

// Evaluate spring physics using implicit Euler integration.
inline float evaluateSpring(float from, float to, float& velocity,
                            SpringParams spring, float dtSec) {
  float pos = from - to;
  for (float t = 0.0f; t < dtSec; t += 0.001f) {
    const float step = std::min(0.001f, dtSec - t);
    const float force = -spring.stiffness * pos - spring.damping * velocity;
    velocity += (force / spring.mass) * step;
    pos += velocity * step;
  }
  return to + pos;
}

// Simple tween track for a single float value.
struct Tween {
  float from = 0.0f;
  float to = 0.0f;
  uint64_t startMs = 0;
  uint64_t durationMs = 0;
  Easing easing = kEmphasized;
  bool active = false;

  void start(float fromVal, float toVal, uint64_t nowMs, uint64_t durMs,
             Easing ease = kEmphasized) {
    from = fromVal;
    to = toVal;
    startMs = nowMs;
    durationMs = durMs;
    easing = ease;
    active = true;
  }

  float value(uint64_t nowMs) const {
    if (!active) return to;
    if (durationMs == 0 || nowMs == 0) return to;
    const uint64_t elapsed = nowMs - startMs;
    if (elapsed >= durationMs) return to;
    const float t = static_cast<float>(elapsed) / static_cast<float>(durationMs);
    return from + (to - from) * evaluateEasing(easing, t);
  }

  bool finished(uint64_t nowMs) const {
    return !active || (nowMs - startMs) >= durationMs;
  }

  void stop() { active = false; }
};

// AnimationManager manages multiple Tween animations by ID.
class AnimationManager {
public:
  using Id = uint32_t;

  Id animate(float from, float to, uint64_t durationMs, Easing easing,
             std::function<void(float)> setter,
             std::function<void()> onComplete = {}) {
    const Id id = nextId_++;
    entries_.push_back({id, from, to, durationMs, easing, std::move(setter), std::move(onComplete), 0, false});
    return id;
  }

  void cancel(Id id) {
    for (auto& e : entries_) {
      if (e.id == id) { e.finished = true; break; }
    }
  }

  void cancelAll() { entries_.clear(); }

  void tick(uint64_t nowMs) {
    for (auto& e : entries_) {
      if (e.finished) continue;
      if (!e.startSet) {
        e.startMs = nowMs;
        e.startSet = true;
      }
      const uint64_t elapsed = nowMs - e.startMs;
      if (elapsed >= e.durationMs) {
        e.setter(e.to);
        e.finished = true;
        if (e.onComplete) e.onComplete();
      } else {
        const float t = static_cast<float>(elapsed) / static_cast<float>(e.durationMs);
        const float v = e.from + (e.to - e.from) * evaluateEasing(e.easing, t);
        e.setter(v);
      }
    }
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                   [](const Entry& e) { return e.finished; }),
                   entries_.end());
  }

  bool hasActive() const { return !entries_.empty(); }

private:
  struct Entry {
    Id id;
    float from, to;
    uint64_t durationMs;
    Easing easing;
    std::function<void(float)> setter;
    std::function<void()> onComplete;
    uint64_t startMs;
    bool startSet;
    bool finished = false;
  };

  std::vector<Entry> entries_;
  Id nextId_ = 1;
};

} // namespace m3
