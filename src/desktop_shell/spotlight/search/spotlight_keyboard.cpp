#include "desktop_shell/spotlight/search/spotlight_keyboard.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/spotlight/search/spotlight_query.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/common/fs/string_util.hpp"

using eh::shell::str::utf8_pop_back;

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
      launch_exec_command(app.spotlightHits[static_cast<size_t>(app.spotlightSel)].exec);
      dock_start_launch_bounce(app, app.spotlightHits[static_cast<size_t>(app.spotlightSel)].path, true);
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
    launch_exec_command(app.spotlightHits[static_cast<size_t>(row)].exec);
    dock_start_launch_bounce(app, app.spotlightHits[static_cast<size_t>(row)].path, true);
    popup_close(app);
    wl_display_flush(app.display);
  }
  return true;
}

}
