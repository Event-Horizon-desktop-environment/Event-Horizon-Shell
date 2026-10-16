#pragma once

// Desk and toplevel value types shared by every compositor backend.
//
// These are deliberately plain aggregates: backends fill them, the UI reads
// them. Nothing here knows about a wire format — that stays in each backend's
// codec module.
//
// Vocabulary note: names in this header are project-owned and must not track
// any other project's terminology. See Docs/Audit/PROVENANCE.md §7.

#include <cstdint>
#include <string>
#include <vector>

// How the bar decides whether a desk is "the same desk" as a candidate row.
//
// Uniform: every desk matches on identity alone. Used when the compositor
// gives stable desk identifiers.
//
// PerOccupancyTitle: identity first, then title, then ordinal position. Used
// by compositors whose desk numbering shifts as desks are created and
// destroyed, so a bare number is not a stable handle.
enum class BarMatchPolicy : std::uint8_t {
  Uniform,
  PerOccupancyTitle,
};

// One desk (workspace / tag / virtual desktop) as exposed to the UI.
//
// Field grouping is intentional: identity and display live in the first
// block, position in the middle, and the three independent state bits are
// collapsed into a single flags word so callers can copy state cheaply.
struct DeskSlot {
  // Stable handle for this desk within its output. Opaque to the UI.
  std::string key;
  // Human-readable label. Falls back to `key` when the compositor sends none.
  std::string label;
  // Path segments describing the desk's position in a desk tree, when the
  // compositor exposes a hierarchy. Flat compositors leave this empty.
  std::vector<std::uint32_t> path;
  // 1-based display position, or 0 when the compositor reports no position.
  std::uint32_t ordinal = 0;

  // State bits. Kept together so a backend can assign them in one expression
  // and the UI can test them without three separate member reads.
  struct {
    // This desk is the one currently shown on its output.
    bool selected = false;
    // Something on this desk is asking for attention.
    bool urgent = false;
    // At least one toplevel is mapped on this desk.
    bool occupied = false;
  } state;
};

// A toplevel window, as tracked for bar and overview purposes.
struct ToplevelRef {
  // Compositor-specific window handle. Opaque to the UI.
  std::string handle;
  // Key of the desk this window sits on. Empty when not yet assigned.
  std::string deskKey;
  // Application id (wayland app_id / x11 class).
  std::string app;
  std::string title;
  // Geometry origin, in layout coordinates.
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// A row the bar wants to resolve against the live desk set.
struct BarItem {
  // The bar's own opaque handle for this row.
  std::uintptr_t row = 0;
  // Applications this row should light up when they appear on a desk.
  std::vector<std::string> apps;
  std::string title;
};
