#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace eh::wayland {

struct DamageRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;

  [[nodiscard]] constexpr bool empty() const noexcept { return w <= 0 || h <= 0; }

  [[nodiscard]] constexpr bool contains(int32_t px, int32_t py) const noexcept {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// A Weston-style damage accumulator: rects added between frames are coalesced
// into a small span list. If the list would grow past kMaxSpans the region
// degrades to "full" instead of growing unbounded.
class DamageRegion {
public:
  static constexpr size_t kMaxSpans = 16;

  void mark_full() noexcept;
  void clear() noexcept;

  void add_rect(const DamageRect& r);
  void add_rect(int32_t x, int32_t y, int32_t w, int32_t h);
  void union_with(const DamageRegion& other);
  void clip_to(int32_t max_w, int32_t max_h);

  void expand(int32_t margin, int32_t max_w, int32_t max_h);

  [[nodiscard]] bool empty() const noexcept { return !full_ && spans_.empty(); }
  [[nodiscard]] bool full() const noexcept { return full_; }
  [[nodiscard]] const std::vector<DamageRect>& spans() const noexcept { return spans_; }
  [[nodiscard]] size_t span_count() const noexcept { return spans_.size(); }

private:
  void coalesce();

  bool full_ = false;
  std::vector<DamageRect> spans_;
};

}
