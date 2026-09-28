#include "desktop_shell/spotlight/search/spotlight_keyboard.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/spotlight/search/spotlight_query.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"

using eh::shell::str::utf8_pop_back;

namespace {

bool try_seeker_action(DockApp& app, const SpotlightHit& hit) {
  const std::string& a = hit.seekerAction;
  if (a.size() == 7 && a.compare(0, 6, "power:") == 0 && a[6] >= '0' && a[6] <= '3') {
    eh::shell::dock::app_drawer::app_drawer_power_exec(app.compositorKind, a[6] - '0');
    popup_close(app);
    if (app.display) wl_display_flush(app.display);
    return true;
  }
  if (a.compare(0, 7, "window:") == 0) {
    std::uint64_t serial = 0;
    try {
      serial = std::stoull(a.substr(7));
    } catch (...) {
      return false;
    }
    for (const auto& tl : app.toplevels.list()) {
      if (tl.serial == serial && tl.handle) {
        zwlr_foreign_toplevel_handle_v1_activate(tl.handle, app.seat);
        if (app.display) wl_display_flush(app.display);
        break;
      }
    }
    popup_close(app);
    if (app.display) wl_display_flush(app.display);
    return true;
  }
  return false;
}

}

namespace eh::shell::dock::spotlight {

bool spotlight_handle_keyboard(DockApp& app, xkb_keysym_t sym, uint32_t keycode, uint32_t) {
  if (app.popupKind != DockApp::PopupKind::Spotlight) return false;

  if (sym == XKB_KEY_Escape) {
    popup_close(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Up) {
    if (!app.spotlightHits.empty()) {
      if (app.spotlightSel <= 0) {
        app.spotlightSel = static_cast<int>(app.spotlightHits.size()) - 1;
      } else {
        app.spotlightSel--;
      }
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Down) {
    if (!app.spotlightHits.empty()) {
      if (app.spotlightSel < 0) {
        app.spotlightSel = 0;
      } else if (app.spotlightSel >= static_cast<int>(app.spotlightHits.size()) - 1) {
        app.spotlightSel = 0;
      } else {
        app.spotlightSel++;
      }
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (app.spotlightSel >= 0 && app.spotlightSel < static_cast<int>(app.spotlightHits.size())) {
      const auto& hit = app.spotlightHits[static_cast<size_t>(app.spotlightSel)];
      if (try_seeker_action(app, hit)) return true;
      launch_exec_command(hit.exec);
      dock_start_launch_bounce(app, hit.path, true);
      popup_close(app);
      wl_display_flush(app.display);
      return true;
    }
  }
  if (sym == XKB_KEY_BackSpace) {
    utf8_pop_back(app.spotlightQuery);
    dock_spotlight_refresh_results(app);
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }

  char utf8Sp[128]{};
  const int nsp = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8Sp, sizeof(utf8Sp) - 1);
  if (nsp > 0) {
    app.spotlightQuery.append(utf8Sp, static_cast<size_t>(nsp));
    dock_spotlight_refresh_results(app);
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
  return true;
  return true;
}

bool spotlight_handle_click(DockApp& app) {
  if (app.popupKind != DockApp::PopupKind::Spotlight) return false;
  const int row = spotlight_pick_row_index(app, app.pointerX, app.pointerY);
  if (row >= 0 && row < static_cast<int>(app.spotlightHits.size())) {
    const auto& hit = app.spotlightHits[static_cast<size_t>(row)];
    if (try_seeker_action(app, hit)) return true;
    launch_exec_command(hit.exec);
    dock_start_launch_bounce(app, hit.path, true);
    popup_close(app);
    wl_display_flush(app.display);
  }
  return true;
}

}
