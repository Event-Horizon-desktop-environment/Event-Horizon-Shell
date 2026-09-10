#include "desktop_shell/widgets/app_drawer/input/app_drawer_dispatch.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/widgets/start_menu/start_menu_zone.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/power_confirm/power_confirm.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/dock/core/dock_settings.hpp"

#include <string>

namespace eh::shell::dock::app_drawer {

constexpr int kAppDrawerRowContextItemMax = 3;
void app_drawer_row_context_menu_layout(double menuAnchorX, double menuAnchorY, double popupW, double popupH,
                                        int itemCount, double* outMenuX, double* outMenuY, double* outMenuW, double* outMenuH);
int app_drawer_row_context_menu_pick(double lx, double ly, double menuX, double menuY, int itemCount);

bool app_drawer_handle_pointer_motion(DockApp& app) {
  if (!app.popupOpen) return false;
  if (!dock_popup_kind_uses_app_drawer_ui(app.popupKind)) return false;
  if (!dock_popup_pointer_on_any_popup_surface(app)) return false;

  const auto z = eh_app_drawer_hit_zone(app, app.pointerX, app.pointerY);
  const int zi = static_cast<int>(z);
  if (eh_app_drawer_debug_level() >= 3 && zi != g_dock_appmenu_trace_last_zone) {
    g_dock_appmenu_trace_last_zone = zi;
    trace_line(3, "dock.motion",
               std::string("AppMenu zone=") + dock_appdrawer_zone_cstr(z) + " (" + std::to_string(zi) + ") xy=(" +
                   std::to_string(app.pointerX) + "," + std::to_string(app.pointerY) + ")");
  }
  if (app.appMenuPinCtxOpen) {
    double mx = 0, my = 0, mw = 0, mh = 0;
    app_drawer_row_context_menu_layout(
        app.appMenuPinCtxMenuX, app.appMenuPinCtxMenuY, static_cast<double>(app.popupW), static_cast<double>(app.popupH), 2,
        &mx, &my, &mw, &mh);
    const int hi = app_drawer_row_context_menu_pick(app.pointerX, app.pointerY, mx, my, 2);
    if (hi != app.appMenuPinCtxHoverItem) {
      app.appMenuPinCtxHoverItem = hi;
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
  } else if (app.appMenuRowCtxOpen) {
    double mx = 0, my = 0, mw = 0, mh = 0;
    const int n = std::clamp(app.appMenuRowCtxItemCount, 2, kAppDrawerRowContextItemMax);
    app_drawer_row_context_menu_layout(
        app.appMenuRowCtxMenuX, app.appMenuRowCtxMenuY, static_cast<double>(app.popupW), static_cast<double>(app.popupH), n,
        &mx, &my, &mw, &mh);
    const int hi = app_drawer_row_context_menu_pick(app.pointerX, app.pointerY, mx, my, n);
    if (hi != app.appMenuRowCtxHoverItem) {
      app.appMenuRowCtxHoverItem = hi;
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
  } else {
    int nextHover = -1;
    if (z == AppDrawerHitZone::AppListRow) nextHover = eh_app_drawer_pick_row_index(app, app.pointerX, app.pointerY);
    int nextCatH = -1;
    if (z == AppDrawerHitZone::CategoryTab) nextCatH = eh_app_drawer_pick_category_tab(app, app.pointerX, app.pointerY);
    int nextPinH = -1;
    if (z == AppDrawerHitZone::PinnedApp) nextPinH = eh_app_drawer_pick_pinned_index(app, app.pointerX, app.pointerY);
    int nextPowerH = -1;
    if (z == AppDrawerHitZone::PowerButton) nextPowerH = eh_app_drawer_pick_power_index(app, app.pointerX, app.pointerY);
    else if (z == AppDrawerHitZone::NightlightButton) nextPowerH = 4;
    const bool hz = nextHover != app.appMenuHoverRow;
    const bool hc = nextCatH != app.appMenuCategoryHoverIdx;
    const bool hp = nextPinH != app.appMenuDrawerPinHoverIdx;
    const bool hw = nextPowerH != app.appMenuPowerHoverIdx;
    if (hz) app.appMenuHoverRow = nextHover;
    if (hc) app.appMenuCategoryHoverIdx = nextCatH;
    if (hp) app.appMenuDrawerPinHoverIdx = nextPinH;
    if (hw) app.appMenuPowerHoverIdx = nextPowerH;
    if (hz || hc || hp || hw) {
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
  }
  if (app.appMenuPowerConfirmOpen) {
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
  return true;
}

bool app_drawer_handle_click(DockApp& app, bool left, bool right) {
  if (!app.popupOpen) return false;
  if (!dock_popup_pointer_on_any_popup_surface(app)) return false;

  if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
    if (!left) return true;
    using P = eh::power_confirm::Pick;
    const double us = dock_ui_scale(app.settings);
    const P pick = eh::power_confirm::pick(
        static_cast<double>(app.popupW), static_cast<double>(app.popupH),
        app.appMenuPowerConfirmIdx, app.pointerX, app.pointerY, us);
    if (pick == P::Confirm) {
      const int idx = app.appMenuPowerConfirmIdx;
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      app_drawer_power_exec(app.compositorKind, idx);
      popup_close(app);
    } else if (pick == P::Cancel || pick == P::Close) {
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      popup_close(app);
    }
    wl_display_flush(app.display);
    return true;
  }

  if (app.appMenuPowerConfirmOpen) {
    if (!left) return true;
    using P = PowerConfirmPick;
    const P pick = pick_power_confirm_modal(static_cast<double>(app.popupW), static_cast<double>(app.popupH),
                                            app.appMenuPowerConfirmIdx, app.pointerX, app.pointerY);
    if (pick == P::Confirm) {
      app_drawer_power_exec(app.compositorKind, app.appMenuPowerConfirmIdx);
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      popup_close(app);
    } else {
      app.appMenuPowerConfirmOpen = false;
      app.appMenuPowerConfirmIdx = -1;
      popup_draw_surface(app);
    }
    wl_display_flush(app.display);
    return true;
  }
  if (dock_popup_kind_uses_app_drawer_ui(app.popupKind)) {
    if (right && !app.appMenuPowerConfirmOpen) {
      const auto zr = eh_app_drawer_hit_zone(app, app.pointerX, app.pointerY);
      if (zr == AppDrawerHitZone::AppListRow) {
        const int rowR = eh_app_drawer_pick_row_index(app, app.pointerX, app.pointerY);
        if (rowR >= 0 && rowR < static_cast<int>(app.appMenuHits.size())) {
          app.appMenuPinCtxOpen = false;
          app.appMenuPinCtxHoverItem = -1;
          app.appMenuRowCtxAnchorRow = rowR;
          app.appMenuRowCtxMenuX = app.pointerX;
          app.appMenuRowCtxMenuY = app.pointerY;
          const std::string& rp = app.appMenuHits[static_cast<size_t>(rowR)].path;
          app.appMenuRowCtxPinnedDock = dock_is_app_pinned(app, rp);
          app.appMenuRowCtxPinnedStart = dock_is_start_menu_pinned(app, rp);
          app.appMenuRowCtxPinnedDrawer = dock_is_drawer_pinned(app, rp);
          app.appMenuRowCtxItemCount = 3;
          app.appMenuRowCtxHoverItem = -1;
          app.appMenuRowCtxOpen = true;
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      if (zr == AppDrawerHitZone::PinnedApp) {
        const int pi = eh_app_drawer_pick_pinned_index(app, app.pointerX, app.pointerY);
        if (pi >= 0 && pi < static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()))) {
          app.appMenuRowCtxOpen = false;
          app.appMenuRowCtxHoverItem = -1;
          app.appMenuPinCtxAnchorIdx = pi;
          app.appMenuPinCtxMenuX = app.pointerX;
          app.appMenuPinCtxMenuY = app.pointerY;
          const std::string& pk = app.settings.drawerPinnedApps[static_cast<size_t>(pi)];
          app.appMenuPinCtxPinnedDock = dock_is_app_pinned(app, pk);
          app.appMenuPinCtxHoverItem = -1;
          app.appMenuPinCtxOpen = true;
          popup_draw_surface(app);
          wl_display_flush(app.display);
          return true;
        }
      }
      return true;
    }
    if (app.appMenuPinCtxOpen && left) {
      double mx = 0, my = 0, mw = 0, mh = 0;
      app_drawer_row_context_menu_layout(
          app.appMenuPinCtxMenuX, app.appMenuPinCtxMenuY, static_cast<double>(app.popupW),
          static_cast<double>(app.popupH), 2, &mx, &my, &mw, &mh);
      const int pick = app_drawer_row_context_menu_pick(app.pointerX, app.pointerY, mx, my, 2);
      if (pick >= 0 && app.appMenuPinCtxAnchorIdx >= 0 &&
          app.appMenuPinCtxAnchorIdx < static_cast<int>(std::min<size_t>(15, app.settings.drawerPinnedApps.size()))) {
        const std::string& key = app.settings.drawerPinnedApps[static_cast<size_t>(app.appMenuPinCtxAnchorIdx)];
        if (pick == 0)
          dock_pinned_toggle(app, key);
        else
          dock_drawer_pinned_toggle(app, key);
        app.appMenuPinCtxOpen = false;
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      app.appMenuPinCtxOpen = false;
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
    if (app.appMenuRowCtxOpen && left) {
      double mx = 0, my = 0, mw = 0, mh = 0;
      const int n = std::clamp(app.appMenuRowCtxItemCount, 2, kAppDrawerRowContextItemMax);
      app_drawer_row_context_menu_layout(
          app.appMenuRowCtxMenuX, app.appMenuRowCtxMenuY, static_cast<double>(app.popupW),
          static_cast<double>(app.popupH), n, &mx, &my, &mw, &mh);
      const int pick = app_drawer_row_context_menu_pick(app.pointerX, app.pointerY, mx, my, n);
      if (pick >= 0 && app.appMenuRowCtxAnchorRow >= 0 &&
          app.appMenuRowCtxAnchorRow < static_cast<int>(app.appMenuHits.size())) {
        const std::string& pth = app.appMenuHits[static_cast<size_t>(app.appMenuRowCtxAnchorRow)].path;
        if (pick == 0)
          dock_pinned_toggle(app, pth);
        else if (pick == 1)
          dock_start_menu_pinned_toggle(app, pth);
        else
          dock_drawer_pinned_toggle(app, pth);
        app.appMenuRowCtxOpen = false;
        popup_draw_surface(app);
        wl_display_flush(app.display);
        return true;
      }
      app.appMenuRowCtxOpen = false;
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
    if (!left) return true;
    const auto zone = eh_app_drawer_hit_zone(app, app.pointerX, app.pointerY);
    if (eh_app_drawer_debug_level() >= 2) {
      trace_line(2, "dock.ptr",
                 std::string("AppMenu press zone=") + dock_appdrawer_zone_cstr(zone) + " xy=(" +
                     std::to_string(app.pointerX) + "," + std::to_string(app.pointerY) + ")");
    }
    if (zone == AppDrawerHitZone::SearchField) {
      app.appMenuSearchFocused = true;
      if (eh_app_drawer_debug_level() >= 2) {
        trace_line(2, "dock.ptr", "AppMenu -> focus search field");
      }
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (zone == AppDrawerHitZone::CategoryTab) {
      const int cat = eh_app_drawer_pick_category_tab(app, app.pointerX, app.pointerY);
      if (eh_app_drawer_debug_level() >= 2) {
        trace_line(2, "dock.ptr", "AppMenu category tab pick=" + std::to_string(cat));
      }
      app.appMenuSelectedCategory = cat;
      eh_app_drawer_refresh_hits(app);
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (zone == AppDrawerHitZone::AppListRow) {
      app.appMenuSearchFocused = false;
      const int row = eh_app_drawer_pick_row_index(app, app.pointerX, app.pointerY);
      if (eh_app_drawer_debug_level() >= 2) {
        trace_line(2, "dock.ptr", "AppMenu list row pick row=" + std::to_string(row) + " hits=" +
                                      std::to_string(app.appMenuHits.size()));
      }
      if (row >= 0 && row < static_cast<int>(app.appMenuHits.size())) {
        if (eh_app_drawer_debug_level() >= 1) {
          trace_line(1, "dock.ptr", "AppMenu launch row=" + std::to_string(row) + " name=\"" + app.appMenuHits[static_cast<size_t>(row)].name + "\"");
        }
        launch_exec_command(app.appMenuHits[static_cast<size_t>(row)].exec);
        dock_start_launch_bounce(app, app.appMenuHits[static_cast<size_t>(row)].path, true);
        popup_close(app);
        wl_display_flush(app.display);
      }
      return true;
    }
    if (zone == AppDrawerHitZone::PowerButton) {
      app.appMenuSearchFocused = false;
      const int pwr = eh_app_drawer_pick_power_index(app, app.pointerX, app.pointerY);
      if (pwr == 0) {
        app_drawer_power_exec(app.compositorKind, 0);
        popup_close(app);
      } else if (pwr >= 1 && pwr <= 3) {
        popup_close(app);
        popup_open_power_confirm(app, pwr);
      } else {
        if (pwr == 0) {
          app_drawer_power_exec(app.compositorKind, 0);
          popup_close(app);
        } else if (pwr >= 1 && pwr <= 3) {
          popup_close(app);
          popup_open_power_confirm(app, pwr);
        }
      }
      wl_display_flush(app.display);
      return true;
    }
    if (zone == AppDrawerHitZone::NightlightButton) {
      app.appMenuSearchFocused = false;
      if (app.gammaService_) {
        const bool next = !app.gammaService_->enabled();
        app.gammaService_->set_enabled(next);
        if (next) app.gammaService_->set_temperature(4000);
        eh_app_drawer_set_nightlight_active(next);
      } else {
        eh_app_drawer_set_nightlight_active(!eh_app_drawer_get_nightlight_active());
      }
      popup_draw_surface(app);
      wl_display_flush(app.display);
      return true;
    }
    if (eh_app_drawer_debug_level() >= 2) {
      trace_line(2, "dock.ptr", "AppMenu press no-op for this zone");
    }
    return true;
  }
  return false;
}

bool app_drawer_handle_axis(DockApp& app, double deltaPx) {
  if (!app.popupOpen) return false;
  if (!dock_popup_pointer_on_any_popup_surface(app)) return false;
  if (!dock_popup_kind_uses_app_drawer_ui(app.popupKind)) return false;

  if (eh_app_drawer_debug_level() >= 2) {
    trace_line(2, "dock.ptr", "AppMenu axis_vertical deltaPx=" + std::to_string(deltaPx));
  }
  eh_app_drawer_scroll_pixels(app, -deltaPx);
  popup_draw_surface(app);
  wl_display_flush(app.display);
  return true;
}

}
