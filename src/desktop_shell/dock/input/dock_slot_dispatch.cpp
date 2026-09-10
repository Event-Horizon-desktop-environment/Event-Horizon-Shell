#include "desktop_shell/dock/input/dock_slot_dispatch.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/shared/popup/dispatch/popup_dispatch.hpp"
#include "desktop_shell/dock/input/dock_pick.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/dock/layout/dock_layout_shared.hpp"
#include "desktop_shell/shared/core/app_launch.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/dock/pinned/dock_pin_identity.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/common/fs/trash_state.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/launchpad/host/launchpad_host.hpp"

#include <iostream>

using eh::shell::paths::normalize_desktop_app_id;

namespace eh::shell::dock {

void dock_handle_slot_press(DockApp& app, uint32_t serial, bool left, bool right, bool onDockSurface) {
  const DockPickResult pr = dock_pick_at(app, app.pointerX, app.pointerY);
  if ((app.popupOpen || (app.launchpad && app.launchpad->is_open())) && onDockSurface && !dock_popup_pointer_on_any_popup_surface(app) && left) {
    bool keep = false;
    if (pr.idx >= 0) {
      const auto& h = pr.all[static_cast<size_t>(pr.idx)];
      keep = eh::shell::popup::popup_keep_open_for_slot(app.popupKind, static_cast<int>(h.kind), h.key);
      if (!keep && app.launchpad && app.launchpad->is_open() && h.kind == PickSlot::Kind::Launchpad) keep = true;
    }
    if (!keep) {
      popup_close(app);
      dock_draw(app);
      wl_display_flush(app.display);
    }
  }
  app.dockPressedSlot = (pr.idx >= 0 && pr.all[static_cast<size_t>(pr.idx)].kind != PickSlot::Kind::Clock &&
                         pr.all[static_cast<size_t>(pr.idx)].kind != PickSlot::Kind::Weather)
                            ? pr.idx
                            : -1;
  dock_draw(app);
  wl_display_flush(app.display);

  if (pr.idx < 0) return;
  const auto& hit = pr.all[static_cast<size_t>(pr.idx)];
  if (hit.kind == PickSlot::Kind::Clock) {
    if (app.popupDismissedThisPress == DockApp::PopupKind::Calendar) {
      app.popupDismissedThisPress = DockApp::PopupKind::None;
      dock_draw(app);
      wl_display_flush(app.display);
      return;
    }
    if (app.popupOpen && app.popupKind == DockApp::PopupKind::Calendar) {
      popup_close(app);
      dock_draw(app);
      wl_display_flush(app.display);
      return;
    }
    const double slotCenterX = dock_strip_slot_center_x(app, pr, pr.idx);
    popup_open_calendar(app, static_cast<int>(slotCenterX), serial);
    return;
  }
  if (hit.kind == PickSlot::Kind::Weather) {
    if (app.popupDismissedThisPress == DockApp::PopupKind::Weather) {
      app.popupDismissedThisPress = DockApp::PopupKind::None;
      dock_draw(app);
      wl_display_flush(app.display);
      return;
    }
    if (app.popupOpen && app.popupKind == DockApp::PopupKind::Weather) {
      popup_close(app);
      dock_draw(app);
      wl_display_flush(app.display);
      return;
    }
    const double slotCenterX = dock_strip_slot_center_x(app, pr, pr.idx);
    popup_open_weather(app, static_cast<int>(slotCenterX), hit.key, serial);
    return;
  }
  {
    const double slotCenterX = dock_strip_slot_center_x(app, pr, pr.idx);
    if (eh::shell::popup::popup_dispatch_slot(app, static_cast<int>(hit.kind), static_cast<int>(slotCenterX), serial)) return;
  }

  if (hit.kind == PickSlot::Kind::Media) {
    if (!app.mpris) return;
    const auto xw = dock_strip_slot_xw(app, pr, pr.idx);
    const int zone = eh::mpris::DockMpris::media_hit_zone(
        app.pointerX - xw.first, xw.second,
        static_cast<double>(dock_effective_icon_px(app.settings)));
    if (zone == 0) app.mpris->previous();
    else if (zone == 2) app.mpris->next();
    else if (zone == 1) app.mpris->play_pause();
    else {
      if (app.popupDismissedThisPress == DockApp::PopupKind::MediaPlayer) {
        app.popupDismissedThisPress = DockApp::PopupKind::None;
        dock_draw(app);
        wl_display_flush(app.display);
        return;
      }
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::MediaPlayer) {
        popup_close(app);
        dock_draw(app);
        wl_display_flush(app.display);
        return;
      }
      popup_open_media_player(app, static_cast<int>(xw.first + xw.second / 2.0), serial);
    }
    return;
  }

