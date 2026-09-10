#include "desktop_shell/widgets/app_drawer/input/app_drawer_keyboard.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"

#include <chrono>
#include <iostream>

using eh::shell::str::utf8_pop_back;

namespace eh::shell::dock::app_drawer {

bool app_drawer_handle_keyboard(DockApp& app, xkb_keysym_t sym, uint32_t keycode, uint32_t state) {
  if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return true;
    if (sym == XKB_KEY_Escape) {
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      popup_close(app);
      wl_display_flush(app.display);
      return true;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      const int idx = app.appMenuPowerConfirmIdx;
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      app_drawer_power_exec(app.compositorKind, idx);
      popup_close(app);
      wl_display_flush(app.display);
      return true;
    }
    return true;
  }

  if (!dock_popup_kind_uses_app_drawer_ui(app.popupKind)) return false;

  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "dock.kbd",
               "AppMenu key state=" + std::to_string(static_cast<int>(state)) + " sym=" +
                   std::to_string(static_cast<int>(sym)) + " searchFocused=" +
                   std::string(app.appMenuSearchFocused ? "1" : "0"));
  }

  if (sym == XKB_KEY_Escape) {
    if (app.appMenuPowerConfirmOpen) {
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (app.appMenuPinCtxOpen) {
      app.appMenuPinCtxOpen = false;
      app.appMenuPinCtxHoverItem = -1;
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (app.appMenuRowCtxOpen) {
      app.appMenuRowCtxOpen = false;
      app.appMenuRowCtxHoverItem = -1;
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (eh_app_drawer_debug_level() >= 1) trace_line(1, "dock.kbd", "AppMenu Escape -> close");
    popup_close(app);
    wl_display_flush(app.display);
    return true;
  }

  if (app.appMenuSearchFocused) {
    if (sym == XKB_KEY_Down) {
      app.appMenuSearchFocused = false;
      if (!app.appMenuHits.empty() && app.appMenuSel < 0) app.appMenuSel = 0;
      eh_app_drawer_ensure_sel_visible(app);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (sym == XKB_KEY_BackSpace) {
      const auto t_kbd = ShellBenchClock::now();
      utf8_pop_back(app.appMenuQuery);
      eh_app_drawer_refresh_hits(app);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t_kbd).count();
        std::cerr << "[search-bench] e2e_backspace query=\"" << app.appMenuQuery << "\" " << us << "us" << std::endl;
      }
      return true;
    }
    char utf8Sf[128]{};
    const int nsf = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8Sf, sizeof(utf8Sf) - 1);
    if (nsf > 0) {
      const auto t_kbd = ShellBenchClock::now();
      app.appMenuQuery.append(utf8Sf, static_cast<size_t>(nsf));
      eh_app_drawer_refresh_hits(app);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t_kbd).count();
        std::cerr << "[search-bench] e2e_keypress query=\"" << app.appMenuQuery << "\" " << us << "us" << std::endl;
      }
      return true;
    }
  }

  if (sym == XKB_KEY_Up) {
    if (!app.appMenuHits.empty()) {
      if (app.appMenuSel <= 0) {
        app.appMenuSel = static_cast<int>(app.appMenuHits.size()) - 1;
      } else {
        app.appMenuSel--;
      }
      eh_app_drawer_ensure_sel_visible(app);
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Down) {
    if (!app.appMenuHits.empty()) {
      if (app.appMenuSel < 0) {
        app.appMenuSel = 0;
      } else if (app.appMenuSel >= static_cast<int>(app.appMenuHits.size()) - 1) {
        app.appMenuSel = 0;
      } else {
        app.appMenuSel++;
      }
      eh_app_drawer_ensure_sel_visible(app);
    }
    popup_draw_surface(app);
    wl_display_flush(app.display);
    return true;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (app.appMenuSel >= 0 && app.appMenuSel < static_cast<int>(app.appMenuHits.size())) {
      launch_exec_command(app.appMenuHits[static_cast<size_t>(app.appMenuSel)].exec);
      dock_start_launch_bounce(app, app.appMenuHits[static_cast<size_t>(app.appMenuSel)].path, true);
      popup_close(app);
      wl_display_flush(app.display);
      return true;
    }
  }

  return false;
}

}
