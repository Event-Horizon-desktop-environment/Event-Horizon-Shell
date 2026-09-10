#include "desktop_shell/shared/toplevel/toplevel_tracker.hpp"

#include <algorithm>
#include <iostream>

#include <wayland-client.h>

namespace eh::shell::shared {

namespace {

void registry_global(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
  auto& tracker = *static_cast<ToplevelTracker*>(data);
  if (std::string_view(interface) == zwlr_foreign_toplevel_manager_v1_interface.name) {
    tracker.toplevelManager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
                         std::min<uint32_t>(version, 3)));
    return;
  }
  if (std::string_view(interface) == ext_foreign_toplevel_list_v1_interface.name) {
    // ext-foreign-toplevel-list-v1 mirrors zwlr-foreign-toplevel-manager; bind
    // it only where the compositor lacks the standard manager.
    if (!is_hyprland()) return;
    tracker.extToplevelList = static_cast<ext_foreign_toplevel_list_v1*>(
        wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface,
                         std::min<uint32_t>(version, 1)));
    return;
  }
}

void registry_global_remove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener g_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

}  // namespace

bool toplevel_tracker_init(ToplevelTracker& tracker, wl_display* display, std::function<void()> visual_dirty) {
  tracker.display = display;
  tracker.compositorKind = detect_compositor_kind();
  tracker.registry = wl_display_get_registry(display);
  if (!tracker.registry) return false;
  wl_registry_add_listener(tracker.registry, &g_registry_listener, &tracker);
  wl_display_flush(display);
  // Synchronous roundtrip announces the globals; safe because the main loop has
  // not started dispatching this display yet (the dock does the same at init).
  (void)wl_display_roundtrip(display);

  if (!tracker.toplevelManager) {
    std::cerr << "[toplevel-tracker] zwlr_foreign_toplevel_manager_v1 not available; "
                 "running apps will not show on the taskbar\n";
    if (!tracker.extToplevelList) return false;
  }

  std::function<void()> dirty = std::move(visual_dirty);
  tracker.toplevels.set_visual_dirty_hook(dirty);
  if (tracker.toplevelManager) tracker.toplevels.attach(tracker.toplevelManager, display);
  if (tracker.extToplevelList) {
    tracker.extToplevels.bind(tracker.extToplevelList, display);
    tracker.extToplevels.set_changed_cb(dirty);
  }

  wl_display_flush(display);
  // Non-blocking: the compositor only emits foreign-toplevel events when a window maps,
  // so a blocking dispatch on an empty desktop stalls the whole session boot
  // until the user opens the first window. The main poll loop delivers the
  // initial snapshot (if any) asynchronously instead.
  (void)wl_display_dispatch_pending(display);
  tracker.ready = true;
  return true;
}

void toplevel_tracker_cleanup(ToplevelTracker& tracker) {
  tracker.toplevels.shutdown();
  tracker.ready = false;
  tracker.registry = nullptr;
  tracker.toplevelManager = nullptr;
  tracker.extToplevelList = nullptr;
  tracker.display = nullptr;
}

}  // namespace eh::shell::shared
