#pragma once

#include "desktop_shell/unified/compositor_kind.hpp"
#include "wl/core/protocols.hpp"
#include "wl/toplevel/ext_foreign_toplevels.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

#include <functional>

struct wl_display;
struct wl_registry;

namespace eh::shell::shared {

// Binds a minimal registry on an arbitrary wl_display to track foreign
// toplevels (plus ext-foreign-toplevel-list where available). This is the role the
// dock's WaylandState used to play for the supervisor + taskbar before the dock
// moved into its own process: the taskbar borrows these containers and the
// ToplevelBridge publishes them onto the IPC bus. Runs on the session's main
// display, which is dispatched by the main event loop.
struct ToplevelTracker {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  CompositorKind compositorKind = CompositorKind::Unknown;
  zwlr_foreign_toplevel_manager_v1* toplevelManager = nullptr;
  ext_foreign_toplevel_list_v1* extToplevelList = nullptr;
  eh::wayland::ExtForeignToplevels extToplevels{};
  eh::wayland::ForeignToplevels toplevels{};
  bool ready = false;
};

// Binds the registry + toplevel manager on `display` and attaches the toplevel
// containers. `visual_dirty` fires when the tracked window list changes. The
// display must already be connected; events flow during normal display dispatch
// (a flush + dispatch is performed here so existing toplevels arrive early).
bool toplevel_tracker_init(ToplevelTracker& tracker, wl_display* display,
                           std::function<void()> visual_dirty);

void toplevel_tracker_cleanup(ToplevelTracker& tracker);

}  // namespace eh::shell::shared
