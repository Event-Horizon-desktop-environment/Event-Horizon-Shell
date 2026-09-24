#pragma once

// hit_registry.hpp — the single source of truth shared by paint and input.
//
// Background: Nautilus (GTK) delivers pointer events via gtk_widget_pick(),
// which walks the widget tree testing each widget's *allocation* — the rect
// assigned once per layout in size_allocate(). Dolphin (Qt) does the same
// through item geometries and childAt(). In both toolkits the rule is
// absolute: input code never recomputes geometry; paint and input read the
// same retained rects, so resizing can never desync them.
//
// This header emulates that model for immediate-mode cairo UI. Every paint
// clears the registry, and each draw site registers the exact rect it just
// painted, tagged with a stable region ID. Input handlers query the
// registry instead of re-deriving coordinates. Since registration happens
// at the draw site from the same locals used for painting, hit areas
// recalculate themselves on every frame at any size — there is no parallel
// geometry left to drift.
//
// Rules for new UI:
//   1. If you paint something clickable, register it: hits.add(id, ...)
//      using the same x/y/w/h locals you paint with.
//   2. If you handle a pointer event, query: hits.query(x, y). Never
//      recompute a painted rect in input code.
//   3. Registration order is paint order; query returns the topmost
//      (last-registered) match, mirroring paint-on-top wins.
//
// Coordinates are doubles: shell geometry (scales, pills, strips) is
// fractional. The header is dependency-free (no cairo, no app state) so
// the logic is unit-testable in isolation. See test/test_hit_registry.cpp.

#include <cstdint>
#include <vector>

namespace eh::shell {

namespace Hit {
inline constexpr uint32_t kNone = 0;

// Group bases. Query with (id & kGroupMask) to match a group; low bits
// carry an index where applicable.
inline constexpr uint32_t kDockSlot = 0x1000;   // + slot index in retained order
inline constexpr uint32_t kGroupMask = 0xF000;
inline constexpr uint32_t kIndexMask = 0x0FFF;

inline constexpr uint32_t dock_slot(int i) {
  return kDockSlot + static_cast<uint32_t>(i);
}
} // namespace Hit

struct HitRegion {
  uint32_t id = Hit::kNone;
  double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
};

class HitRegistry {
 public:
  void clear() { regions_.clear(); }

  void add(uint32_t id, double x, double y, double w, double h) {
    if (id == Hit::kNone || w <= 0.0 || h <= 0.0) return;
    regions_.push_back(HitRegion{id, x, y, w, h});
  }

  // Topmost (last-registered) region containing the point, or kNone.
  // Edges are half-open [x, x+w), matching surface coordinate convention.
  [[nodiscard]] uint32_t query(double x, double y) const {
    for (size_t i = regions_.size(); i-- > 0;) {
      const auto& r = regions_[i];
      if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
        return r.id;
    }
    return Hit::kNone;
  }

  [[nodiscard]] size_t size() const { return regions_.size(); }
  [[nodiscard]] const HitRegion& at(size_t i) const { return regions_[i]; }

 private:
  std::vector<HitRegion> regions_;
};

} // namespace eh::shell