  if (hit.kind == PickSlot::Kind::Workspaces) {
    const int pick = dock_pick_workspace_index(app, pr, pr.idx, app.pointerX);
    if (pick >= 0 && static_cast<size_t>(pick) < app.workspaceStrip.size()) {
      const auto& entry = app.workspaceStrip[static_cast<size_t>(pick)];
      dock_slot_hooks::workspace_activate_entry(entry, app.compositorKind);
    }
    return;
  }

  if (hit.kind == PickSlot::Kind::Tray || hit.key.rfind(kSlotKeyTray, 0) == 0) {
    const std::string want = hit.key.substr(std::string(kSlotKeyTray).size());
    const DockApp::TrayItem* ti = nullptr;
    for (const auto& cand : pr.traySnap) {
      if ((cand.service + cand.path) == want) { ti = &cand; break; }
    }
    if (!ti) return;
    std::cout << "[input] tray_" << (right ? "menu" : "activate") << ": service='" << ti->service << "' path='" << ti->path << "'\n";
    if (right) {
      popup_open_for_tray(app, *ti, static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), serial);
    } else {
      try {
        auto proxy = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti->service}, sdbus::ObjectPath{ti->path});
        proxy->callMethod("Activate")
          .onInterface("org.kde.StatusNotifierItem")
          .withArguments(static_cast<int32_t>(app.pointerX), static_cast<int32_t>(app.pointerY));
      } catch (const std::exception& e) {
        eh::shell_log::dbus_tray("activate failed: ", e.what());
      }
    }
    return;
  }

  if (hit.kind == PickSlot::Kind::Settings || hit.key == kSlotKeySettings) {
    if (right) {
      app.popupAppWindows.clear();
      app.popupAppDesktopActions.clear();
      app.popupAppDesktopExec.clear();
      {
        const auto* settingsTl = dock_toplevel_by_serial(app, pr.settingsChosenSerial);
        popup_open_for_app(app, std::string(kSlotKeySettings), settingsTl ? settingsTl->handle : nullptr,
                           static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), serial);
      }
      return;
    }
    if (const auto* settingsTl = dock_toplevel_by_serial(app, pr.settingsChosenSerial);
        settingsTl && settingsTl->handle) {
      std::cout << "[input] activate_settings_window\n";
      zwlr_foreign_toplevel_handle_v1_activate(settingsTl->handle, app.seat);
      wl_display_flush(app.display);
    } else {
      std::cout << "[input] open_settings\n";
      launch_settings_app(app);
    }
    return;
  }

  if (hit.kind == PickSlot::Kind::Spotlight || hit.key == kSlotKeySpotlight) {
    std::cout << "[input] spotlight_popup\n";
    if (app.popupOpen && app.popupKind == DockApp::PopupKind::Spotlight) {
      popup_close(app);
      wl_display_flush(app.display);
      return;
    }
    popup_open_spotlight(app, static_cast<int>(app.pointerX), serial);
    return;
  }

  if (hit.kind == PickSlot::Kind::Launchpad) {
    std::cout << "[input] launchpad_popup\n";
    if (eh_app_drawer_debug_level() >= 1) {
      app_drawer::trace_line(1, "dock",
                             "launchpad open request pick_idx=" + std::to_string(pr.idx) + " key=" + hit.key);
    }
    if (app.launchpad && app.launchpad->is_open()) {
      if (eh_app_drawer_debug_level() >= 1) app_drawer::trace_line(1, "dock", "toggle close Launchpad");
      app.launchpad->close();
      wl_display_flush(app.display);
      return;
    }
    if (app.launchpad) {
      const int slotCenterX = static_cast<int>(std::round(dock_strip_slot_center_x(app, pr, pr.idx)));
      if (eh_app_drawer_debug_level() >= 1) {
        app_drawer::trace_line(1, "dock", "open Launchpad from dock slotCenterX=" + std::to_string(slotCenterX));
      }
      app.launchpad->toggle(slotCenterX, serial);
    } else {
      std::cerr << "[launchpad] widget clicked but DockApp::launchpad is null (Host not wired for this session)\n";
    }
    return;
  }

  if (hit.kind == PickSlot::Kind::Trash) {
    if (right) {
      app.trashFull = eh::shell::fs::trash_has_files();
      std::vector<DockApp::PopupMenuItem> items;
      items.push_back({.label = "Open Trash", .id = 1, .enabled = true});
      items.push_back({.label = "", .id = -1, .enabled = false});
      items.push_back({.label = "Empty Trash", .id = 2, .enabled = app.trashFull});
      app.popupItems = std::move(items);
      popup_open_for_trash(app, static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), serial);
      return;
    }
    launch_exec_command("xdg-open trash:///");
    return;
  }

  if (hit.kind == PickSlot::Kind::AppMenu || hit.key == kSlotKeyAppMenu || hit.kind == PickSlot::Kind::AppDrawer ||
      hit.key == kSlotKeyAppDrawer || hit.kind == PickSlot::Kind::Smenu) {
    app.appMenuSmenuMode = (hit.kind == PickSlot::Kind::Smenu);
    if (app.appMenuSmenuMode) eh_app_drawer_update_categories(app);
    std::cout << "[input] app_menu_popup\n";
    if (eh_app_drawer_debug_level() >= 1) {
      app_drawer::trace_line(1, "dock",
                             "widget open request pick_idx=" + std::to_string(pr.idx) + " key=" + hit.key);
    }
    if (app.popupOpen && app.popupKind == DockApp::PopupKind::AppMenu) {
      if (eh_app_drawer_debug_level() >= 1) app_drawer::trace_line(1, "dock", "toggle close AppMenu");
      popup_close(app);
      wl_display_flush(app.display);
      return;
    }

    {
      const int slotCenterX = static_cast<int>(std::round(dock_strip_slot_center_x(app, pr, pr.idx)));
      if (eh_app_drawer_debug_level() >= 1) {
        app_drawer::trace_line(1, "dock", "open AppMenu from dock slotCenterX=" + std::to_string(slotCenterX));
      }
      popup_open_app_menu(app, slotCenterX, serial);
    }
    return;
  }

  const auto* chosen = dock_toplevel_by_serial(app, hit.chosenSerial);
  if ((!chosen || !chosen->handle) && hit.isPinned && right) {
    app.popupAppWindows.clear();
    app.popupAppDesktopActions.clear();
    app.popupAppDesktopExec.clear();
    popup_open_for_app(app, hit.key, nullptr, static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), serial);
    return;
  }
  if (hit.isPinned && left) {
    app.pinDragPinsSnapshot = app.settings.pinnedApps;
    app.pinDragPaintOrder.clear();
    app.pinDragCandidate = true;
    app.pinDragging = false;
    app.pinDragDirty = false;
    app.pinDragInsertIdx = -1;
    app.pinDragLayerOnlyNextDraw = false;
    app.pinDragPerfInsertChanges = 0;
    app.pinDragPerfGhostCoalesce = 0;
    app.pinDragPerfScheduleNoop = 0;
    app.pinDragStartX = app.pointerX;
    app.pinDragStartY = app.pointerY;

    std::string rawPin = hit.key;
    for (const auto& p : app.settings.pinnedApps) {
      if (eh::shell::paths::normalize_desktop_app_id(p) == hit.key) {
        rawPin = p;
        break;
      }
    }
    app.pinDragKeyRaw = rawPin;
    app.pinDragKey = eh::shell::paths::normalize_desktop_app_id(rawPin);
    app.pinClickHandle = (chosen && chosen->handle) ? chosen->handle : nullptr;
    return;
  }
  if (!chosen || !chosen->handle) return;
  if (right) {
    app.popupAppWindows.clear();
    app.popupAppDesktopActions.clear();
    app.popupAppDesktopExec.clear();
    for (const auto& tl : app.toplevels) {
      if (!tl.handle || tl.closed) continue;
      if (dock_pin_identity_norm_matches_anchor(app, hit.key, normalize_desktop_app_id(tl.appId))) {
        app.popupAppWindows.push_back({tl.handle, tl.title});
      }
    }
    popup_open_for_app(app, hit.key, chosen->handle, static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), serial);
    return;
  }
  std::cout << "[input] activate: app_id='" << chosen->appId << "' title='" << chosen->title << "'\n";
  zwlr_foreign_toplevel_handle_v1_activate(chosen->handle, app.seat);
  wl_display_flush(app.display);
  dock_start_launch_bounce(app, hit.key, false);
}

}
