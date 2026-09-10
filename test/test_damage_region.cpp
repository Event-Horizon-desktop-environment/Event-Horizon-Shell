#include "wl/buffer/damage_region.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>

using eh::wayland::DamageRect;
using eh::wayland::DamageRegion;

// Keeps the checks (and the variables they reference) alive in release builds,
// where assert() compiles away under -Werror.
#define EH_CHECK(expr)                                                                                                \
  do {                                                                                                                \
    if (!(expr)) {                                                                                                    \
      std::fprintf(stderr, "EH_CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                               \
      std::abort();                                                                                                   \
    }                                                                                                                 \
  } while (0)

static void test_default_empty() {
  DamageRegion r;
  EH_CHECK(r.empty());
  EH_CHECK(!r.full());
  EH_CHECK(r.span_count() == 0);
}

static void test_mark_full_and_clear() {
  DamageRegion r;
  r.mark_full();
  EH_CHECK(r.full());
  EH_CHECK(!r.empty());
  EH_CHECK(r.span_count() == 0);
  r.add_rect(0, 0, 10, 10);
  EH_CHECK(r.full());
  EH_CHECK(r.span_count() == 0);
  r.clear();
  EH_CHECK(r.empty());
  EH_CHECK(!r.full());
}

static void test_single_rect() {
  DamageRegion r;
  r.add_rect(DamageRect{5, 7, 3, 4});
  EH_CHECK(!r.empty() && !r.full());
  EH_CHECK(r.span_count() == 1);
  const auto& s = r.spans()[0];
  EH_CHECK(s.x == 5 && s.y == 7 && s.w == 3 && s.h == 4);
}

static void test_degenerate_ignored() {
  DamageRegion r;
  r.add_rect(1, 1, 0, 5);
  r.add_rect(1, 1, 5, 0);
  r.add_rect(1, 1, -3, 5);
  EH_CHECK(r.empty());
}

static void test_overlap_coalesces() {
  DamageRegion r;
  r.add_rect(0, 0, 10, 10);
  r.add_rect(5, 5, 10, 10);
  EH_CHECK(r.span_count() == 1);
  const auto& s = r.spans()[0];
  EH_CHECK(s.x == 0 && s.y == 0 && s.w == 15 && s.h == 15);
}

static void test_touching_edges_coalesce() {
  DamageRegion r;
  r.add_rect(0, 0, 10, 10);
  r.add_rect(10, 0, 10, 10);
  EH_CHECK(r.span_count() == 1);
  EH_CHECK(r.spans()[0].w == 20 && r.spans()[0].h == 10);
}

static void test_disjoint_stay_separate() {
  DamageRegion r;
  r.add_rect(0, 0, 4, 4);
  r.add_rect(100, 100, 4, 4);
  EH_CHECK(r.span_count() == 2);
  r.union_with(DamageRegion{});
  EH_CHECK(r.span_count() == 2);
}

static void test_union_with_full() {
  DamageRegion a;
  a.add_rect(0, 0, 2, 2);
  DamageRegion b;
  b.mark_full();
  a.union_with(b);
  EH_CHECK(a.full());
  EH_CHECK(a.span_count() == 0);
}

static void test_overflow_falls_back_to_full() {
  DamageRegion r;
  for (int i = 0; i < 64; ++i) {
    r.add_rect(i * 1000, static_cast<int32_t>(i % 2) * 1000, 1, 1);
    if (r.full()) break;
  }
  EH_CHECK(r.full());
  r.add_rect(1, 1, 1, 1);
  EH_CHECK(r.full());
}

static void test_clip_to() {
  DamageRegion r;
  r.add_rect(-5, -5, 20, 20);
  r.clip_to(10, 10);
  EH_CHECK(r.span_count() == 1);
  const auto& s = r.spans()[0];
  EH_CHECK(s.x == 0 && s.y == 0 && s.w == 10 && s.h == 10);

  DamageRegion out;
  out.add_rect(50, 50, 5, 5);
  out.clip_to(10, 10);
  EH_CHECK(out.empty());

  DamageRegion f;
  f.mark_full();
  f.clip_to(4, 4);
  EH_CHECK(f.full());

  DamageRegion e;
  e.clip_to(4, 4);
  EH_CHECK(e.empty());
}

static void test_expand() {
  DamageRegion r;
  r.add_rect(10, 10, 4, 4);
  r.expand(3, 100, 100);
  EH_CHECK(r.span_count() >= 1);
  int32_t min_x = 1 << 30, min_y = 1 << 30, max_x = 0, max_y = 0;
  for (const auto& s : r.spans()) {
    min_x = std::min(min_x, s.x);
    min_y = std::min(min_y, s.y);
    max_x = std::max(max_x, s.x + s.w);
    max_y = std::max(max_y, s.y + s.h);
  }
  EH_CHECK(min_x == 7 && min_y == 7 && max_x == 17 && max_y == 17);

  DamageRegion edge;
  edge.add_rect(0, 0, 2, 2);
  edge.expand(5, 4, 4);
  EH_CHECK(edge.span_count() == 1);
  EH_CHECK(edge.spans()[0].x == 0 && edge.spans()[0].y == 0 && edge.spans()[0].w == 4 && edge.spans()[0].h == 4);

  DamageRegion f;
  f.mark_full();
  f.expand(3, 10, 10);
  EH_CHECK(f.full());
}

static void test_marquee_pattern_stays_small() {
  DamageRegion r;
  for (int i = 0; i < 200; ++i) {
    r.add_rect(i % 40, 30, 60, 12);
    EH_CHECK(!r.full());
    EH_CHECK(r.span_count() <= DamageRegion::kMaxSpans);
  }
  EH_CHECK(r.span_count() == 1);
}

static void test_rect_contains() {
  const DamageRect rect{10, 10, 5, 5};
  EH_CHECK(rect.contains(10, 10));
  EH_CHECK(rect.contains(14, 14));
  EH_CHECK(!rect.contains(15, 15));
  EH_CHECK(!rect.contains(9, 10));
  EH_CHECK(!rect.contains(10, 9));
}

int main() {
  test_default_empty();
  test_mark_full_and_clear();
  test_single_rect();
  test_degenerate_ignored();
  test_overlap_coalesces();
  test_touching_edges_coalesce();
  test_disjoint_stay_separate();
  test_union_with_full();
  test_overflow_falls_back_to_full();
  test_clip_to();
  test_expand();
  test_marquee_pattern_stays_small();
  test_rect_contains();
  std::puts("test_damage_region: all checks passed");
  return 0;
}
