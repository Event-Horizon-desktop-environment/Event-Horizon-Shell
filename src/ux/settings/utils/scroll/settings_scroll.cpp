#include <algorithm>
#include <cmath>
#include <cstdint>
#include <time.h>

#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "ux/settings/utils/widget_picker/widget_picker.hpp"
#include "ux/settings/settings_tab_desktop_widgets/world_clock_popup.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_dock_appearance/settings_tab_dock_appearance.hpp"
#include "ux/settings/settings_tab_sound/settings_tab_sound.hpp"
#include "ux/settings/settings_tab_monitors/settings_tab_monitors.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "ux/settings/settings_tab_appearance/settings_tab_appearance.hpp"
#include "ux/settings/settings_tab_mango/settings_tab_mango.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/settings_tab_default_apps/settings_tab_default_apps.hpp"
#include "ux/settings/settings_tab_color_themes/settings_tab_color_themes.hpp"
#include "services/audio/pipewire_service.hpp"
#include "services/network/core/network_manager_service.hpp"

extern void draw(App& app);

// Single source of truth.
// The active tab's resting (non-animated) scroll offset. Per-tab ints are the
// persistent store; the controller animates around them.
int settings_scroll_raw_px(const App& app) {
  switch (app.activeTab) {
    case 0: return app.settingsDockScrollPx;
    case 1: return app.settingsPanelScrollPx;
    case 11: return app.settingsTaskbarScrollPx;
    case 6: return app.settingsWallpaperScrollPx;
    case 7: return app.settingsMonitorsScrollPx;
    case 8: return app.settingsSoundScrollPx;
    case 2: return app.settingsAppearanceScrollPx;
    case 44: return app.settingsIconsScrollPx;
    case 45: return app.settingsThemesScrollPx;
    case 17: return app.settingsNetworkScrollPx;
    case 31: return app.settingsKeyboardScrollPx;
    case 32: return app.settingsPowerScrollPx;
    case 47: return app.settingsBluetoothScrollPx;
    case 46: return app.settingsAccountsScrollPx;
    case 48: return app.autostartScrollPx;
    case 50: return app.settingsColorThemesScrollPx;
    case 27: return app.settingsDesktopWidgetsScrollPx;
    case 29: return app.settingsDesktopScrollPx;
    case 30: return app.settingsTimeScrollPx;
    case 9: return app.settingsLauncherScrollPx;
    default:
      if (app.activeTab >= 20 && app.activeTab <= 26) return app.settingsMangoScrollPx;
      if (app.activeTab == 33) return app.settingsHyprlandScrollPx;
      return 0;
  }
}

double settings_scroll_px(const App& app) { return app.settingsScroll.current(); }

int settings_scroll_px_int(const App& app) { return app.settingsScroll.current_px(); }

void settings_scroll_sync_after_clamp(App& app) {
  const double raw = static_cast<double>(settings_scroll_raw_px(app));
  if (!app.settingsScroll.animating()) {
    app.settingsScroll.snap_to(raw);
  } else if (app.settingsScroll.target() > raw + 0.5) {
    // Content shrank mid-animation (window resize): pull the target back so
    // the animation can never rest out of range.
    app.settingsScroll.scroll_to(raw);
  }
}

namespace {
void scroll_draw(App& app) {
  const int rawPx = settings_scroll_raw_px(app);
  app.settingsScroll.set_max(std::max(app.settingsScroll.max(), rawPx));
  app.settingsScroll.scroll_to(rawPx);
  app.settingsScrollNeedsRedraw = true;

  // Draw if no frame callback is pending, so the surface gets a buffer and
  // the frame-callback chain starts (empty commits don't reliably deliver
  // frame callbacks on all wspace).
  if (!app.surfaceFrameCb) {
    if (!app.surface) return;
    draw(app);
  }
}
}

#include "settings/settings_tab_monitors/settings_monitors_ui.inl"

