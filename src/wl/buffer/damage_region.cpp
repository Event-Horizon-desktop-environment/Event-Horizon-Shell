#include "wl/buffer/damage_region.hpp"

#include <algorithm>
#include <limits>

namespace eh::wayland {

void DamageRegion::mark_full() noexcept {
  full_ = true;
  spans_.clear();
}

void DamageRegion::clear() noexcept {
  full_ = false;
  spans_.clear();
}

void DamageRegion::add_rect(const DamageRect& r) {
  if (full_ || r.empty()) return;
  if (spans_.capacity() == 0) spans_.reserve(kMaxSpans);
  spans_.push_back(r);
  coalesce();
}

void DamageRegion::add_rect(int32_t x, int32_t y, int32_t w, int32_t h) { add_rect(DamageRect{x, y, w, h}); }

void DamageRegion::union_with(const DamageRegion& other) {
  if (full_) return;
  if (other.full_) {
    mark_full();
    return;
  }
  if (spans_.capacity() < other.spans_.size()) spans_.reserve(std::max(other.spans_.size(), kMaxSpans));
  spans_.insert(spans_.end(), other.spans_.begin(), other.spans_.end());
  coalesce();
}

void DamageRegion::clip_to(int32_t max_w, int32_t max_h) {
  if (full_ || spans_.empty()) return;
  size_t out = 0;
  for (size_t i = 0; i < spans_.size(); ++i) {
    DamageRect r = spans_[i];
    const int32_t x0 = std::clamp(r.x, static_cast<int32_t>(0), max_w);
    const int32_t y0 = std::clamp(r.y, static_cast<int32_t>(0), max_h);
    const int32_t x1 = std::clamp(r.x + r.w, static_cast<int32_t>(0), max_w);
    const int32_t y1 = std::clamp(r.y + r.h, static_cast<int32_t>(0), max_h);
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    if (!r.empty()) spans_[out++] = r;
  }
  spans_.resize(out);
}

void DamageRegion::expand(int32_t margin, int32_t max_w, int32_t max_h) {
  if (full_ || margin <= 0) {
    clip_to(max_w, max_h);
    return;
  }
  for (auto& r : spans_) {
    const int32_t nx = r.x < margin ? 0 : r.x - margin;
    const int32_t ny = r.y < margin ? 0 : r.y - margin;
    const int64_t nw64 = static_cast<int64_t>(r.w) + static_cast<int64_t>(r.x - nx) + static_cast<int64_t>(margin);
    const int64_t nh64 = static_cast<int64_t>(r.h) + static_cast<int64_t>(r.y - ny) + static_cast<int64_t>(margin);
    r.x = nx;
    r.y = ny;
    r.w = static_cast<int32_t>(std::min<int64_t>(nw64, std::numeric_limits<int32_t>::max()));
    r.h = static_cast<int32_t>(std::min<int64_t>(nh64, std::numeric_limits<int32_t>::max()));
  }
  clip_to(max_w, max_h);
  coalesce();
}

void DamageRegion::coalesce() {
  bool merged_any = true;
  while (merged_any && !full_) {
    merged_any = false;
    for (size_t i = 0; i < spans_.size(); ++i) {
      for (size_t j = i + 1; j < spans_.size();) {
        const auto& a = spans_[i];
        const auto& b = spans_[j];
        // Rects merge when they intersect or just touch (edge-adjacent counts).
        if (a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h) {
          const int32_t x0 = std::min(a.x, b.x);
          const int32_t y0 = std::min(a.y, b.y);
          const int32_t x1 = std::max(a.x + a.w, b.x + b.w);
          const int32_t y1 = std::max(a.y + a.h, b.y + b.h);
          spans_[i] = DamageRect{x0, y0, x1 - x0, y1 - y0};
          spans_[j] = spans_.back();
          spans_.pop_back();
          merged_any = true;
          continue;
        }
        ++j;
      }
      if (spans_.size() > kMaxSpans) {
        mark_full();
        return;
      }
    }
  }
}

}
