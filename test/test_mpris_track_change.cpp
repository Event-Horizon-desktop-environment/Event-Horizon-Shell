// test_mpris_track_change.cpp — unit tests for
// services/mpris/mpris_track_change.hpp (partial-Metadata track detection).
// Pure logic, no D-Bus needed.

#include "services/mpris/mpris_track_change.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace eh::mpris;

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      ++failures;                                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
    }                                                                          \
  } while (0)

// (oldTrackId, oldTitle, hasTrackId, incomingTrackId, hasTitle, incomingTitle)
int main() {
  // Steady state: identical full dicts -> no change.
  CHECK(!track_changed("/t/1", "A", true, "/t/1", true, "A"));
  // Track id change -> new track.
  CHECK(track_changed("/t/1", "A", true, "/t/2", true, "B"));
  // THE BUG: title-only partial on track change (no ids anywhere) -> new track,
  // so the merge site resets artist/album instead of mixing.
  CHECK(track_changed("", "Apocalyptica - X", false, "", true, "Spiritbox - Mourning"));
  // Same title repeated (e.g. re-emit) -> no change.
  CHECK(!track_changed("", "Same", false, "", true, "Same"));
  // Same-track tweak carrying the same trackid -> no change (artist kept).
  CHECK(!track_changed("/t/1", "A", true, "/t/1", true, "A live"));
  // Partial dict without trackid while the old entry had one -> treat as new
  // (id vanished; matches the position-reset philosophy).
  CHECK(track_changed("/t/1", "A", false, "", true, "B"));
  // Empty incoming dict, old had an id -> new (degenerate disappearance).
  CHECK(track_changed("/t/1", "A", false, "", false, ""));
  // Empty incoming dict, nothing cached -> no change.
  CHECK(!track_changed("", "", false, "", false, ""));
  // Artist-only update (no title key): title unknown, keep merging.
  // hasTitle=false, no ids -> false regardless of artist content.
  CHECK(!track_changed("", "A", false, "", false, ""));
  // Old id present, incoming same id, title tweaked -> same track.
  CHECK(!track_changed("/t/9", "A", true, "/t/9", true, "A (live)"));

  if (failures == 0) std::printf("test_mpris_track_change: all %d checks passed\n", 10);
  return failures == 0 ? 0 : 1;
}