static int settings_scroll_max_px_inner(const App& app, int tabIdx) {
    
  if (tabIdx != 0 && tabIdx != 1 && tabIdx != 11) return 0;
  const int viewBottom = app.height - kSpacingL;

  // Tab 1 (panel): widgets are last, use old widget-only extent.
  if (tabIdx == 1) {
    const int anchor = widget_section_y0_for_tab(app, 2, tabIdx);
    const auto* rr = widgets_for_section_for_tab(app, 2, tabIdx);
    const int extent = anchor + widget_section_block_height(static_cast<int>(rr->size())) + kSpacingXL;
    return std::max(0, extent - viewBottom);
  }

  // Tabs 0 (dock) and 11 (taskbar): widgets first, then toggles, then sliders.
  // Compute total content height including all three cards.
  constexpr int kDockVisCardH = 52 + kDockVisRowPitch * kDockVisToggleRows + 24;
  constexpr int kTbVisCardH = 52 + kDockVisRowPitch * 8 + 24;
  constexpr int kTbAppearCardH = kSpacingXL + kSpacingS +
      2 * kSliderRowH + kSpacingM +
      3 * kSliderRowH + kSpacingM +
      2 * kSliderRowH + kSpacingM +
      kBorderToggleBandH + kSpacingM +
      1 * kSliderRowH + kSpacingM +
      2 * kSliderRowH +
      kSpacingXL;

  if (tabIdx == 0) {
    const int chDock = static_cast<int>(settings_dock_widgets_card_fill_height_px(app));
    const bool widgetsChild = dock_m3_is_widgets_child_tab();
    const bool settingsChild = !widgetsChild && !dock_m3_is_appearance_child_tab();
    const int topCardH = widgetsChild ? kDockWidgetToggleCardH : kDockVisCardH;
    int totalH = chDock + kCardGap + topCardH;
    // Settings child tab: renderer picker card below the visibility card.
    if (settingsChild) totalH += kCardGap + (52 + kDockVisRowPitch + 24);
    int extent = kContentTop + totalH + kSpacingXL;
    // Appearance child tab: two-column slider grid. Include its height so the
    // last slider rows stay reachable by scrolling.
    if (!dock_m3_is_widgets_child_tab()) {
      extent = std::max(extent, kContentTop + dock_appearance_tab_scroll_max_px() + kSpacingXL);
    }
    return std::max(0, extent - viewBottom);
  }

  // tabIdx == 11
  const auto* rhs = widgets_for_section_for_tab(app, 2, 11);
  const int tbStackBottom = widget_section_y0_for_tab(app, 2, 11) +
                            widget_section_block_height(static_cast<int>(rhs->size()));
  const int tbWidgetCardH = std::max(140, std::max(
      tbStackBottom + kSpacingXL - kContentTop,
      app.height - kContentTop - kSpacingXL));
  const int totalH = tbWidgetCardH + kCardGap + kTbVisCardH + kCardGap + kTbAppearCardH;
  const int extent = kContentTop + totalH + kSpacingXL;
  return std::max(0, extent - viewBottom);
}

void settings_clamp_dock_scroll_px(App& app) {
  const int mx = settings_scroll_max_px_inner(app, 0);
  app.settingsDockScrollPx = std::clamp(app.settingsDockScrollPx, 0, mx);
}

void settings_clamp_panel_scroll_px(App& app) {
  const int mx = settings_scroll_max_px_inner(app, 1);
  app.settingsPanelScrollPx = std::clamp(app.settingsPanelScrollPx, 0, mx);
}

void settings_clamp_taskbar_scroll_px(App& app) {
  const int mx = settings_scroll_max_px_inner(app, 11);
  app.settingsTaskbarScrollPx = std::clamp(app.settingsTaskbarScrollPx, 0, mx);
}

void settings_clamp_autostart_scroll_px(App& app) {
  const int viewH = app.height - kContentTop - kSpacingL;
  const auto& entries = app.autostartEntries;
  const int entryH = static_cast<int>(entries.size()) * 60;
  const int formH = app.autostartFormOpen ? (36 * 2 + 36 + 10 * 4 + 40) : 0;
  const int cardH = std::max(180, 80 + entryH + formH + 16 + 40);
  const int maxScroll = std::max(0, cardH - viewH);
  app.autostartScrollPx = std::clamp(app.autostartScrollPx, 0, maxScroll);
}

