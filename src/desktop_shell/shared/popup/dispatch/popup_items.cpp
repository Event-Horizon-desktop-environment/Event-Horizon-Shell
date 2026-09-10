#include "desktop_shell/shared/popup/dispatch/popup_items.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/common/fs/trash_state.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/shared/core/app_launch.hpp"
#include "desktop_shell/shared/toplevel/toplevel_hooks.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <iostream>
#include <sdbus-c++/sdbus-c++.h>

using eh::shell::paths::normalize_desktop_app_id;

namespace eh::shell::popup {

bool popup_handle_items_motion(DockApp& app) {
  if (!app.popupOpen) return false;
  if (!dock_popup_pointer_on_any_popup_surface(app)) return false;
  if (app.popupKind != DockApp::PopupKind::Tray &&
      app.popupKind != DockApp::PopupKind::App &&
      app.popupKind != DockApp::PopupKind::Trash) return false;

  const int rowH = 26;
  int y = 4;
  int next = -1;
  for (size_t i = 0; i < app.popupItems.size(); ++i) {
    const auto& it = app.popupItems[i];
    if (it.id < 0) { y += rowH / 2; continue; }
    if (app.pointerY >= y && app.pointerY < (y + rowH)) { next = static_cast<int>(i); break; }
    y += rowH;
  }
  if (next != app.popupHoverItem) {
    app.popupHoverItem = next;
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
  return true;
}

bool popup_handle_items_click(DockApp& app) {
  if (!app.popupOpen) return false;
  if (!dock_popup_pointer_on_any_popup_surface(app)) return false;

  const int rowH = 26;
  int y = 4;
  for (const auto& it : app.popupItems) {
    if (it.id < 0) {
      y += rowH / 2;
      continue;
    }
    const bool hit = (app.pointerY >= y && app.pointerY < (y + rowH));
    if (hit) {
      if (!it.enabled) return true;
      if (app.popupKind == DockApp::PopupKind::Tray) {
        std::cout << "[tray] menu_click: id=" << it.id << " label='" << it.label << "'\n";
        try {
          auto menu = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{app.popupService}, sdbus::ObjectPath{app.popupMenuPath});
          menu->callMethod("Event")
            .onInterface("com.canonical.dbusmenu")
            .withArguments(int32_t{it.id}, std::string("clicked"), sdbus::Variant(int32_t{0}), uint32_t{0});
        } catch (const std::exception& e) {
          eh::shell_log::dbus_tray("menu event failed: ", e.what());
        }
      } else if (app.popupKind == DockApp::PopupKind::Trash) {
        if (it.id == 1) {
          launch_exec_command("xdg-open trash:///");
        } else if (it.id == 2) {
          const char* home = std::getenv("HOME");
          if (home) {
            const std::string trashFiles = std::string(home) + "/.local/share/Trash/files";
            const std::string trashInfo = std::string(home) + "/.local/share/Trash/info";
            launch_exec_command("rm -rf " + trashFiles + "/* " + trashInfo + "/*");
            app.trashFull = eh::shell::fs::trash_has_files();
          }
        }
      } else if (app.popupKind == DockApp::PopupKind::App) {
        std::cout << "[dock] app_menu_click: key='" << app.popupAppKey << "' action=" << it.id << " label='" << it.label << "'\n";
        const int32_t action = it.id;
        if (action >= 1000 && action < 2000) {
          const size_t idx = static_cast<size_t>(action - 1000);
          if (idx < app.popupAppDesktopActions.size()) {
            const auto& a = app.popupAppDesktopActions[idx];
            std::cout << "[dock] desktop_action: id='" << a.id << "' name='" << a.name << "'\n";
            launch_exec_command(a.exec);
            dock_start_launch_bounce(app, app.popupAppKey, true);
          }
        } else if (action >= 2000 && action < 3000) {
          const size_t idx = static_cast<size_t>(action - 2000);
          if (idx < app.popupAppWindows.size()) {
            auto* h = app.popupAppWindows[idx].first;
            if (h) {
              zwlr_foreign_toplevel_handle_v1_activate(h, app.seat);
              wl_display_flush(app.display);
              dock_start_launch_bounce(app, app.popupAppKey, false);
            }
          }
        } else if (action == 3000) {
          launch_exec_command(app.popupAppDesktopExec);
          dock_start_launch_bounce(app, app.popupAppKey, true);
        } else if (action == static_cast<int32_t>(AppPopupAction::PinToggle)) {
          dock_pinned_toggle(app, app.popupAppKey);
        } else if (action == static_cast<int32_t>(AppPopupAction::MinimizeOne)) {
          if (app.popupAppChosenHandle) {
            zwlr_foreign_toplevel_handle_v1_set_minimized(app.popupAppChosenHandle);
            wl_display_flush(app.display);
          }
        } else if (action == static_cast<int32_t>(AppPopupAction::ToggleMaximize)) {
          bool isMax = false;
          for (const auto& tl : app.toplevels) {
            if (tl.handle == app.popupAppChosenHandle) { isMax = tl.maximized; break; }
          }
          if (app.popupAppChosenHandle) {
            if (isMax) zwlr_foreign_toplevel_handle_v1_unset_maximized(app.popupAppChosenHandle);
            else zwlr_foreign_toplevel_handle_v1_set_maximized(app.popupAppChosenHandle);
            wl_display_flush(app.display);
          }
        } else if (action == static_cast<int32_t>(AppPopupAction::ToggleFullscreen)) {
          bool isFs = false;
          for (const auto& tl : app.toplevels) {
            if (tl.handle == app.popupAppChosenHandle) { isFs = tl.fullscreen; break; }
          }
          if (app.popupAppChosenHandle) {
            if (isFs) zwlr_foreign_toplevel_handle_v1_unset_fullscreen(app.popupAppChosenHandle);
            else zwlr_foreign_toplevel_handle_v1_set_fullscreen(app.popupAppChosenHandle, nullptr);
            wl_display_flush(app.display);
          }
        } else if (action == static_cast<int32_t>(AppPopupAction::CloseOne)) {
          if (app.popupAppChosenHandle) {
            zwlr_foreign_toplevel_handle_v1_close(app.popupAppChosenHandle);
            wl_display_flush(app.display);
          }
        } else if (action == static_cast<int32_t>(AppPopupAction::CloseAll)) {
          for (const auto& tl : app.toplevels) {
            if (!tl.handle || tl.closed) continue;
            const std::string key = normalize_desktop_app_id(tl.appId);
            if (dock_pin_identity_norm_matches_anchor(app, app.popupAppKey, key)) {
              zwlr_foreign_toplevel_handle_v1_close(tl.handle);
            }
          }
          wl_display_flush(app.display);
        }
      }
      popup_close(app);
      return true;
    }
    y += rowH;
  }
  return true;
}

}
