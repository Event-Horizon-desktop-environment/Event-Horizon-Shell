#pragma once

// Track-change decision for MPRIS partial Metadata merges.
//
// Players (notably browsers) often send partial Metadata dicts, e.g. a
// title-only update on track change. Merging that over the previous track's
// cached fields yields a mixed snapshot (new title + old artist) which then
// emits as a bogus notification. The merge site must therefore detect a track
// change from the *incoming* dict BEFORE merging and reset identity fields
// first. Pure logic, unit-tested in test/test_mpris_track_change.cpp.

#include <string>

namespace eh::mpris {

// Returns true when the incoming partial Metadata belongs to a different
// track than the cached one. Prefers mpris:trackid when either side has one;
// falls back to xesam:title comparison when neither side ever had an id
// (mirrors the position-reset heuristic in DockMpris).
[[nodiscard]] inline bool track_changed(const std::string& oldTrackId,
                                        const std::string& oldTitle, bool hasTrackId,
                                        const std::string& incomingTrackId, bool hasTitle,
                                        const std::string& incomingTitle) {
  if (hasTrackId || !oldTrackId.empty()) {
    return incomingTrackId != oldTrackId;
  }
  if (hasTitle) {
    return incomingTitle != oldTitle;
  }
  return false;
}

}  // namespace eh::mpris
