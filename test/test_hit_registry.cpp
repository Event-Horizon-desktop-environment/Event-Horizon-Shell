// test_hit_registry.cpp — unit tests for shared/system/input/hit_registry.hpp.
// Pure logic, no Wayland/cairo needed.

#include "shared/system/input/hit_registry.hpp"

#include <cassert>
#include <cstdio>

using namespace eh::shell;

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++failures;                                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                          \
  } while (0)

int main() {
  // Empty registry misses.
  {
    HitRegistry r;
    CHECK(r.query(0, 0) == Hit::kNone);
    CHECK(r.size() == 0);
  }

  // Basic hit / half-open edges, fractional coords (scaled UI).
  {
    HitRegistry r;
    r.add(Hit::dock_slot(2), 100.5, 4.0, 60.25, 48.0);
    CHECK(r.query(100.5, 4.0) == Hit::dock_slot(2));
    CHECK(r.query(160.0, 51.0) == Hit::dock_slot(2));
    CHECK(r.query(160.75, 4.0) == Hit::kNone);
    CHECK(r.query(100.5, 52.0) == Hit::kNone);
  }

  // Topmost (last registered) wins on overlap — paint-on-top semantics.
  {
    HitRegistry r;
    r.add(Hit::dock_slot(0), 0.0, 0.0, 200.0, 48.0);
    r.add(Hit::dock_slot(1), 100.0, 0.0, 200.0, 48.0); // painted later
    CHECK(r.query(150.0, 24.0) == Hit::dock_slot(1));
    CHECK(r.query(50.0, 24.0) == Hit::dock_slot(0));
  }

  // Group matching via mask, index extraction.
  {
    HitRegistry r;
    r.add(Hit::dock_slot(41), 10.0, 10.0, 20.0, 20.0);
    uint32_t hid = r.query(15.0, 15.0);
    CHECK((hid & Hit::kGroupMask) == Hit::kDockSlot);
    CHECK((hid & Hit::kIndexMask) == 41);
  }

  // Degenerate rects are never registered.
  {
    HitRegistry r;
    r.add(Hit::dock_slot(0), 10.0, 10.0, 0.0, 48.0);
    r.add(Hit::dock_slot(1), 10.0, 10.0, 48.0, 0.0);
    r.add(Hit::dock_slot(2), 10.0, 10.0, -5.0, 48.0);
    r.add(Hit::kNone, 10.0, 10.0, 48.0, 48.0);
    CHECK(r.size() == 0);
    CHECK(r.query(10.0, 10.0) == Hit::kNone);
  }

  // Resize scenario: re-registering after clear reflects new geometry.
  {
    HitRegistry r;
    r.add(Hit::dock_slot(0), 0.0, 0.0, 120.0, 48.0); // narrow output
    CHECK(r.query(100.0, 24.0) == Hit::dock_slot(0));
    r.clear(); // next frame at a new size
    r.add(Hit::dock_slot(0), 0.0, 0.0, 200.0, 48.0); // wider output
    CHECK(r.query(180.0, 24.0) == Hit::dock_slot(0));
  }

  if (failures == 0) std::printf("hit_registry: all tests passed\n");
  return failures == 0 ? 0 : 1;
}
