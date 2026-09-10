#pragma once

// Self-contained, frame-synced vertical scroll controller.
//
// This is the ONLY place scroll animation/clamping math lives. Tabs never
// touch it directly — they only feed in a content bound (set_max) and wheel
// deltas. Everything else (easing, clamping, the paint offset) is owned here,
// so it can never drift out of sync with the tab page again.
//
// Smoothness: tick() must be called once per compositor frame callback
// (wl_surface.frame), which by definition runs at the monitor's refresh rate.
// The easing is frame-rate independent (exponential decay over wall-clock
// time), so 60 Hz, 120 Hz and 144 Hz displays all animate identically.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>

namespace eh::settings {

class ScrollController {
 public:
  // Bound the scroll to [0, maxPx]. If the current/target overshoot (e.g.
  // after a window resize shrank the content) they are pulled back in and
  // any in-flight animation is cancelled.
  void set_max(int maxPx) {
    maxPx_ = std::max(0, maxPx);
    const double bound = static_cast<double>(maxPx_);
    if (target_ > bound) target_ = bound;
    if (current_ > bound) {
      current_ = animFrom_ = bound;
      animating_ = false;
    }
  }

  // Jump straight to px (tab switch, layout reset). No easing. Callers pass
  // already-clamped values, so no upper-bound clamp here (maxPx_ is a soft
  // estimate that may lag the true content bound).
  void snap_to(double px) {
    target_ = current_ = animFrom_ = std::max(0.0, px);
    animating_ = false;
  }

  // Animate toward an absolute pixel position.
  void scroll_to(double px) {
    const double nt = std::clamp(px, 0.0, static_cast<double>(maxPx_));
    if (std::abs(nt - current_) < 0.01) {
      target_ = nt;
      animating_ = false;
      return;
    }
    if (std::abs(nt - target_) < 0.01) return;
    animFrom_ = current_;
    animStartNs_ = now_ns();
    target_ = nt;
    animating_ = true;
  }

  // Apply a wheel delta (positive = content moves down / scroll up).
  void scroll_by(double deltaPx) { scroll_to(target_ - deltaPx); }

  // Advance the animation. Call once per compositor frame callback.
  void tick() {
    if (!animating_) return;
    const double elapsedMs = static_cast<double>(now_ns() - animStartNs_) / 1000000.0;
    constexpr double kTauMs = 40.0;  // matches the previous smoothing response
    const double factor = 1.0 - std::exp(-elapsedMs / kTauMs);
    current_ = animFrom_ + (target_ - animFrom_) * factor;
    if (std::abs(target_ - current_) <= 0.5) {
      current_ = target_;
      animating_ = false;
    }
  }

  double current() const { return current_; }
  int current_px() const { return static_cast<int>(std::lround(current_)); }
  double target() const { return target_; }
  int max() const { return maxPx_; }
  bool animating() const { return animating_; }

 private:
  static std::uint64_t now_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
  }

  double current_ = 0.0;
  double target_ = 0.0;
  double animFrom_ = 0.0;
  std::uint64_t animStartNs_ = 0;
  int maxPx_ = 0;
  bool animating_ = false;
};

}  // namespace eh::settings
