#include "desktop_shell/shared/toplevel/toplevel_hooks.hpp"

#include "desktop_shell/dock/core/dock_app.h"

#include "desktop_shell/common/bench/debug_profile.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

#include "wl/core/protocols.hpp"

bool dock_foreign_toplevel_debug_enabled() {
   
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_DOCK_DEBUG") ? 1 : 0;
  return cached != 0;
}

void dock_foreign_toplevel_bind_manager_and_hooks(DockApp& app) {
   
  app.toplevels.set_debug(dock_foreign_toplevel_debug_enabled());
  if (!app.toplevelManager) {
    std::cerr << "Warning: zwlr_foreign_toplevel_manager_v1 not available; running apps will not show.\n";
    return;
  }

  app.toplevels.attach(app.toplevelManager, app.display);
  app.toplevels.set_handle_destroyed_hook([&app](zwlr_foreign_toplevel_handle_v1* h) {
    if (app.popupAppChosenHandle == h) app.popupAppChosenHandle = nullptr;
    if (app.pinClickHandle == h) app.pinClickHandle = nullptr;
    app.popupAppWindows.erase(
        std::remove_if(app.popupAppWindows.begin(), app.popupAppWindows.end(),
                       [h](const std::pair<zwlr_foreign_toplevel_handle_v1*, std::string>& p) { return p.first == h; }),
        app.popupAppWindows.end());
  });
  app.toplevels.set_visual_dirty_hook([&app]() {
    // Throttle: fire at most once per 16ms (~60Hz) to coalesce
    // Proton/RuneScape toplevel event floods into a single redraw request.
    using Clock = std::chrono::steady_clock;
    static Clock::time_point s_last_fire{};
    const auto now = Clock::now();
    if (now - s_last_fire < std::chrono::milliseconds(16)) return;
    s_last_fire = now;
    app.deferDockRedraw = true;
    app.sizeDirty = true;
    dock_schedule_frame(app);
  });
}

void dock_ext_toplevels_set_changed_cb(DockApp& app) {
  app.extToplevels.set_changed_cb([&app]() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point s_last_fire{};
    const auto now = Clock::now();
    if (now - s_last_fire < std::chrono::milliseconds(16)) return;
    s_last_fire = now;
    app.deferDockRedraw = true;
    app.sizeDirty = true;
    dock_schedule_frame(app);
  });
}