void settings_apply_wheel_scroll_delta(App& app, double delta_px) {
  if (world_clock_popup_visible(app)) {
    world_clock_popup_consume_scroll(app, delta_px);
    return;
  }
  if (app.widgetPickerOpen) {
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    const std::vector<int> vis = eh::settings::widget_picker::visible_indices(app);
    const eh::settings::widget_picker::WidgetPickerLayout pk =
        eh::settings::widget_picker::layout_for(app, static_cast<int>(vis.size()), app.width, app.height,
                                                kSpacingL + kSidebarW + kSpacingL);
    if (app.pointerX >= pk.gridX && app.pointerX < pk.gridX + pk.gridW &&
        app.pointerY >= pk.gridY && app.pointerY < pk.gridY + pk.gridClipH) {
      app.widgetPickerScrollPx = std::clamp(app.widgetPickerScrollPx - step, 0, pk.gridScrollMax);
      scroll_draw(app);
      return;
    }
    if (app.pointerX >= pk.px && app.pointerX < pk.px + pk.pw &&
        app.pointerY >= pk.py && app.pointerY < pk.py + pk.ph) {
      scroll_draw(app);
      return;
    }
  }
  {
    const int sidebarX = kSpacingL;
    if (app.pointerX >= sidebarX && app.pointerX < sidebarX + kSidebarW &&
        app.pointerY >= kContentTop && app.pointerY < app.height - kSpacingL) {
      const int step = static_cast<int>(std::lround(delta_px));
      if (step == 0) return;
      app.sidebarScrollPx -= step;
      settings_clamp_sidebar_scroll_px(app);
      scroll_draw(app);
      return;
    }
  }
  if (app.defaultAppPickerOpen && app.activeTab == 10) {
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    int lx = 0;
    int ly = 0;
    int lw = 0;
    int vh = 0;
    int rh = 0;
    int nRows = 0;
    int maxScroll = 0;
    if (!default_app_picker_popup_geom(app, tcx, tcw, &lx, &ly, &lw, &vh, &rh, &nRows, &maxScroll)) return;
    if (!point_in_rect(app.pointerX, app.pointerY, lx, ly, lw, vh)) return;
    app.defaultAppPickerScrollPx -= step;
    app.defaultAppPickerScrollPx = std::clamp(app.defaultAppPickerScrollPx, 0, maxScroll);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 8) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    eh::audio::PipeWireService::instance().start();
    const eh::audio::Snapshot ss = eh::audio::PipeWireService::instance().snapshot();
    app.settingsSoundScrollPx -= step;
    settings_clamp_sound_scroll_px(app, static_cast<int>(ss.output_streams.size()),
                                   static_cast<int>(ss.input_streams.size()), sound_tab_bt_count_clamped(app));
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 7) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const double pyLogical = app.pointerY + settings_scroll_px(app);
    eh::settings_monitors_tab::MonitorsTabLayout monLay{};
    eh::settings_monitors_tab::compute_monitors_tab_layout(tcx, tcw, kContentTop, settings_content_viewport_h(app),
                                                           &monLay);
    if (point_in_rect(app.pointerX, pyLogical, monLay.canvas_x, monLay.canvas_y,
                      monLay.canvas_w, monLay.canvas_h)) {
      monitors_wheel_zoom_canvas(app, monLay, delta_px);
      scroll_draw(app);
      return;
    }
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsMonitorsScrollPx -= step;
    settings_clamp_monitors_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 6) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsWallpaperScrollPx -= step;
    settings_clamp_wallpaper_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 2) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsAppearanceScrollPx -= step;
    settings_clamp_appearance_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 17) {
    eh::net::NetworkManagerService::instance().start();
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsNetworkScrollPx -= step;
    app.settingsNetworkScrollPx = std::max(0, app.settingsNetworkScrollPx);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab >= 20 && app.activeTab <= 26) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsMangoScrollPx -= step;
    settings_clamp_mango_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 33) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    if (hyprland_handle_tab_bar_scroll(app, app.pointerX, app.pointerY, delta_px, tcx, tcw)) {
      scroll_draw(app);
      return;
    }
    const int colH = app.height - kHyprlandContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kHyprlandContentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= kHyprlandContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsHyprlandScrollPx -= step;
    settings_clamp_hyprland_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 9) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int contentTop = kContentTop + kDockChildTabH + 12;
    const int colH = app.height - contentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < contentTop || app.pointerX >= tcx + tcw ||
        app.pointerY >= contentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsLauncherScrollPx -= step;
    settings_clamp_launcher_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 27) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsDesktopWidgetsScrollPx = std::max(0, app.settingsDesktopWidgetsScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 29) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsDesktopScrollPx = std::max(0, app.settingsDesktopScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 30) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsTimeScrollPx = std::max(0, app.settingsTimeScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 31) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsKeyboardScrollPx = std::max(0, app.settingsKeyboardScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 32) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsPowerScrollPx = std::max(0, app.settingsPowerScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 46) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsAccountsScrollPx = std::max(0, app.settingsAccountsScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 44) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsIconsScrollPx = std::max(0, app.settingsIconsScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 45) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsThemesScrollPx = std::max(0, app.settingsThemesScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 47) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsBluetoothScrollPx = std::max(0, app.settingsBluetoothScrollPx - step);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.autostartAppBrowserOpen && app.activeTab == 48) {
    const int bw = 420, bh = 360;
    const int bx = (app.width - bw) / 2;
    const int by = std::max(30, (app.height - bh) / 2);
    const int lt = by + 48;
    const int lh = bh - 58;
    if (point_in_rect(app.pointerX, app.pointerY, bx + 8, lt, bw - 16, lh)) {
      const int step = static_cast<int>(std::lround(delta_px));
      if (step == 0) return;
      const int kBrowserEntryH = 40;
      const int total = static_cast<int>(app.autostartInstalledApps.size());
      const int maxVis = std::max(1, lh / kBrowserEntryH);
      const int vis = std::min(total, maxVis);
      const int maxScroll = std::max(0, (total - vis) * kBrowserEntryH);
      app.autostartAppBrowserScrollPx = std::clamp(app.autostartAppBrowserScrollPx - step, 0, maxScroll);
      scroll_draw(app);
      return;
    }
  }
  if (app.activeTab == 48) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.autostartScrollPx = std::max(0, app.autostartScrollPx - step);
    settings_clamp_autostart_scroll_px(app);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (app.activeTab == 50) {
    int tcx = 0, tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int colH = app.height - kContentTop - kSpacingL;
    if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw || app.pointerY >= kContentTop + colH)
      return;
    const int step = static_cast<int>(std::lround(delta_px));
    if (step == 0) return;
    app.settingsColorThemesScrollPx = std::max(0, app.settingsColorThemesScrollPx - step);
    settings_clamp_color_themes_scroll(app, tcw);
    settings_scroll_sync_after_clamp(app);
    scroll_draw(app);
    return;
  }
  if (!(app.activeTab == 0 || app.activeTab == 1 || app.activeTab == 11)) return;
  int tcx = 0, tcw = 0;
  settings_content_column_geom(app, &tcx, &tcw);
  const int colH = app.height - kContentTop - kSpacingL;
  if (app.pointerX < tcx || app.pointerY < kContentTop || app.pointerX >= tcx + tcw ||
      app.pointerY >= kContentTop + colH)
    return;
  const int step = static_cast<int>(std::lround(delta_px));
  if (step == 0) return;

  if (app.activeTab == 0) {
    app.settingsDockScrollPx -= step;
    settings_clamp_dock_scroll_px(app);
  } else if (app.activeTab == 11) {
    app.settingsTaskbarScrollPx -= step;
    settings_clamp_taskbar_scroll_px(app);
  } else {
    app.settingsPanelScrollPx -= step;
    settings_clamp_panel_scroll_px(app);
  }
  settings_scroll_sync_after_clamp(app);
  scroll_draw(app);
}
