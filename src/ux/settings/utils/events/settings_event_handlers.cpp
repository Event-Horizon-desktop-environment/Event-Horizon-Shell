#include <cairo/cairo.h>
#include <wayland-client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "services/audio/pipewire_service.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"

#include "desktop_shell/common/log/debug_log.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/settings_tab_network/dialogs/vpn_add_dialog.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/sound/settings_sound_cache.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/widget_picker.hpp"
#include "ux/settings/settings_tab_desktop_widgets/world_clock_popup.hpp"
#include "ux/settings/utils/monitors/settings_monitors_tab.hpp"

#include "ux/settings/settings_tab_power/settings_tab_power.hpp"
#include "ux/settings/settings_tab_notifications/settings_tab_notifications.hpp"
#include "ux/settings/settings_tab_appearance/settings_tab_appearance.hpp"
#include "ux/settings/settings_tab_icons/settings_tab_icons.hpp"
#include "ux/settings/settings_tab_themes/settings_tab_themes.hpp"
#include "ux/settings/settings_tab_color_themes/settings_tab_color_themes.hpp"
#include "ux/settings/settings_tab_workspaces/settings_tab_workspaces.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/settings_tab_monitors/settings_tab_monitors.hpp"
#include "ux/settings/settings_tab_sound/settings_tab_sound.hpp"
#include "ux/settings/settings_tab_default_apps/settings_tab_default_apps.hpp"
#include "ux/settings/data/default_apps/settings_default_apps.hpp"
#include "ux/settings/settings_tab_bing/settings_tab_bing.hpp"
#include "ux/settings/settings_tab_network/settings_tab_network.hpp"
#include "ux/settings/settings_tab_network/dialogs/wifi_password_prompt.hpp"
#include "ux/settings/settings_tab_network/dialogs/keyring_password_prompt.hpp"
#include "ux/settings/settings_tab_nightlight/settings_tab_nightlight.hpp"
#include "ux/settings/settings_tab_desktop_widgets/settings_tab_desktop_widgets.hpp"
#include "ux/settings/settings_tab_desktop/settings_tab_desktop.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widgets_preferences.hpp"
#include "ux/settings/settings_tab_time/settings_tab_time.hpp"
#include "ux/settings/settings_tab_keyboard/settings_tab_keyboard.hpp"
#include "ux/settings/settings_tab_power/settings_tab_power.hpp"
#include "ux/settings/settings_tab_bluetooth/settings_tab_bluetooth.hpp"
#include "ux/settings/settings_tab_accounts/settings_tab_accounts.hpp"
#include "ux/settings/settings_tab_autostart/settings_tab_autostart.hpp"
#include "ux/settings/settings_tab_mango/settings_tab_mango.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_dock_appearance/settings_tab_dock_appearance.hpp"
#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"
#include "ux/settings/settings_tab_ui_layout/settings_tab_layout.hpp"
#include "ux/settings/settings_serialize.hpp"

extern void draw(App& app);

static constexpr int kHeaderH = 56;

namespace {
static constexpr int kWallpaperFolderPickerRowCount = 4;

static constexpr int kSidebarSubTabIndentX = 28;

// Sidebar definitions.
struct SidebarItemDef {
  int id = -1;
  const char* label;
  const char* glyph;
  const char* const* subLabels;
  const char* const* subGlyphs;
  int subCount;
  bool isCategory = false;
  const int* subTabIds = nullptr;
};

static const char* const kPanelsAndUISubs[] = {"Dock", "Taskbar", "Launcher", "Desktop", "Desktop Widgets"};
static const char* const kPanelsAndUISubGlyphs[] = {"dock_to_bottom", "dock_to_bottom", "apps", "desktop_windows", "widgets"};
static const char* const kDisplaySubs[] = {"Monitors", "Appearance", "Themes", "Color Themes", "Icons", "UI Layout", "Nightlight"};
static const char* const kDisplaySubGlyphs[] = {"monitor", "palette", "palette", "colorize", "photo_library", "view_quilt", "dark_mode"};

static const int kPanelsAndUITabIds[] = {0, 11, 9, 29, 27};
static const int kDisplayTabIds[] = {7, 2, 45, 50, 44, 3, 19};
static const char* const kSystemSubs[] = {"Notifications", "Sound", "Default Apps", "Time", "Keyboard & Language", "Bluetooth", "Power", "Accounts", "Startup"};
static const char* const kSystemSubGlyphs[] = {"notifications", "volume_up", "app_registration", "schedule", "keyboard", "bluetooth", "power_settings_new", "account_circle", "play_arrow"};
static const int kSystemTabIds[] = {5, 8, 10, 30, 31, 47, 32, 46, 48};
static const char* const kWallpaperSettingsSubs[] = {"Wallpaper", "Bing Wallpaper"};
static const char* const kWallpaperSettingsSubGlyphs[] = {"wallpaper", "photo_library"};
static const int kWallpaperSettingsTabIds[] = {6, 16};

static const char* const kNetworkSubs[] = {"Wi-Fi", "Wired", "VPN"};

static const char* const kNetworkSubGlyphs[] = {"wifi", "lan", "vpn_key"};
static const int kNetworkTabIds[] = {17, 18, 28};

static const char* const kMangoSubs[] = {"Decoration", "Colors", "Animations", "Keybinds", "Layout", "Input", "Misc"};
static const char* const kMangoSubGlyphs[] = {"border_style", "palette", "play_arrow", "keyboard", "grid_view", "mouse", "tune"};
static const int kMangoTabIds[] = {20, 21, 22, 23, 24, 25, 26};



static const SidebarItemDef kSidebarDefs[] = {
    {12, "Panels & UI", "dashboard", kPanelsAndUISubs, kPanelsAndUISubGlyphs, 5, true, kPanelsAndUITabIds},
    {13, "Display", "display_settings", kDisplaySubs, kDisplaySubGlyphs, 7, true, kDisplayTabIds},
    {16, "MangoWM", "view_module", kMangoSubs, kMangoSubGlyphs, 7, true, kMangoTabIds},
    {33, "Hyprland", "view_module", nullptr, nullptr, 0, false, nullptr},
    {14, "System", "tune", kSystemSubs, kSystemSubGlyphs, 9, true, kSystemTabIds},
    {15, "Wallpaper Settings", "wallpaper", kWallpaperSettingsSubs, kWallpaperSettingsSubGlyphs, 2, true, kWallpaperSettingsTabIds},
    {17, "Network", "wifi", kNetworkSubs, kNetworkSubGlyphs, 3, true, kNetworkTabIds},
};
static constexpr int kSidebarDefCount = sizeof(kSidebarDefs) / sizeof(kSidebarDefs[0]);

// Slider / apply helpers.

void workspace_slider_row_geom(int contentX, int contentW, int row, int& trX, int& trY, int& trW) {
  int cardX, cardY, cardW;
  workspace_app_geom(contentX, contentW, row, trX, trY, trW, cardX, cardY, cardW);
}


bool apply_workspace_slider_x(App& app, int row, double px) {
  int trX, trY, trW;
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  workspace_slider_row_geom(contentX, contentW, row, trX, trY, trW);
  (void)trY;
  if (row == 0) {
    const int v = slider_value_from_x(px, trX, trW, 0, 16);
    if (v == app.settings.workspacesMaxSlots) return false;
    app.settings.workspacesMaxSlots = v;
    return true;
  }
  if (row == 1) {
    const int v = slider_value_from_x(px, trX, trW, 1, 8);
    if (v == app.settings.workspacesMaxIcons) return false;
    app.settings.workspacesMaxIcons = v;
    return true;
  }
  return false;
}

void launcher_slider_row_geom(int contentX, int contentW, int row, int& trX, int& trY, int& trW) {
  int cardX, cardY, cardW;
  launcher_app_geom(contentX, contentW, row, trX, trY, trW, cardX, cardY, cardW);
}

bool apply_launcher_slider_x(App& app, int row, double px) {
  int trX, trY, trW;
  const int contentX = 16 + 240 + 16;
  const int contentW = app.width - contentX - 16;
  launcher_slider_row_geom(contentX, contentW, row, trX, trY, trW);
  (void)trY;
  if (row == 0) {
    const int v = slider_value_from_x(px, trX, trW, 70, 150);
    if (v == app.settings.launchpadLayoutScalePct) return false;
    app.settings.launchpadLayoutScalePct = v;
    return true;
  }
  if (row == 1) {
    const int v = slider_value_from_x(px, trX, trW, 30, 95);
    if (v == app.settings.launchpadIconFillPct) return false;
    app.settings.launchpadIconFillPct = v;
    return true;
  }
  if (row == 2) {
    const int v = slider_value_from_x(px, trX, trW, 0, 24);
    if (v == app.settings.launchpadCellGapPx) return false;
    app.settings.launchpadCellGapPx = v;
    return true;
  }
  if (row == 3) {
    const int v = slider_value_from_x(px, trX, trW, 50, 200);
    if (v == app.settings.launchpadFolderSizePct) return false;
    app.settings.launchpadFolderSizePct = v;
    return true;
  }
  if (row == 4) {
    const int v = slider_value_from_x(px, trX, trW, 4, 48);
    if (v == app.settings.launchpadFolderGapPx) return false;
    app.settings.launchpadFolderGapPx = v;
    return true;
  }
  if (row == 5) {
    const int v = slider_value_from_x(px, trX, trW, 4, 12);
    if (v == app.settings.launchpadGridColumns) return false;
    app.settings.launchpadGridColumns = v;
    return true;
  }
  if (row == 6) {
    const int v = slider_value_from_x(px, trX, trW, 3, 10);
    if (v == app.settings.launchpadGridRows) return false;
    app.settings.launchpadGridRows = v;
    return true;
  }
  if (row == 7) {
    const int v = slider_value_from_x(px, trX, trW, 0, 100);
    if (v == app.settings.slotPillOpacity && v == app.settings.taskbarSlotPillOpacity) return false;
    app.settings.slotPillOpacity = v;
    app.settings.taskbarSlotPillOpacity = v;
    return true;
  }
  if (row == 8) {
    const int v = slider_value_from_x(px, trX, trW, 50, 300);
    if (v == app.settings.launchpadDpiScalePct) return false;
    app.settings.launchpadDpiScalePct = v;
    return true;
  }
  return false;
}
} // namespace

// Monitors UI inline helpers.
#include "settings/settings_tab_monitors/settings_monitors_ui.inl"

namespace {
// Shared geometry helpers.
void wallpaper_folder_picker_modal_geom(const App& app, int* outPx, int* outPy, int* outPw, int* outPh,
                                                 int* outRowPitch, int* outN) {
  *outN = kWallpaperFolderPickerRowCount;
  *outRowPitch = 34;
  *outPw = 540;
  *outPh = 56 + *outN * *outRowPitch + 18;
  *outPx = std::max(20, (app.width - *outPw) / 2);
  *outPy = std::max(20, (app.height - *outPh) / 2);
}

// Slider drag helpers.
bool settings_slider_drag_pending(const App& app) noexcept {
  return app.sliderDrag >= 0 || app.wsSliderDrag >= 0 || app.launcherSliderDrag >= 0 ||
         app.mangoSliderDrag >= 0 ||
          app.notifSliderDrag >= 0 ||
         app.draggingPanelTopGap || app.draggingPanelHeight || app.draggingPanelRadius || app.draggingPanelOpacity ||
           app.appearanceOverlaySliderDrag >= 0 || app.appearanceColorSliderDrag >= 0 || app.soundVolDragCode >= 0 ||
           app.keyboardSliderDrag >= 0 || app.themesSliderDrag >= 0;
}

void settings_drag_preview_patch_sc(eh::config::ShellConfig& sc, void* user) {
  auto* ap = static_cast<App*>(user);
  patch_workspaces_widget_in_shell_config(sc, ap->settings);
  patch_widget_slot_enabled_into_shell_config(sc, ap->settings);
}

void settings_publish_embedded_drag_preview(App& app) {
  if (!app.embedded) return;
  eh::config::ShellConfig ui = settings_to_shell_config(app.settings);
  eh::config::shell_config_set_settings_drag_preview(ui, settings_drag_preview_patch_sc, &app);
}

void settings_publish_embedded_drag_preview_throttled(App& app, bool force) {
  if (!app.embedded) return;
  if (!force) {
    constexpr std::uint64_t kMinIntervalMs = 16;
    const std::uint64_t now = eh::shell::now_mono_ms();
    if (app.settingsDragPreviewThrottleLastMs != 0 && now - app.settingsDragPreviewThrottleLastMs < kMinIntervalMs)
      return;
    app.settingsDragPreviewThrottleLastMs = now;
  } else {
    app.settingsDragPreviewThrottleLastMs = 0;
  }
  settings_publish_embedded_drag_preview(app);
}

void settings_slider_motion_commit(App& app) {
  static bool s_draw_in_flight = false;
  static auto lastDraw = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const bool isDrag = settings_slider_drag_pending(app);
  if (isDrag) {
    if (s_draw_in_flight) return;
    const double elapsed = std::chrono::duration<double, std::milli>(now - lastDraw).count();
    if (elapsed < 4.0) return;
    lastDraw = now;
  }

  s_draw_in_flight = true;

  const auto t0 = now;
  const auto tPrev0 = std::chrono::steady_clock::now();
  settings_publish_embedded_drag_preview_throttled(app, false);
  const double tPrev = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tPrev0).count();
  const auto tDraw0 = std::chrono::steady_clock::now();
  draw(app);
  const double tDrawMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tDraw0).count();

  s_draw_in_flight = false;

  if (isDrag) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    static uint64_t lastLog = 0;
    const uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (nowMs - lastLog > 200) {
      lastLog = nowMs;
      std::cerr << "[slider-bench] commit=" << ms << "ms  preview=" << tPrev << "ms  draw=" << tDrawMs << "ms\n";
    }
  }
}

void settings_sound_vol_pw_apply_throttled(App& app, eh::audio::PipeWireService& pw, double norm_t) {
  if (app.soundVolDragPwNodeId == 0) return;
  constexpr std::uint64_t kMinIntervalMs = 16;
  const std::uint64_t now = eh::shell::now_mono_ms();
  if (app.soundVolPwLastApplyMonoMs != 0 && now - app.soundVolPwLastApplyMonoMs < kMinIntervalMs) return;
  pw.set_node_volume(app.soundVolDragPwNodeId, norm_t);
  app.soundVolPwLastApplyMonoMs = now;
}

void settings_sound_vol_pw_drag_commit_final(App& app) {
  if (app.soundVolDragPwNodeId == 0) {
    app.soundVolPwLastApplyMonoMs = 0;
    return;
  }
  const double t = app.settingsSliderDragNormT;
  if (t >= 0.0 && t <= 1.0) eh::audio::PipeWireService::instance().set_node_volume(app.soundVolDragPwNodeId, t);
  app.soundVolDragPwNodeId = 0;
  app.soundVolPwLastApplyMonoMs = 0;
}
} // namespace

// Dropdown helpers.
void settings_close_non_default_app_dropdowns(App& app) {
  app.wallpaperModeDropdownOpen = false;
  app.matugenSchemeDd.close();
  app.matugenModeDd.close();
  app.launcherViewModeDropdownOpen = false;
  app.wallpaperModeDropdownHoverRow = -1;
  app.launcherViewModeDropdownHoverRow = -1;
  app.qtColorSchemeDropdownOpen = false;
  app.qtColorSchemeDropdownHoverRow = -1;
  app.monitorsActiveDd = -1;
  app.monitorsDdHoverRow = -1;
  app.monitorsHyprExtraSlider = -1;
  app.soundActiveDd = -1;
  app.soundDdHoverRow = -1;
  app.hyprlandLayoutDropdownOpen = false;
  app.hyprlandLayoutDropdownHoverRow = -1;
  app.hyprlandAnimEditIdx = -1;
  app.hyprlandAnimCurveDdOpen = false;
  app.hyprlandAnimCurveDdHover = -1;
  app.hyprlandAnimStyleDdOpen = false;
  app.hyprlandAnimStyleDdHover = -1;
  app.hyprlandAnimSpeedEditActive = false;
  app.hyprlandBezierOpen = false;
  app.hyprlandBezierDrag = -1;
  app.hyprlandBezierP1x = 0.25;
  app.hyprlandBezierP1y = 0.1;
  app.hyprlandBezierP2x = 0.25;
  app.hyprlandBezierP2y = 1.0;

}

void settings_close_mode_dropdowns(App& app) {
  settings_close_non_default_app_dropdowns(app);
  app.defaultAppPickerOpen = false;
  app.defaultAppPickerPopupHoverIdx = -1;
  default_app_picker_teardown_layer(app);
  app.wallpaperFolderPickerOpen = false;
  app.timeFormatDropdownOpen = false;
  app.timeFormatDropdownHoverRow = -1;
  app.taskbarWidthModeDropdownOpen = false;
  app.taskbarWidthModeDropdownHoverRow = -1;
}

// Event handlers.
void on_pointer_leave(App& app) {
  app.btnHoverIdx = -1;
  if (app.worldClockPopupOpen &&
      (app.worldClockHoverItem != -1 || app.worldClockDropdownHover != -1)) {
    app.worldClockHoverItem = -1;
    app.worldClockDropdownHover = -1;
    draw(app);
  }
  if (app.activeTab == 0) dock_m3_handle_pointer_leave(app);
  if (app.activeTab == 33) hyprland_m3_handle_pointer_leave(app.hyprlandChildTab);
  if (app.activeTab == 9) launcher_m3_handle_pointer_leave();
  if (app.activeTab == 18) settings_wired_handle_pointer_leave(app);
  if (app.activeTab == 48) app.autostartHoverRow = -1;
  if (app.activeTab == 50) app.colorThemesHoverPill = -1;
  if (app.pointerLeftDown && settings_slider_drag_pending(app)) {
    save_settings(app.settings);
    if (app.embedded) settings_publish_embedded_drag_preview_throttled(app, true);
  }
  if (app.soundVolDragCode >= 0) settings_sound_vol_pw_drag_commit_final(app);
  app.pointerLeftDown = false;
  app.sliderDrag = -1;
  app.wsSliderDrag = -1;
  app.launcherSliderDrag = -1;
  app.notifSliderDrag = -1;
  app.monitorsScaleSliderDragIdx = -1;
  app.monitorsHyprExtraSlider = -1;
  app.desktopWidgetSliderDrag = -1;
  app.monitorsCanvasDragIdx = -1;
  app.monitorsCanvasPanArmed = false;
  app.appearanceOverlaySliderDrag = -1;
  app.soundVolDragCode = -1;
  app.soundPaintSnapPending.reset();
  app.settingsSliderDragNormT = -1.0;
  app.settings_paint_shell_snapshot_valid = false;
  app.keyboardSliderDrag = -1;
  app.timeSliderDrag = -1;
  app.accountsActiveField = AccountsField::None;
  settings_close_mode_dropdowns(app);
  app.testNotifBtn.handlePointerLeave();
  if (app.widgetDragArmed || app.widgetDragging) {
    app.settingsWidgetDragRepaintQueued = false;
    app.widgetDragArmed = false;
    app.widgetDragging = false;
    app.widgetDragSection = App::kSectionNone;
    app.widgetDragTargetSection = App::kSectionNone;
    app.widgetDragFromIndex = -1;
    draw(app);
  }
}

void on_pointer_motion(App& app, wl_surface* ptrSurf, double x, double y) {
  app.pointerX = x;
  app.pointerY = y;

  // Title bar button hover tracking.
  if (app.embedded) {
    constexpr int kBtnSize = 20;
    constexpr int kBtnGap  = 8;
    const int btnY = (kHeaderH - kBtnSize) / 2;
    int bx = app.width - kSpacingL;
    bx -= kBtnSize;
    if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
      app.btnHoverIdx = 2; // close (rightmost)
    } else {
      bx -= kBtnGap;
      bx -= kBtnSize;
      if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
        app.btnHoverIdx = 1; // maximize (middle)
      } else {
        bx -= kBtnGap;
        bx -= kBtnSize;
        if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
          app.btnHoverIdx = 0; // minimize (leftmost)
        } else {
          app.btnHoverIdx = -1;
        }
      }
    }
  }

  if (keyring_prompt_visible(app)) {
    keyring_prompt_consume_pointer_motion(app);
    return;
  }

  if (wifi_password_prompt_visible(app)) {
    wifi_password_prompt_consume_pointer_motion(app);
    return;
  }

  if (vpn_add_dialog_visible(app)) {
    vpn_add_dialog_consume_pointer_motion(app);
    return;
  }

  if (world_clock_popup_visible(app)) {
    world_clock_popup_consume_pointer_motion(app);
    return;
  }

  if (app.activeTab == 11 && taskbar_m3_has_active_slider(app)) {
    const double ly = app.pointerY + settings_scroll_px(app);
    taskbar_m3_handle_pointer_move(app, static_cast<float>(app.pointerX), static_cast<float>(ly));
    settings_slider_motion_commit(app);
    return;
  }

  if (app.activeTab == 0 && dock_m3_has_active_slider(app)) {
    const int contentXeh = 16 + 240 + 16;
    const int contentWeh = app.width - contentXeh - 16;
    const double ly = app.pointerY + settings_scroll_px(app);
    // Check if the appearance child tab has an active slider (tracked via app.sliderDrag)
    if (app.sliderDrag >= 200 && app.sliderDrag < 212) {
      dock_appearance_consume_pointer_move(app, contentXeh, contentWeh);
    } else {
      dock_m3_handle_pointer_move(app, static_cast<float>(app.pointerX), static_cast<float>(ly));
    }
    settings_slider_motion_commit(app);
    return;
  }

  if (app.activeTab == 33 &&
      hyprland_m3_has_active_slider(app, app.hyprlandChildTab)) {
    const int subTab = app.hyprlandChildTab;
    const double ly = app.pointerY + settings_scroll_px(app);
    hyprland_m3_handle_pointer_move(app, static_cast<float>(app.pointerX), static_cast<float>(ly), subTab);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.activeTab == 9 && launcher_m3_has_active_slider(app)) {
    const double ly = app.pointerY + settings_scroll_px(app);
    launcher_m3_handle_pointer_move(app, static_cast<float>(app.pointerX), static_cast<float>(ly));
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.wsSliderDrag >= 0) {
    int trX, trY, trW;
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    workspace_slider_row_geom(contentX, contentW, app.wsSliderDrag, trX, trY, trW);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)apply_workspace_slider_x(app, app.wsSliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.launcherSliderDrag >= 0 && app.activeTab == 9) {
    int trX, trY, trW;
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    launcher_slider_row_geom(contentX, contentW, app.launcherSliderDrag, trX, trY, trW);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)apply_launcher_slider_x(app, app.launcherSliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.notifSliderDrag >= 0 && app.activeTab == 5) {
    int trX, trY, trW;
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    int cardX, cardY, cardW;
    notif_app_geom(contentX, contentW, app.notifSliderDrag, trX, trY, trW, cardX, cardY, cardW);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)apply_notifications_slider_x(app, app.notifSliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.keyboardSliderDrag >= 0 && app.activeTab == 31) {
    const int contentXlocal = 16 + 240 + 16;
    const int contentWlocal = app.width - contentXlocal - 16;
    int trX = contentXlocal + kCardPad;
    int trW = contentWlocal - kCardPad - kCardPad - 52;
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    const int minV = app.keyboardSliderDrag == 0 ? 15 : 150;
    const int maxV = app.keyboardSliderDrag == 0 ? 50 : 1000;
    const int nv = slider_value_from_x(app.pointerX, trX, trW, minV, maxV);
    if (app.keyboardSliderDrag == 0) app.settings.keyboardRepeatRate = nv;
    else app.settings.keyboardRepeatDelay = nv;
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.sliderDrag >= 0 && app.activeTab == 32) {
    const int contentXlocal = 16 + 240 + 16;
    const int contentWlocal = app.width - contentXlocal - 16;
    if (app.sliderDrag == 10) {
      int trX = contentXlocal + kCardPad;
      int trW = contentWlocal - kCardPad - kCardPad - 80;
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      app.settings.powerDisplaySleepTimeout = slider_value_from_x(app.pointerX, trX, trW, 1, 120);
      settings_slider_motion_commit(app);
      return;
    }
    if (app.sliderDrag == 11) {
      int trX = contentXlocal + kCardPad;
      int trW = contentWlocal - kCardPad - kCardPad - 80;
      app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
      app.settings.powerIdleSuspendTimeout = slider_value_from_x(app.pointerX, trX, trW, 5, 240);
      settings_slider_motion_commit(app);
      return;
    }
  }

  if (app.pointerLeftDown && app.activeTab == 19 && app.sliderDrag >= 210 && app.sliderDrag < 216) {
    const int contentX = 16 + kSidebarW + 16;
    const int contentW = app.width - contentX - 16;
    nightlight_consume_pointer_move(app, contentX, contentW);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.activeTab == 8 && app.soundVolDragCode >= 0) {
    eh::audio::PipeWireService::instance().start();
    app.soundPaintSnapPending.emplace(eh::audio::PipeWireService::instance().snapshot());
    const eh::audio::Snapshot& snap = *app.soundPaintSnapPending;
    auto& pw = eh::audio::PipeWireService::instance();
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const int n_bt_mv = sound_tab_bt_count_clamped(app);
    const SoundTabGeom sg =
        sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app),
                               static_cast<int>(snap.output_streams.size()),
                               static_cast<int>(snap.input_streams.size()), n_bt_mv);
    if (app.soundVolDragCode >= 2000 && app.soundVolDragCode < 3000) {
      const int dev = app.soundVolDragCode - 2000;
      int trx = 0;
      int trey = 0;
      int trw = 0;
      if (dev == 0 && snap.default_sink != 0) {
        monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 1, &trx, &trey,
                                                &trw);
        const double nt = slider_norm_from_x(app.pointerX, trx, trw);
        app.settingsSliderDragNormT = nt;
        settings_sound_vol_pw_apply_throttled(app, pw, nt);
      } else if (dev == 1 && snap.default_source != 0) {
        monitors_form_slider_track_geom_content(sg.devices.content_y0, sg.devices.x, sg.devices.w, 3, &trx, &trey,
                                                &trw);
        const double nt = slider_norm_from_x(app.pointerX, trx, trw);
        app.settingsSliderDragNormT = nt;
        settings_sound_vol_pw_apply_throttled(app, pw, nt);
      }
    } else if (app.soundVolDragCode < 1000) {
      const int i = app.soundVolDragCode;
      if (i >= 0 && i < static_cast<int>(snap.output_streams.size())) {
        int trx = 0;
        int trey = 0;
        int trw = 0;
        sound_stream_slider_geom_content(sg.playback.content_y0, sg.playback.x, sg.playback.w, i, &trx, &trey, &trw);
        const double nt = slider_norm_from_x(app.pointerX, trx, trw);
        app.settingsSliderDragNormT = nt;
        settings_sound_vol_pw_apply_throttled(app, pw, nt);
      }
    } else {
      const int i = app.soundVolDragCode - 1000;
      if (i >= 0 && i < static_cast<int>(snap.input_streams.size())) {
        int trx = 0;
        int trey = 0;
        int trw = 0;
        sound_stream_slider_geom_content(sg.recording.content_y0, sg.recording.x, sg.recording.w, i, &trx, &trey,
                                         &trw);
        const double nt = slider_norm_from_x(app.pointerX, trx, trw);
        app.settingsSliderDragNormT = nt;
        settings_sound_vol_pw_apply_throttled(app, pw, nt);
      }
    }
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.activeTab == 7) {
    int tcx = 0;
    int tcw = 0;
    settings_content_column_geom(app, &tcx, &tcw);
    const double pyLogical = app.pointerY + settings_scroll_px(app);
    eh::settings_monitors_tab::MonitorsTabLayout monLay{};
    eh::settings_monitors_tab::compute_monitors_tab_layout(tcx, tcw, kContentTop, settings_content_viewport_h(app),
                                                           &monLay);
    if (app.monitorsCanvasDragIdx >= 0) {
      const size_t ix = static_cast<size_t>(app.monitorsCanvasDragIdx);
      if (ix < app.monitorsTab.outputs.size() && !app.monitorsTab.outputs[ix].disabled) {
        double min_x = 0, min_y = 0, sf = 1;
        monitors_canvas_transform(app, monLay, &min_x, &min_y, &sf);
        const double dcx = app.pointerX - app.monitorsCanvasPressLogicalX;
        const double dcy = pyLogical - app.monitorsCanvasPressLogicalY;
        double dmx = 0, dmy = 0;
        eh::settings_monitors_tab::canvas_delta_to_monitor_delta(dcx, dcy, sf, &dmx, &dmy);
        const int nx = app.monitorsCanvasDragAnchorX + static_cast<int>(std::lround(dmx));
        const int ny = app.monitorsCanvasDragAnchorY + static_cast<int>(std::lround(dmy));
        eh::settings_monitors_tab::format_position_xy(nx, ny, &app.monitorsTab.outputs[ix].position);
        app.monitorsTab.dirty = true;
      }
      draw(app);
      return;
    }
    if (app.monitorsCanvasPanArmed) {
      app.monitorsCanvasPanX = app.pointerX - app.monitorsCanvasPanGrabX;
      app.monitorsCanvasPanY = pyLogical - app.monitorsCanvasPanGrabY;
      draw(app);
      return;
    }
    if (app.monitorsScaleSliderDragIdx >= 0) {
      monitors_apply_form_scale_drag(app, app.pointerX, monLay, tcx, tcw);
      draw(app);
      return;
    }
    if (app.monitorsHyprExtraSlider >= 0) {
      monitors_clamp_selected(app);
      const size_t six = static_cast<size_t>(app.monitorsSelectedIdx);
      if (six < app.monitorsTab.outputs.size()) {
        auto& hrow = app.monitorsTab.outputs[six];
        auto hcit = app.monitorsTab.caps.find(hrow.name);
        const eh::settings_monitors::OutputCaps* hcaps =
            hcit != app.monitorsTab.caps.end() ? &hcit->second : nullptr;
        const MonitorsFormRows hfr = monitors_form_rows(app, hrow, hcaps);
        const MonitorsFormGeom hfg = monitors_form_layout(monLay, tcx, tcw, hfr);
        const MonitorsSectionGeom* hsec = nullptr;
        int sliderRow = -1;
        switch (app.monitorsHyprExtraSlider) {
          case 0:
            hsec = &hfg.hdr;
            sliderRow = hfr.h_sdr_b;
            break;
          case 1:
            hsec = &hfg.hdr;
            sliderRow = hfr.h_sdr_s;
            break;
          case 2:
            hsec = &hfg.luminance;
            sliderRow = hfr.l_sdr_min;
            break;
          case 3:
            hsec = &hfg.luminance;
            sliderRow = hfr.l_sdr_max;
            break;
          case 4:
            hsec = &hfg.luminance;
            sliderRow = hfr.l_min;
            break;
          case 5:
            hsec = &hfg.luminance;
            sliderRow = hfr.l_max;
            break;
          case 6:
            hsec = &hfg.luminance;
            sliderRow = hfr.l_avg;
            break;
          default:
            break;
        }
        if (sliderRow >= 0 && hsec && hfg.has_hdr) {
          int htrX = 0, htrY = 0, htrW = 0;
          monitors_form_slider_track_geom_content(hsec->content_y0, hsec->x, hsec->w, sliderRow, &htrX, &htrY, &htrW);
          monitors_apply_hypr_extra_drag(app, &hrow, app.monitorsHyprExtraSlider, app.pointerX, htrX, htrW);
          app.monitorsTab.dirty = true;
        }
      }
      draw(app);
      return;
    }
  }

  if (app.pointerLeftDown && app.appearanceOverlaySliderDrag >= 0 && app.activeTab == 2 && app.appearanceChildTab == kAppearanceGeneral) {
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    int trX, trY, trW, cardX, cardY, cardW;
    const int gridIdx = (app.appearanceOverlaySliderDrag == 0) ? 0 : (app.appearanceOverlaySliderDrag + 1);
    appearance_app_geom(contentX, contentW, gridIdx, trX, trY, trW, cardX, cardY, cardW, kContentTop + kDockChildTabH + 12);
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)apply_appearance_overlay_slider(app, app.appearanceOverlaySliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.appearanceColorSliderDrag >= 0 && app.activeTab == 2 && app.appearanceChildTab == kAppearanceGeneral) {
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    const int colW = (contentW * 68) / 100;
    const int colX = contentX + (contentW - colW) / 2;
    constexpr int kColorGap = 12;
    const int col = app.appearanceColorSliderDrag % 2;
    const int cw = std::max(160, (colW - kColorGap) / 2);
    const int cx = colX + col * (cw + kColorGap);
    const int trX = cx + 20;
    const int trW = cw - 40;
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)apply_appearance_color_slider(app, app.appearanceColorSliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.mangoSliderDrag >= 0 && app.activeTab >= 20 && app.activeTab <= 26) {
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    int trX = contentX + kCardPad;
    int trW = contentW - kCardPad - kCardPad - 52;
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    (void)mango_slider_apply(app, app.mangoSliderDrag, app.pointerX);
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.themesSliderDrag >= 0 && app.activeTab == 45) {
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    int trX = contentX + kCardPad;
    int trW = contentW - kCardPad - kCardPad - 52;
    if (trW < 40) trW = 40;
    app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
    int v = slider_value_from_x(app.pointerX, trX, trW, 16, 64);
    if (v != app.mangoConfig.cursor_size) {
      app.mangoConfig.cursor_size = v;
      settings_slider_motion_commit(app);
    }
    return;
  }



  if (app.pointerLeftDown && app.activeTab == 6 && app.sliderDrag == 220) {
    const int tcx = kSpacingL + kSidebarW + kSpacingL;
    const int tcw = app.width - tcx - kSpacingL;
    const int gsX = tcx + kCardPad;
    const int gsW = std::min(640, tcw - 2 * kCardPad);
    const int sTrackX = gsX + 240;
    const int sTrackW = gsW - 64 - 240;
    const double nt = slider_norm_from_x(app.pointerX, sTrackX, sTrackW);
    app.wallpaperUiOpacityPct = std::clamp(static_cast<int>(nt * 100.0 + 0.5), 10, 100);
    app.settingsSliderDragNormT = nt;
    settings_slider_motion_commit(app);
    return;
  }

  if (app.pointerLeftDown && app.widgetDragArmed && !app.widgetDragging && app.widgetDragSection >= 0) {
    const double dx = app.pointerX - app.widgetDragPressX;
    const double dy = app.pointerY - app.widgetDragPressY;
    if (dx * dx + dy * dy > 36.0) {
      app.widgetDragging = true;
      widget_drag_update_insert(app);
      draw(app);
    }
    return;
  }
  if (app.pointerLeftDown && app.widgetDragging) {
    static auto lastUpdate = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(now - lastUpdate).count();
    if (elapsed < 6.0) return;
    lastUpdate = now;
    const auto t0 = now;
    widget_drag_update_insert(app);
    draw(app);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    static uint64_t lastLog = 0;
    const uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (nowMs - lastLog > 200) { lastLog = nowMs; std::cerr << "[drag-bench] total=" << ms << "ms\n"; }
    return;
  }

  if (!app.pointerLeftDown) {
    bool needDdDraw = false;
    int tcx, tcw;
    settings_content_column_geom(app, &tcx, &tcw);
    if (app.activeTab == 6 && app.wallpaperModeDropdownOpen) {
      int wcx, wcy, wcw, wch;
      const WallpaperVerticalMetrics wm = wallpaper_vertical_metrics(static_cast<double>(tcx), static_cast<double>(tcw));
      wallpaper_mode_combo_geom(tcx, tcw, wm.modeRowY, &wcx, &wcy, &wcw, &wch);
      const int ly = wcy + wch + 2;
      const double pyL = app.pointerY + settings_scroll_px(app);
      const int nr =
          settings_mode_dd_pointer_row(app.pointerX, pyL, wcx, ly, wcw, kSettingsDdRowH, 5);
      if (nr != app.wallpaperModeDropdownHoverRow) {
        app.wallpaperModeDropdownHoverRow = nr;
        needDdDraw = true;
      }
    } else if (app.wallpaperModeDropdownHoverRow != -1) {
      app.wallpaperModeDropdownHoverRow = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 11 && app.taskbarWidthModeDropdownOpen) {
      const int cbx = tcx + 8 + tcw - 16 - kCardPad - kSettingsComboW;
      const int kVisCardTop = kContentTop + kDockChildTabH + 12;
      const int cby = kVisCardTop + 52 + kDockVisRowPitch + (kDockVisRowPitch - kSettingsComboH) / 2;
      const int ly = cby + kSettingsComboH + 2;
      const double pyL = app.pointerY + settings_scroll_px(app);
      const int nr = settings_mode_dd_pointer_row(app.pointerX, pyL, cbx, ly, kSettingsComboW, kSettingsDdRowH, 3);
      if (nr != app.taskbarWidthModeDropdownHoverRow) {
        app.taskbarWidthModeDropdownHoverRow = nr;
        needDdDraw = true;
      }
    } else if (app.taskbarWidthModeDropdownHoverRow != -1) {
      app.taskbarWidthModeDropdownHoverRow = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 0 && app.rendererDd.open()) {
      dock_renderer_dd_sync(app, tcx, tcw);
      const int scr = settings_scroll_px_int(app);
      const int nr = app.rendererDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY),
                                            scr, app.width, app.height);
      if (nr != app.rendererDd.hover_row()) {
        app.rendererDd.set_hover_row(nr);
        needDdDraw = true;
      }
    }
    if (app.activeTab == 2 && app.appearanceChildTab == kAppearanceGeneral) {
      matugen_scheme_dd_sync(app, tcx, tcw);
      matugen_mode_dd_sync(app, tcx, tcw);
      const int scr = settings_scroll_px_int(app);
      if (app.matugenSchemeDd.open()) {
        const int nr = app.matugenSchemeDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                                   app.width, app.height);
        if (nr != app.matugenSchemeDd.hover_row()) {
          app.matugenSchemeDd.set_hover_row(nr);
          needDdDraw = true;
        }
      }
      if (app.matugenModeDd.open()) {
        const int nr2 = app.matugenModeDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                                  app.width, app.height);
        if (nr2 != app.matugenModeDd.hover_row()) {
          app.matugenModeDd.set_hover_row(nr2);
          needDdDraw = true;
        }
      }
    }
    if (app.activeTab == 9 && app.launcherViewModeDropdownOpen) {
      int vcx, vcy, vcw, vch;
      launcher_view_mode_combo_geom(tcx, tcw, &vcx, &vcy, &vcw, &vch);
      const int ly = vcy + vch + 2;
      const int nr = settings_mode_dd_pointer_row(app.pointerX, app.pointerY, vcx, ly, vcw, kSettingsDdRowH, kViewModeCount);
      if (nr != app.launcherViewModeDropdownHoverRow) {
        app.launcherViewModeDropdownHoverRow = nr;
        needDdDraw = true;
      }
    } else if (app.launcherViewModeDropdownHoverRow != -1) {
      app.launcherViewModeDropdownHoverRow = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 10 && app.defaultAppPickerOpen) {
      int plx = 0;
      int ply = 0;
      int plw = 0;
      int pvh = 0;
      int prh = 0;
      int pnRows = 0;
      int pms = 0;
      if (default_app_picker_popup_geom(app, tcx, tcw, &plx, &ply, &plw, &pvh, &prh, &pnRows, &pms)) {
        int hidx = -1;
        if (point_in_rect(app.pointerX, app.pointerY, plx, ply, plw, pvh)) {
          const int rel = static_cast<int>(app.pointerY) - ply + app.defaultAppPickerScrollPx;
          hidx = rel / prh;
          if (hidx < 0 || hidx >= pnRows) hidx = -1;
        }
        if (hidx != app.defaultAppPickerPopupHoverIdx) {
          app.defaultAppPickerPopupHoverIdx = hidx;
          needDdDraw = true;
        }
      }
    } else if (app.defaultAppPickerPopupHoverIdx != -1) {
      app.defaultAppPickerPopupHoverIdx = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 10 && !app.defaultAppPickerOpen) {
      eh::settings::default_apps::DefaultAppsLayout daLay{};
      eh::settings::default_apps::compute_default_apps_layout(tcx, tcw, kContentTop, settings_content_viewport_h(app),
                                                              &daLay);
      const int hrow = default_apps_hit_pill_row(app, app.pointerX, app.pointerY, daLay);
      if (hrow != app.defaultAppsDropdownHoverRow) {
        app.defaultAppsDropdownHoverRow = hrow;
        needDdDraw = true;
      }
    } else if (app.defaultAppsDropdownHoverRow != -1 && !app.defaultAppPickerOpen) {
      app.defaultAppsDropdownHoverRow = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 7 && app.monitorsActiveDd >= 0) {
      const double pyL = app.pointerY + settings_scroll_px(app);
      int tcx = 0;
      int tcw = 0;
      settings_content_column_geom(app, &tcx, &tcw);
      eh::settings_monitors_tab::MonitorsTabLayout monLay{};
      eh::settings_monitors_tab::compute_monitors_tab_layout(tcx, tcw, kContentTop, settings_content_viewport_h(app),
                                                           &monLay);
      monitors_clamp_selected(app);
      const auto& srow = app.monitorsTab.outputs[static_cast<size_t>(app.monitorsSelectedIdx)];
      auto scit = app.monitorsTab.caps.find(srow.name);
      const eh::settings_monitors::OutputCaps* scaps = scit != app.monitorsTab.caps.end() ? &scit->second : nullptr;
      const MonitorsFormRows fr = monitors_form_rows(app, srow, scaps);
      const MonitorsFormGeom fg = monitors_form_layout(monLay, tcx, tcw, fr);
      int dcx = 0, dcy = 0, dcw = 0, dch = 0;
      if (monitors_dd_combo_geom(fr, fg, app.monitorsActiveDd, &dcx, &dcy, &dcw, &dch)) {
        int nrows = 0;
        if (app.monitorsActiveDd == 0 && scaps)
          nrows = static_cast<int>(scaps->resolutions.size());
        else if (app.monitorsActiveDd == 1 && scaps) {
          auto it = scaps->resolution_refresh_hz.find(srow.resolution);
          if (it != scaps->resolution_refresh_hz.end()) nrows = static_cast<int>(it->second.size());
        } else if (app.monitorsActiveDd == 2)
          nrows = 8;
        else if (app.monitorsActiveDd == 3)
          nrows = 2;
        else if (app.monitorsActiveDd == 4)
          nrows = app.monitorsTab.kind == CompositorKind::Mango ? 2 : 3;
        else if (app.monitorsActiveDd == 5)
          nrows = kMonitorCmCount;
        else if (app.monitorsActiveDd == 6 || app.monitorsActiveDd == 7 || app.monitorsActiveDd == 8)
          nrows = 3;
        const int dly =
            monitors_dd_popup_list_doc_top_y(dcy, dch, nrows, settings_scroll_px_int(app), app.height);
        if (nrows > 0) {
          const int nr = settings_mode_dd_pointer_row(app.pointerX, pyL, dcx, dly, dcw, kSettingsDdRowH, nrows);
          if (nr != app.monitorsDdHoverRow) {
            app.monitorsDdHoverRow = nr;
            needDdDraw = true;
          }
        }
      }
    } else if (app.monitorsDdHoverRow != -1) {
      app.monitorsDdHoverRow = -1;
      needDdDraw = true;
    }
    if (app.activeTab == 8 && app.soundActiveDd >= 0) {
      const double pyL = app.pointerY + settings_scroll_px(app);
      eh::audio::PipeWireService::instance().start();
      const eh::audio::Snapshot snap = eh::audio::PipeWireService::instance().snapshot();
      const int n_play = static_cast<int>(snap.output_streams.size());
      const int n_rec = static_cast<int>(snap.input_streams.size());
      const int n_bt_dd = sound_tab_bt_count_clamped(app);
      const SoundTabGeom sg =
          sound_compute_tab_geom(tcx, tcw, kContentTop, settings_content_viewport_h(app), n_play, n_rec, n_bt_dd);
      int dcx = 0;
      int dcy = 0;
      int dcw = 0;
      int dch = 0;
      if (sound_dd_combo_geom(sg, app.soundActiveDd, n_play, n_rec, &dcx, &dcy, &dcw, &dch)) {
        int nrows = 0;
        std::vector<std::string> sound_bt_dd_labels;
        if (app.soundActiveDd == 0)
          nrows = static_cast<int>(snap.sinks.size());
        else if (app.soundActiveDd == 1)
          nrows = static_cast<int>(snap.sources.size());
        else if (app.soundActiveDd == 2)
          nrows = kSoundEngineRateCount;
        else if (app.soundActiveDd == 3)
          nrows = 1 + kSoundEngineRateCount;
        else if (app.soundActiveDd == 4)
          nrows = 4;
        else if (app.soundActiveDd == 5)
          nrows = kSoundCompatPcmChoiceCount;
        else if (app.soundActiveDd >= kSoundBtDdBase && app.soundActiveDd < kSoundBtDdBase + kSoundMaxBtCards) {
          const int bi = app.soundActiveDd - kSoundBtDdBase;
          const auto& btc = settings_sound_bt_cards_cached(app);
          if (bi >= 0 && bi < static_cast<int>(btc.size())) {
            nrows = static_cast<int>(btc[static_cast<size_t>(bi)].profiles.size());
            sound_bt_dd_labels = sound_bt_profile_row_labels(btc[static_cast<size_t>(bi)]);
          }
        } else if (app.soundActiveDd >= 100 && app.soundActiveDd < 1000)
          nrows = static_cast<int>(snap.sinks.size());
        else if (app.soundActiveDd >= 1000)
          nrows = static_cast<int>(snap.sources.size());
        int popup_list_x = dcx;
        int popup_list_w = dcw;
        if (!sound_bt_dd_labels.empty()) {
          const SoundBtPopupGeom pg = sound_bt_dropdown_popup_geom(app, tcx, tcw, dcx, dcw, sound_bt_dd_labels);
          popup_list_x = pg.x;
          popup_list_w = pg.w;
        }
        const int dly = sound_dd_popup_list_doc_top_y(dcy, dch, nrows, settings_scroll_px_int(app), app.height);
        if (nrows > 0) {
          const int nr =
              settings_mode_dd_pointer_row(app.pointerX, pyL, popup_list_x, dly, popup_list_w, kSettingsDdRowH, nrows);
          if (nr != app.soundDdHoverRow) {
            app.soundDdHoverRow = nr;
            needDdDraw = true;
          }
        }
      }
    } else if (app.soundDdHoverRow != -1) {
      app.soundDdHoverRow = -1;
      needDdDraw = true;
    }

    {
      const int sidebarX = kSpacingL;
      const int stX = sidebarX + 8;
      const int stW = kSidebarW - 16;
      int newHover = -1;
      if (app.pointerX >= stX && app.pointerX < stX + stW) {
        const int scroll = app.sidebarScrollPx;
        int y = kSidebarTabBaseY;
        for (int i = 0; i < kSidebarDefCount && newHover < 0; ++i) {
          const auto& def = kSidebarDefs[i];
          if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
          if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
          const bool isExpanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
          const bool hasChildren = def.subCount > 0;
          if (app.pointerY >= y - scroll && app.pointerY < y - scroll + kSidebarTabH) {
            newHover = i * 100;
            break;
          }
          y += kSidebarTabPitchY;
          if (hasChildren && isExpanded) {
            for (int j = 0; j < def.subCount; ++j) {
              const int subX = kSpacingL + kSidebarSubTabIndentX;
              const int subW = kSidebarW - kSidebarSubTabIndentX - 16;
              if (app.pointerY >= y - scroll && app.pointerY < y - scroll + 36 &&
                  app.pointerX >= subX && app.pointerX < subX + subW) {
                newHover = i * 100 + (j + 1);
                break;
              }
              y += 36;
            }
          }
        }
      }
      if (newHover != app.sidebarHoverIdx) {
        app.sidebarHoverIdx = newHover;
        needDdDraw = true;
      }
    }
    if (app.activeTab == 33 && app.hyprlandLayoutDropdownOpen) {
      int layoutListTop = app.hyprlandLayoutComboY + kSettingsComboH + 2;
      const double pyLd = app.pointerY + settings_scroll_px(app);
      int layoutBx = app.hyprlandLayoutComboX;
      const int nr = settings_mode_dd_pointer_row(app.pointerX, pyLd, layoutBx, layoutListTop, 140,
                                                   kSettingsDdRowH, 4);
      if (nr != app.hyprlandLayoutDropdownHoverRow) {
        app.hyprlandLayoutDropdownHoverRow = nr;
        needDdDraw = true;
      }
    } else if (app.hyprlandLayoutDropdownHoverRow != -1) {
      app.hyprlandLayoutDropdownHoverRow = -1;
      needDdDraw = true;
    }

    if (app.activeTab == 45 && app.qtColorSchemeDropdownOpen) {
      const int scr = settings_scroll_px_int(app);
      const int cols = std::max(1, (tcw - 32) / 240);
      const int qtCount = static_cast<int>(eh::theming::known_qt_styles().size());
      const int qtRows = (qtCount + cols - 1) / cols;
      const int gridTop = kContentTop + kTabBarH + kSpacingL + 60;
      const int gridBottom = gridTop + qtRows * (150 + 8) - 8;
      int cbx = tcx + 20 + 140;
      int cbw = tcw - 20 - 140 - 28;
      if (cbw < 100) cbw = 100;
      const int cby = gridBottom + 12;
      const int ly = cby + kQtColorSchemeComboH + 2 - scr;
      const auto qtSchemes = eh::theming::list_qt_color_schemes();
      const int nSchemes = static_cast<int>(qtSchemes.size());
      const int nr = settings_mode_dd_pointer_row(app.pointerX, app.pointerY, cbx, ly, cbw,
                                                  kSettingsDdRowH, nSchemes + 1);
      if (nr != app.qtColorSchemeDropdownHoverRow) {
        app.qtColorSchemeDropdownHoverRow = nr;
        needDdDraw = true;
      }
    } else if (app.qtColorSchemeDropdownHoverRow != -1) {
      app.qtColorSchemeDropdownHoverRow = -1;
      needDdDraw = true;
    }

    if (app.hyprlandBezierOpen && app.activeTab == 33) {
      hyprland_bezier_motion(app, tcx, tcw, needDdDraw);
    }
    if (!app.hyprlandBezierOpen && app.hyprlandAnimEditIdx >= 0 && app.activeTab == 33) {
      hyprland_anim_popup_motion(app, tcx, tcw, needDdDraw);
    }

    // Power tab dropdown hover tracking.
    if (app.activeTab == 32 && !app.pointerLeftDown) {
      const double pyL = app.pointerY + settings_scroll_px(app);

      // Sleep & Power tab (child 0)
      if (app.powerChildTab == 0) {
        if (app.powerBtnDropdownOpen) {
          int cbx, cby, cbw, cbh;
          power_btn_combo_geom(tcx, tcw, cbx, cby, cbw, cbh);
          const int listTop = cby + cbh + 2;
          const int nr = settings_mode_dd_pointer_row(app.pointerX, pyL, cbx, listTop, cbw, kSettingsDdRowH, 4);
          if (nr != app.powerBtnDropdownHoverRow) {
            app.powerBtnDropdownHoverRow = nr;
            needDdDraw = true;
          }
        } else if (app.powerBtnDropdownHoverRow != -1) {
          app.powerBtnDropdownHoverRow = -1;
          needDdDraw = true;
        }

        if (app.lidCloseDropdownOpen) {
          int cbx, cby, cbw, cbh;
          lid_close_combo_geom(tcx, tcw, cbx, cby, cbw, cbh);
          const int listTop = cby + cbh + 2;
          const int nr = settings_mode_dd_pointer_row(app.pointerX, pyL, cbx, listTop, cbw, kSettingsDdRowH, 3);
          if (nr != app.lidCloseDropdownHoverRow) {
            app.lidCloseDropdownHoverRow = nr;
            needDdDraw = true;
          }
        } else if (app.lidCloseDropdownHoverRow != -1) {
          app.lidCloseDropdownHoverRow = -1;
          needDdDraw = true;
        }
      }

      // Performance tab (child 1) — SettingsDropdown hover tracking
      if (app.powerChildTab == 1) {
        const int scr = settings_scroll_px_int(app);
        if (app.powerGovDd.open()) {
          power_gov_dd_sync(app, tcx, tcw);
          const int nr = app.powerGovDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                                 app.width, app.height);
          if (nr != app.powerGovDd.hover_row()) {
            app.powerGovDd.set_hover_row(nr);
            needDdDraw = true;
          }
        }
        if (app.powerEppDd.open()) {
          power_epp_dd_sync(app, tcx, tcw);
          const int nr = app.powerEppDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                                 app.width, app.height);
          if (nr != app.powerEppDd.hover_row()) {
            app.powerEppDd.set_hover_row(nr);
            needDdDraw = true;
          }
        }
        if (app.powerTunedDd.open()) {
          power_tuned_dd_sync(app, tcx, tcw);
          const int nr = app.powerTunedDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                                   app.width, app.height);
          if (nr != app.powerTunedDd.hover_row()) {
            app.powerTunedDd.set_hover_row(nr);
            needDdDraw = true;
          }
        }
      }
    }

    if (needDdDraw) draw(app);
  }

  // Button hover for Notifications tab
  if (!app.pointerLeftDown && app.activeTab == 5) {
    if (app.testNotifBtn.containsPoint(static_cast<float>(app.pointerX), static_cast<float>(app.pointerY))) {
      app.testNotifBtn.handlePointerEnter(static_cast<float>(app.pointerX), static_cast<float>(app.pointerY));
    } else {
      app.testNotifBtn.handlePointerLeave();
    }
  }

  // Real-time hover for Wi‑Fi AP list
  if (!app.pointerLeftDown && app.activeTab == 17 && !wifi_password_prompt_visible(app)) {
    auto& nm = eh::net::NetworkManagerService::instance();
    nm.start();
    nm.refresh();
    const auto& st = nm.state();
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    const int apCardTop = kContentTop + 100 + kCardGap;  // kWifiCardTop + kWifiCardH + kCardGap
    const int totalRows = static_cast<int>(st.accessPoints.size());
    const int apListContentH = totalRows * 48;
    const int maxH = std::max(60, app.height - apCardTop - 52 - kSpacingL - kSpacingL);
    const int apListH = 52 + std::min(apListContentH, maxH);
    const int listY0 = apCardTop + 52;
    const int listH = apListH - 52;
    const int scrollMax = std::max(0, apListContentH - listH);
    const int scrollPx = std::min(app.settingsNetworkScrollPx, scrollMax);

    int newHover = -1;
    for (int i = 0; i < totalRows; ++i) {
      const int ry = listY0 - scrollPx + i * 48;
      if (point_in_rect(app.pointerX, app.pointerY, contentX + kCardPad + kCardPad, ry,
                        contentW - 4 * kCardPad, 48)) {
        newHover = i;
        break;
      }
    }
    if (newHover != app.settingsNetworkHoverRow) {
      app.settingsNetworkHoverRow = newHover;
      draw(app);
    }
  }

  // Real-time hover for Bluetooth device list
  if (!app.pointerLeftDown && app.activeTab == 47) {
    auto& bt = eh::bt::BluezService::instance();
    bt.start();
    const auto state = bt.full_state();
    const auto& devs = state.devices;
    const int contentX = 16 + 240 + 16;
    const int contentW = app.width - contentX - 16;
    const int cardX = contentX + 8;
    const int kScanCardTop = kContentTop + 52 + kSliderRowH + kSpacingXL + kCardGap;
    constexpr int kBTBtnRowH = 36;
    const int devCardTop = kScanCardTop + 52 + kBTBtnRowH + kSpacingXL + kCardGap;
    const int listTop = devCardTop + 52;
    const int baseY = listTop;
    constexpr int kDevRowH = 56;
    const double pyL = app.pointerY + settings_scroll_px(app);
    int newHover = -1;
    for (size_t i = 0; i < devs.size(); ++i) {
      const int rowY = baseY + static_cast<int>(i) * kDevRowH;
      if (point_in_rect(app.pointerX, pyL, cardX + kCardPad, rowY,
                        contentW - 4 * kCardPad, kDevRowH)) {
        newHover = static_cast<int>(i);
        break;
      }
    }
    if (newHover != app.settingsBluetoothHoverRow) {
      app.settingsBluetoothHoverRow = newHover;
      draw(app);
    }
  }

  // Real-time hover for autostart rows
  if (!app.pointerLeftDown && app.activeTab == 48) {
    const int contentX = 16 + kSidebarW + 16;
    const int contentW = app.width - contentX - 16;
    settings_autostart_consume_pointer_move(app, contentX, contentW);
  }

  // Real-time hover for color themes mode pills
  if (!app.pointerLeftDown && app.activeTab == 50) {
    const int contentX = 16 + kSidebarW + 16;
    const int contentW = app.width - contentX - 16;
    settings_color_themes_consume_pointer_move(app, contentX, contentW);
  }

  // Real-time hover for wallpaper thumbnail grid
  if (!app.pointerLeftDown && app.activeTab == 6 && app.wallpaperUiSubTab == 0) {
    const int contentX = kSpacingL + kSidebarW + kSpacingL;
    const int contentW = app.width - contentX - kSpacingL;
    const int cardInsetX = contentX + 8;
    const WallpaperVerticalMetrics wm = wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
    const WallpaperTabLayout wl = wallpaper_tab_layout(app, cardInsetX, contentW, wm.galleryGridTop);
    const double gx0 = static_cast<double>(cardInsetX + kCardPad);
    const int rowStride = wl.thumbH + kWpThumbLabelH + 6;
    const double pyL = app.pointerY + settings_scroll_px(app);

    // Fast bounding box for entire gallery area before any expensive ensure/layout/hit loop.
    const double galleryY0 = static_cast<double>(wl.galleryTop);
    const double galleryY1 = galleryY0 + static_cast<double>(wl.rows) * rowStride;
    const double galleryX1 = gx0 + static_cast<double>(wl.cols) * (wl.thumb + kWpThumbGap);
    const bool overGallery = (app.pointerX >= gx0 && app.pointerX < galleryX1 &&
                              pyL >= galleryY0 && pyL < galleryY1);
    if (overGallery) {
      ensure_wallpaper_gallery(app);
      wallpaper_clamp_page(app, wl.perPage);
    }

    int newHover = -1;
    std::string newHoverPath;
    if (overGallery) {
      const size_t gallOff = static_cast<size_t>(app.wallpaperGalleryPage * wl.perPage);
      for (size_t ii = 0; ii < static_cast<size_t>(wl.perPage); ++ii) {
        const size_t gi = gallOff + ii;
        if (gi >= app.wallpaperGalleryPaths.size()) break;
        const int row = static_cast<int>(ii / static_cast<size_t>(wl.cols));
        const int col = static_cast<int>(ii % static_cast<size_t>(wl.cols));
        const double gx = gx0 + static_cast<double>(col * (wl.thumb + kWpThumbGap));
        const double gy = static_cast<double>(wl.galleryTop) + static_cast<double>(row * rowStride);
        if (point_in_rect(app.pointerX, pyL, static_cast<int>(gx), static_cast<int>(gy), wl.thumb, wl.thumbH)) {
          newHover = static_cast<int>(ii);
          newHoverPath = app.wallpaperGalleryPaths[gi];
          break;
        }
      }
    }

    if (newHover != app.wallpaperHoveredSlot || newHoverPath != app.wallpaperHoveredPath) {
      const bool wasHovering = app.wallpaperHoveredSlot >= 0;
      app.wallpaperHoveredSlot = newHover;
      app.wallpaperHoveredPath = newHoverPath;

      app.wallpaperHoverAnim.cancel_all();
      if (newHover >= 0 && !wasHovering) {
        app.wallpaperHoverAnim.animate(1.0f, 1.10f, 120.0f, eh::shell::Easing::EaseOutQuad,
            [&app](float v) {
              app.wallpaperHoverScale = v;
              schedule_settings_surface_frame(app);
            },
            [&app]() {
              app.wallpaperHoverAnim.animate(1.10f, 1.0f, 180.0f, eh::shell::Easing::EaseOutCubic,
                  [&app](float v) {
                    app.wallpaperHoverScale = v;
                    schedule_settings_surface_frame(app);
                  },
                  nullptr);
            });
      } else if (newHover < 0 && wasHovering) {
        app.wallpaperHoverAnim.animate(app.wallpaperHoverScale, 1.0f, 150.0f, eh::shell::Easing::EaseOutCubic,
            [&app](float v) {
              app.wallpaperHoverScale = v;
              schedule_settings_surface_frame(app);
            },
            nullptr);
      }
      // Throttle immediate draw on hover change for high refresh (240Hz) smoothness.
      // The frame callback + hover anim will drive further updates at compositor rate.
      static auto lastWpHoverDraw = std::chrono::steady_clock::now();
      const auto now = std::chrono::steady_clock::now();
      const double elapsedMs = std::chrono::duration<double, std::milli>(now - lastWpHoverDraw).count();
      if (elapsedMs >= 4.0) {
        lastWpHoverDraw = now;
        draw(app);
      } else {
        app.pendingRedraw = true;
        schedule_settings_surface_frame(app);
      }
    }
  }

  if (app.widgetPickerOpen) {
    const std::vector<int> vis = eh::settings::widget_picker::visible_indices(app);
    int vw{};
    int vh{};
    int inset{};
    eh::settings::widget_picker::viewport_for_pointer(app, ptrSurf, &vw, &vh, &inset);
    const eh::settings::widget_picker::WidgetPickerLayout pk =
        eh::settings::widget_picker::layout_for(app, static_cast<int>(vis.size()), vw, vh, inset);
    int hover = -1;
    for (int slot = 0; slot < static_cast<int>(vis.size()); ++slot) {
      const int row = slot / 2;
      const int col = slot % 2;
      const int cx = pk.gridX + col * (pk.cardW + pk.colGap);
      const int cy = pk.gridY + row * (pk.cardH + pk.rowGap) - app.widgetPickerScrollPx;
      if (cy + pk.cardH < pk.gridY || cy >= pk.gridY + pk.gridClipH) continue;
      if (point_in_rect(app.pointerX, app.pointerY, cx, cy, pk.cardW, pk.cardH)) {
        hover = slot;
        break;
      }
    }
    if (hover != app.widgetPickerHoverSlot) {
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "pointer_move: widget picker hover slot=%d", hover);
        debug_log("settings", "%s", buf);
      }
      app.widgetPickerHoverSlot = hover;
      draw(app);
    }
  }

  // Redraw on pointer motion for tabs that need instant button hover.
  // Skip motions in the sidebar to avoid input lag at high pointer speeds —
  // the sidebar (40px items) doesn't need per-pixel tracking and will
  // be updated by the next tab-content redraw.
  // Throttle to ~60 fps to prevent flooding the compositor at high pointer
  // rates (e.g. 1000+ Hz mice) which otherwise causes visible lag.
  if (!app.pointerLeftDown && !app.widgetPickerOpen) {
    static auto lastMotionDraw = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastMotionDraw >= std::chrono::milliseconds(16)) {
      lastMotionDraw = now;
      const int sidebarBoundary = kSpacingL + kSidebarW;
      if (app.pointerX >= sidebarBoundary) {
        draw(app);
      }
    }
  }
}

void on_pointer_button(App& app, wl_surface* ptrSurf, uint32_t button, uint32_t state,
                              uint32_t pointer_btn_serial) {
  if (!ptrSurf && !app.embedded) ptrSurf = app.seat.pointer_focus_surface();
  (void)pointer_btn_serial;
  if (button != 0x110  ) return;

  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    if (world_clock_popup_visible(app)) {
      app.pointerLeftDown = false;
      world_clock_popup_consume_pointer_up(app);
      return;
    }
    if (keyring_prompt_visible(app)) {
      keyring_prompt_consume_pointer_up(app);
      draw(app);
      return;
    }
    if (wifi_password_prompt_visible(app)) {
      wifi_password_prompt_consume_pointer_up(app);
      draw(app);
      return;
    }
    if (vpn_add_dialog_visible(app)) {
      vpn_add_dialog_consume_pointer_up(app);
      draw(app);
      return;
    }
    if (app.soundVolDragCode >= 0) {
      settings_sound_vol_pw_drag_commit_final(app);
      eh::audio::PipeWireService::instance().start();
      const eh::audio::Snapshot snap = eh::audio::PipeWireService::instance().snapshot();
      for (const auto& d : snap.sinks) {
        if (d.node_id == snap.default_sink) {
          app.settings.audioDefaultSinkVolumePct = d.volume_pct;
          app.settings.audioDefaultSinkMuted = d.muted;
          break;
        }
      }
      for (const auto& d : snap.sources) {
        if (d.node_id == snap.default_source) {
          app.settings.audioDefaultSourceVolumePct = d.volume_pct;
          app.settings.audioDefaultSourceMuted = d.muted;
          break;
        }
      }
    }
    // M3 dock tab: commit toggle/slider state
    if (app.activeTab == 0) {
      // Save appearance slider values before clearing drag state
      if (app.sliderDrag >= 200 && app.sliderDrag < 212) {
        {
          char buf[128];
          std::snprintf(buf, sizeof(buf), "pointer_up: saving dock slider sliderDrag=%d", app.sliderDrag);
          debug_log("settings", "%s", buf);
        }
        save_settings(app.settings);
        app.sliderDrag = -1;
        app.settingsSliderDragNormT = -1.0;
      }
      const double ly = app.pointerY + settings_scroll_px(app);
      if (dock_renderer_dd_commit_pointer_up(app, static_cast<float>(app.pointerX),
                                             static_cast<float>(app.pointerY))) {
        save_settings(app.settings);
      } else if (dock_m3_handle_pointer_up(app, static_cast<float>(app.pointerX), static_cast<float>(ly))) {
        debug_log("settings", "pointer_up: dock_m3_handle_pointer_up triggered save");
        save_settings(app.settings);
      }
    }

    // M3 workspace tab: commit toggle/slider state
    if (app.activeTab == 33) {
      const double ly = app.pointerY + settings_scroll_px(app);
      if (hyprland_m3_handle_pointer_up(app, static_cast<float>(app.pointerX), static_cast<float>(ly), app.hyprlandChildTab)) {
        hyprland_commit_cfg(app);
        save_settings(app.settings);
      }
    }

    // M3 launcher tab: commit toggle/slider state
    if (app.activeTab == 9) {
      const double ly = app.pointerY + settings_scroll_px(app);
      if (launcher_m3_handle_pointer_up(app, static_cast<float>(app.pointerX), static_cast<float>(ly))) {
        save_settings(app.settings);
      }
    }

    // M3 taskbar tab: commit toggle/slider state
    if (app.activeTab == 11) {
      const double ly = app.pointerY + settings_scroll_px(app);
      if (taskbar_m3_handle_pointer_up(app, static_cast<float>(app.pointerX), static_cast<float>(ly))) {
        save_settings(app.settings);
      }
    }

    const bool had_slider_drag = settings_slider_drag_pending(app);
    if (had_slider_drag && app.embedded) settings_publish_embedded_drag_preview_throttled(app, true);
    if (had_slider_drag) {
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "pointer_up: general slider drag save tab=%d sliderDrag=%d",
                      app.activeTab, app.sliderDrag);
        debug_log("settings", "%s", buf);
      }
      save_settings(app.settings);
    }
    if (app.activeTab == 7 && app.monitorsCanvasDragIdx >= 0) {
      const size_t mix = static_cast<size_t>(app.monitorsCanvasDragIdx);
      if (mix < app.monitorsTab.outputs.size()) {
        eh::settings_monitors_tab::finalize_arrangement_drag(app.monitorsTab.outputs, app.monitorsTab.caps, mix);
        app.monitorsTab.dirty = true;
      }
    }
    app.monitorsCanvasDragIdx = -1;
    app.monitorsCanvasPanArmed = false;
    app.pointerLeftDown = false;
    app.sliderDrag = -1;
    app.wsSliderDrag = -1;
    app.launcherSliderDrag = -1;
    app.notifSliderDrag = -1;
    app.appearanceOverlaySliderDrag = -1;
    app.appearanceColorSliderDrag = -1;
    if (app.mangoSliderDrag >= 0) mango_commit_cfg(app);
    app.mangoSliderDrag = -1;
    app.themesSliderDrag = -1;
    app.monitorsScaleSliderDragIdx = -1;
    app.monitorsHyprExtraSlider = -1;
    app.soundVolDragCode = -1;
    app.soundPaintSnapPending.reset();
    app.settingsSliderDragNormT = -1.0;
    app.keyboardSliderDrag = -1;
    app.timeSliderDrag = -1;
    app.settings_paint_shell_snapshot_valid = false;
    if (app.widgetDragging && app.widgetDragSection >= 0 && app.widgetDragFromIndex >= 0) {
      widget_drag_apply(app);
    }
    app.settingsWidgetDragRepaintQueued = false;
    app.widgetDragging = false;
    app.widgetDragArmed = false;
    app.widgetDragSection = App::kSectionNone;
    app.widgetDragTargetSection = App::kSectionNone;
    app.widgetDragFromIndex = -1;
    if (app.activeTab == 5) {
      app.testNotifBtn.handlePointerUp(static_cast<float>(app.pointerX), static_cast<float>(app.pointerY));
    }
    if (app.activeTab == 18) {
      const int upContentX = 16 + 260 + 16;
      const int upContentW = app.width - upContentX - 16;
      if (settings_wired_consume_pointer_up(app, upContentX, upContentW)) { draw(app); return; }
    }
    app.sidebarHoverIdx = -1;
    draw(app);
    return;
  }

  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  app.pointerLeftDown = true;

  if (world_clock_popup_visible(app)) {
    world_clock_popup_consume_pointer_down(app);
    return;
  }

  // Title bar button clicks (embedded only).
  if (app.embedded) {
    constexpr int kBtnSize = 20;
    constexpr int kBtnGap  = 8;
    const int btnY = (kHeaderH - kBtnSize) / 2;
    int bx = app.width - kSpacingL;
    bx -= kBtnSize;
    if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
      eh::settings::embed_toggle();  // close (red, rightmost)
      return;
    }
    bx -= kBtnGap;
    bx -= kBtnSize;
    if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
      eh::settings::embed_maximize_toggle();  // maximize (yellow, middle)
      return;
    }
    bx -= kBtnGap;
    bx -= kBtnSize;
    if (point_in_rect(app.pointerX, app.pointerY, bx, btnY, kBtnSize, kBtnSize)) {
      eh::settings::embed_minimize();  // minimize (green, leftmost)
      return;
    }
  }

  const int contentX = 16 + kSidebarW + 16;
  const int contentW = app.width - contentX - 16;
  const int sidebarXHit = kSpacingL;
  const int sidebarTabX = sidebarXHit + 8;
  const int sidebarTabW = kSidebarW - 16;
  auto add_widget_to = [&](std::vector<std::string>& target, const std::string& w) {
    if (std::find(target.begin(), target.end(), w) != target.end()) return;
    target.push_back(w);
    save_settings(app.settings);
    draw(app);
  };

  if (app.widgetPickerOpen) {
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "pointer_up: widget picker click px=%.0f py=%.0f", app.pointerX, app.pointerY);
      debug_log("settings", "%s", buf);
    }
    const std::vector<int> vis = eh::settings::widget_picker::visible_indices(app);
    int vw{};
    int vh{};
    int inset{};
    eh::settings::widget_picker::viewport_for_pointer(app, ptrSurf, &vw, &vh, &inset);
    const eh::settings::widget_picker::WidgetPickerLayout pk =
        eh::settings::widget_picker::layout_for(app, static_cast<int>(vis.size()), vw, vh, inset);

    if (!point_in_rect(app.pointerX, app.pointerY, pk.px, pk.py, pk.pw, pk.ph)) {
      debug_log("settings", "pointer_up: widget picker click outside bounds -> closing");
      eh::settings::widget_picker::close_picker(app);
      draw(app);
      return;
    }

    if (point_in_rect(app.pointerX, app.pointerY, pk.closeX, pk.closeY, pk.closeW, pk.closeH)) {
      eh::settings::widget_picker::close_picker(app);
      draw(app);
      return;
    }

    if (!app.widgetPickerFilter.empty() &&
        point_in_rect(app.pointerX, app.pointerY, pk.clearX, pk.clearY, pk.clearW, pk.clearH)) {
      debug_log("settings", "pointer_up: widget picker clear filter");
      app.widgetPickerFilter.clear();
      app.widgetPickerHoverSlot = -1;
      app.widgetPickerScrollPx = 0;
      draw(app);
      return;
    }

    if (point_in_rect(app.pointerX, app.pointerY, pk.searchX, pk.searchY, pk.searchW, pk.searchH)) {
      if (!app.widgetPickerSearchActive) {
        debug_log("settings", "pointer_up: widget picker focus search");
        eh::settings::widget_picker::focus_search(app);
      }
      return;
    }

    for (int slot = 0; slot < static_cast<int>(vis.size()); ++slot) {
      const int row = slot / 2;
      const int col = slot % 2;
      const int cx = pk.gridX + col * (pk.cardW + pk.colGap);
      const int cy = pk.gridY + row * (pk.cardH + pk.rowGap) - app.widgetPickerScrollPx;
      if (cy + pk.cardH < pk.gridY || cy >= pk.gridY + pk.gridClipH) continue;
      if (!point_in_rect(app.pointerX, app.pointerY, cx, cy, pk.cardW, pk.cardH)) continue;
      const int vi = vis[static_cast<size_t>(slot)];
      const std::string w = eh::settings::widget_picker::widget_id_for_visible_index(vi);
      {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "pointer_up: widget picker picked widget=%s slot=%d vi=%d", w.c_str(), slot, vi);
        debug_log("settings", "%s", buf);
      }
      if (app.widgetPickerForDesktop) {
        auto configs = eh::shell::desktop::desktop_widgets_prefs_load();
        DesktopWidgetConfig newCfg;
        newCfg.type = widget_id_to_desktop_widget_type(w);
        newCfg.widgetId = w;
        char idBuf[32];
        std::snprintf(idBuf, sizeof(idBuf), "%s%zu", w.c_str(), configs.size() + 1);
        newCfg.id = idBuf;
        newCfg.posX = 100;
        newCfg.posY = 100 + static_cast<int>(configs.size() * 150);
        configs.push_back(newCfg);
        eh::shell::desktop::desktop_widgets_prefs_save(configs);
        app.settings.desktopWidgets = configs;
        save_settings(app.settings);
      } else if (app.widgetPickerForTaskbar) {
        if (app.widgetPickerSection == "left") add_widget_to(app.settings.taskbarLeftWidgets, w);
        else if (app.widgetPickerSection == "center") add_widget_to(app.settings.taskbarCenterWidgets, w);
        else add_widget_to(app.settings.taskbarRightWidgets, w);
      } else {
        if (app.widgetPickerSection == "left") add_widget_to(app.settings.leftWidgets, w);
        else if (app.widgetPickerSection == "center") add_widget_to(app.settings.centerWidgets, w);
        else add_widget_to(app.settings.rightWidgets, w);
      }
      eh::settings::widget_picker::close_picker(app);
      draw(app);
      return;
    }
    return;
  }

  if (app.wallpaperFolderPickerOpen) {
    if (app.activeTab != 6) {
      app.wallpaperFolderPickerOpen = false;
      draw(app);
      return;
    }
    int wpx = 0;
    int wpy = 0;
    int wpw = 0;
    int wph = 0;
    int wrh = 0;
    int wn = 0;
    wallpaper_folder_picker_modal_geom(app, &wpx, &wpy, &wpw, &wph, &wrh, &wn);
    if (!point_in_rect(app.pointerX, app.pointerY, wpx, wpy, wpw, wph)) {
      app.wallpaperFolderPickerOpen = false;
      draw(app);
      return;
    }
    const int listY0 = wpy + 56;
    if (app.pointerY >= listY0 && app.pointerY < listY0 + wn * wrh) {
      const int idx = static_cast<int>((app.pointerY - listY0) / wrh);
      if (idx >= 0 && idx < wn) {
        app.settings.wallpaperFolderPickerMode = idx;
        save_settings(app.settings);
      }
    }
    app.wallpaperFolderPickerOpen = false;
    draw(app);
    return;
  }

  if (app.iconThemePickerOpen) {
    const std::string sys = eh::icons::detect_system_icon_theme();
    std::vector<eh::icons::ThemeInfo> themes;
    themes.reserve(app.iconThemes.size());
    for (const auto& t : app.iconThemes) {
      themes.push_back(t);
    }
    const int n = static_cast<int>(themes.size());
    int rowPitch = 34;
    const int pw = 520;
    const int maxBodyH = std::max(200, app.height - 48);
    int ph = 56 + n * rowPitch + 18;
    while (ph > maxBodyH && rowPitch > 22) {
      rowPitch -= 2;
      ph = 56 + n * rowPitch + 18;
    }
    const int px = std::max(20, (app.width - pw) / 2);
    const int py = std::max(20, (app.height - ph) / 2);
    if (!point_in_rect(app.pointerX, app.pointerY, px, py, pw, ph)) {
      app.iconThemePickerOpen = false;
      draw(app);
      return;
    }
    const int listY0 = py + 56;
    if (app.pointerY >= listY0 && app.pointerY < listY0 + n * rowPitch) {
      const int idx = static_cast<int>((app.pointerY - listY0) / rowPitch);
      if (static_cast<size_t>(idx) < themes.size()) app.settings.iconTheme = themes[static_cast<size_t>(idx)].id;
      save_settings(app.settings);

      app.iconThemePickerOpen = false;
      draw(app);
      return;
    }
    return;
  }

  if (app.defaultAppPickerOpen) {
    if (app.activeTab != 10) {
      app.defaultAppPickerOpen = false;
      app.defaultAppPickerPopupHoverIdx = -1;
      draw(app);
      return;
    }
    int lx = 0;
    int ly = 0;
    int lw = 0;
    int vh = 0;
    int rh = 0;
    int nRows = 0;
    int maxScroll = 0;
    if (!default_app_picker_popup_geom(app, contentX, contentW, &lx, &ly, &lw, &vh, &rh, &nRows, &maxScroll)) {
      app.defaultAppPickerOpen = false;
      app.defaultAppPickerPopupHoverIdx = -1;
      draw(app);
      return;
    }
    if (point_in_rect(app.pointerX, app.pointerY, lx, ly, lw, vh)) {
      const int relY = static_cast<int>(app.pointerY) - ly + app.defaultAppPickerScrollPx;
      const int idx = relY / rh;
      if (idx >= 0 && idx < nRows) {
        if (idx == 0) {
          eh::settings::default_apps::clear_stored_desktop_id(app.settings.defaultApps, app.defaultAppPickerCategory);
          save_settings(app.settings);
        } else {
          const eh::app_drawer::DesktopEntry& e = app.defaultAppPickerEntries[static_cast<size_t>(idx - 1)];
          std::string base = e.path;
          if (const size_t sl = base.rfind('/'); sl != std::string::npos) base = base.substr(sl + 1);
          eh::settings::default_apps::set_stored_desktop_id(app.settings.defaultApps, app.defaultAppPickerCategory,
                                                            std::move(base));
          save_settings(app.settings);
        }
        app.defaultAppPickerOpen = false;
        app.defaultAppPickerPopupHoverIdx = -1;
        draw(app);
        return;
      }
    }

    eh::settings::default_apps::DefaultAppsLayout daLayPick{};
    eh::settings::default_apps::compute_default_apps_layout(contentX, contentW, kContentTop, settings_content_viewport_h(app),
                                                            &daLayPick);
    const int pickRow = default_apps_hit_pill_row(app, app.pointerX, app.pointerY, daLayPick);
    if (pickRow >= 0) {
      const int r = pickRow;
      if (r == app.defaultAppPickerCategory) {
        app.defaultAppPickerOpen = false;
        app.defaultAppPickerPopupHoverIdx = -1;
        draw(app);
        return;
      }
      app.defaultAppPickerCategory = r;
      settings_fill_default_app_picker(app);
      app.defaultAppPickerPopupHoverIdx = -1;
      draw(app);
      return;
    }

    app.defaultAppPickerOpen = false;
    app.defaultAppPickerPopupHoverIdx = -1;
    draw(app);
    return;
  }

  // Keyring prompt consumes pointer before content clicks
  if (keyring_prompt_visible(app)) {
    if (keyring_prompt_consume_pointer_down(app)) {
      draw(app);
      return;
    }
  }

  // Password prompt overlay consumes pointer before content clicks
  if (wifi_password_prompt_visible(app)) {
    if (wifi_password_prompt_consume_pointer_down(app)) {
      draw(app);
      return;
    }
  }

  // VPN add dialog overlay
  if (vpn_add_dialog_visible(app)) {
    if (vpn_add_dialog_consume_pointer_down(app)) {
      draw(app);
      return;
    }
  }

  const int scroll = app.sidebarScrollPx;
  int currentY = kSidebarTabBaseY;
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    const bool isExpanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
    const bool hasChildren = def.subCount > 0;

    if (point_in_rect(app.pointerX, app.pointerY, sidebarTabX, currentY - scroll, sidebarTabW, kSidebarTabH)) {
      if (hasChildren) {
        settings_close_mode_dropdowns(app);
        if (isExpanded) {
          app.sidebarExpanded.erase(def.label);
        } else {
          app.sidebarExpanded.insert(def.label);
        }
        app.sidebarHoverIdx = -1;
        app.sidebarScrollPx = 0;
        draw(app);
        return;
      } else if (!def.isCategory) {
        settings_close_mode_dropdowns(app);
        if (app.activeTab == 44) { clear_icons_preview_cache(); icons_backup_reset(); }
        if (app.activeTab == 45) themes_backup_reset();
        app.activeTab = def.id;
        app.activeSubTab = -1;
        app.sidebarHoverIdx = -1;
        if (def.id == 7 && app.activeTab != 7) {
          app.monitorsTab.refresh_from_system();
          app.monitorsTabDidInitialRefresh = true;
        }
        if (def.id == 8) eh::audio::PipeWireService::instance().start();
        if (def.id == 17 || def.id == 33) eh::net::NetworkManagerService::instance().start();
        draw(app);
        return;
      }
    }
    currentY += kSidebarTabPitchY;

    if (hasChildren && isExpanded) {
      for (int j = 0; j < def.subCount; ++j) {
        const int subHitX = sidebarXHit + kSidebarSubTabIndentX;
        const int subHitW = kSidebarW - kSidebarSubTabIndentX - 16;
        if (point_in_rect(app.pointerX, app.pointerY, subHitX, currentY - scroll,
                          subHitW, 36)) {
          settings_close_mode_dropdowns(app);
          if (def.subTabIds) {
            if (app.activeTab == 44) { clear_icons_preview_cache(); icons_backup_reset(); }
            if (app.activeTab == 45) themes_backup_reset();
            app.activeTab = def.subTabIds[j];
            app.activeSubTab = -1;
            if (def.subTabIds[j] == 7) {
              app.monitorsTab.refresh_from_system();
              app.monitorsTabDidInitialRefresh = true;
            }
            if (def.subTabIds[j] == 8) eh::audio::PipeWireService::instance().start();
            if (def.subTabIds[j] == 17 || def.subTabIds[j] == 18 || def.subTabIds[j] == 28) eh::net::NetworkManagerService::instance().start();
            if (def.subTabIds[j] == 47) eh::bt::BluezService::instance().start();
            if (def.subTabIds[j] >= 20 && def.subTabIds[j] <= 26) {
              auto mcf = eh::settings_mango::read_mango_config();
              app.mangoConfig = mcf.cfg;
              app.mangoConfigLines = mcf.lines;
            }
            if (def.subTabIds[j] >= 33 && def.subTabIds[j] <= 43) {
              app.hyprlandConfig = eh::settings_hyprland::read_config();
            }
            if (def.subTabIds[j] == 48) {
              app.autostartNeedsRefresh = true;
            }
          } else {
            if (app.activeTab == 44) { clear_icons_preview_cache(); icons_backup_reset(); }
            if (app.activeTab == 45) themes_backup_reset();
        app.activeTab = def.id;
            app.activeSubTab = j;
          }
          app.sidebarHoverIdx = -1;
          draw(app);
          return;
        }
        currentY += 36;
      }
    }
  }

  if (app.wallpaperModeDropdownOpen && app.activeTab == 6) {
    int cx, cy, cw, ch;
    WallpaperVerticalMetrics wm = wallpaper_vertical_metrics(static_cast<double>(contentX), static_cast<double>(contentW));
    wallpaper_mode_combo_geom(contentX, contentW, wm.modeRowY, &cx, &cy, &cw, &ch);
    const double pyL = app.pointerY + settings_scroll_px(app);
    const int ly = cy + ch + 2;
    const int lh = 5 * kSettingsDdRowH;
    if (point_in_rect(app.pointerX, pyL, cx, ly, cw, lh)) {
      const int rr = settings_mode_dd_pointer_row(app.pointerX, pyL, cx, ly, cw, kSettingsDdRowH, 5);
      if (rr >= 0 && rr <= 4) app.settings.wallpaperMode = rr;
      save_settings(app.settings);
      settings_close_mode_dropdowns(app);
      draw(app);
      return;
    }
    if (point_in_rect(app.pointerX, pyL, cx, cy, cw, ch)) {
      settings_close_mode_dropdowns(app);
      draw(app);
      return;
    }
    settings_close_mode_dropdowns(app);
    draw(app);
    return;
  }

  if (app.matugenSchemeDd.open() && app.activeTab == 2) {
    matugen_scheme_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    const int rr = app.matugenSchemeDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                               app.width, app.height);
    if (rr >= 0) {
      if (rr < kMatugenSchemeCount) app.settings.matugenScheme = kMatugenSchemeValues[rr];
      save_settings(app.settings);
    }
    settings_close_mode_dropdowns(app);
    draw(app);
    return;
  }

  if (app.matugenModeDd.open() && app.activeTab == 2) {
    matugen_mode_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    const int rr = app.matugenModeDd.hit_row(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), scr,
                                             app.width, app.height);
    if (rr >= 0) {
      if (rr < kMatugenModeCount) app.settings.matugenMode = kMatugenModeValues[rr];
      save_settings(app.settings);
    }
    settings_close_mode_dropdowns(app);
    draw(app);
    return;
  }

  if (app.activeTab == 32 && app.powerChildTab == 1) {
    if (settings_power_consume_performance_dropdown_pointer_up(app, contentX, contentW)) return;
  }

  if (app.qtColorSchemeDropdownOpen && app.activeTab == 45) {
    const int scr = settings_scroll_px_int(app);
    const double pyL = app.pointerY + static_cast<double>(scr);
    const int subTab = app.themesSubTab;
    if (subTab == 1) {
      const int cols = std::max(1, (contentW - 32) / 240);
      const int qtCount = static_cast<int>(eh::theming::known_qt_styles().size());
      const int qtRows = (qtCount + cols - 1) / cols;
      const int gridTop = kContentTop + kTabBarH + kSpacingL + 60;
      const int gridBottom = gridTop + qtRows * (150 + 8) - 8;
      int cbx = contentX + 20 + 140;
      int cbw = contentW - 20 - 140 - 28;
      if (cbw < 100) cbw = 100;
      const int cby = gridBottom + 12;
      const int ly = cby + kQtColorSchemeComboH + 2 - scr;
      const auto qtSchemes = eh::theming::list_qt_color_schemes();
      const int nSchemes = static_cast<int>(qtSchemes.size());

      if (point_in_rect(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), cbx, ly, cbw,
                        (nSchemes + 1) * kSettingsDdRowH)) {
        const int rr = settings_mode_dd_pointer_row(app.pointerX, app.pointerY, cbx, ly, cbw,
                                                    kSettingsDdRowH, nSchemes + 1);
        if (rr >= 0) {
          set_pending_qt_color_scheme((rr == 0) ? "" : qtSchemes[static_cast<size_t>(rr - 1)]);
        }
        settings_close_mode_dropdowns(app);
        draw(app);
        return;
      }
      if (point_in_rect(static_cast<int>(app.pointerX), static_cast<int>(pyL), cbx, cby, cbw, kQtColorSchemeComboH)) {
        settings_close_mode_dropdowns(app);
        draw(app);
        return;
      }
      settings_close_mode_dropdowns(app);
      draw(app);
      return;
    }
    settings_close_mode_dropdowns(app);
    draw(app);
    return;
  }

  if (app.launcherViewModeDropdownOpen && app.activeTab == 9) {
    int vcx, vcy, vcw, vch;
    launcher_view_mode_combo_geom(contentX, contentW, &vcx, &vcy, &vcw, &vch);
    const int ly = vcy + vch + 2;
    if (point_in_rect(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), vcx, ly, vcw,
                      kViewModeCount * kSettingsDdRowH)) {
      const int rr =
          settings_mode_dd_pointer_row(app.pointerX, app.pointerY, vcx, ly, vcw, kSettingsDdRowH, kViewModeCount);
      if (rr >= 0 && rr < kViewModeCount) app.settings.launchpadViewMode = rr;
      save_settings(app.settings);
      settings_close_non_default_app_dropdowns(app);
      draw(app);
      return;
    }
    if (point_in_rect(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), vcx, vcy, vcw, vch)) {
      settings_close_non_default_app_dropdowns(app);
      draw(app);
      return;
    }
    settings_close_non_default_app_dropdowns(app);
    draw(app);
    return;
  }

  if (app.activeTab == 3 && settings_layout_consume_pointer_down(app, contentX, contentW)) {
    draw(app);
    return;
  }

  if (app.activeTab == 0) {
    const double ly = settings_logical_content_y_tab01(app);
    if (dock_m3_handle_pointer_down(app, static_cast<float>(app.pointerX), static_cast<float>(ly), contentX, contentW)) {
      return;
    }
    // Delegate to Appearance child tab if M3 didn't consume it
    if (dock_appearance_consume_pointer_down(app, contentX, contentW)) {
      draw(app);
      return;
    }
  }

  if (app.activeTab == 11) {
    const double ly = settings_logical_content_y_tab01(app);
    if (taskbar_m3_handle_pointer_down(app, static_cast<float>(app.pointerX), static_cast<float>(ly), contentX, contentW)) {
      return;
    }
    // M3 handled nothing — click was outside any M3 widget area, nothing left to do
  }
  if (app.activeTab == 2) {
    if (settings_appearance_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 44) {
    if (settings_icons_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 45) {
    if (settings_themes_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 50) {
    if (settings_color_themes_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 4) {
    int trX, trY, trW;
    // Slider items 0 and 1
    for (int row = 0; row < 2; ++row) {
      int cardX, cardY, cardW;
      workspace_app_geom(contentX, contentW, row, trX, trY, trW, cardX, cardY, cardW);
      if (point_in_rect(app.pointerX, app.pointerY, trX - 6, trY - 10, trW + 12, 36)) {
        app.wsSliderDrag = row;
        apply_workspace_slider_x(app, row, app.pointerX);
        app.settingsSliderDragNormT = slider_norm_from_x(app.pointerX, trX, trW);
        draw(app);
        return;
      }
    }
    // Toggle at item 2
    {
      int cardX, cardY, cardW;
      workspace_app_geom(contentX, contentW, 2, trX, trY, trW, cardX, cardY, cardW);
      constexpr int swW = 52, swH = 26;
      const int swX = cardX + cardW - swW - kSpacingXL;
      const int swY = cardY - 24 + (92 - swH) / 2;
      if (point_in_rect(app.pointerX, app.pointerY, swX, swY, swW, swH)) {
        app.settings.workspacesShowApps = !app.settings.workspacesShowApps;
        save_settings(app.settings);
        draw(app);
        return;
      }
    }
  }

  if (app.activeTab == 9) {
    const double ly = app.pointerY + settings_scroll_px(app);
    if (launcher_m3_handle_pointer_down(app, static_cast<float>(app.pointerX), static_cast<float>(ly),
                                        contentX, contentW)) {
      return;
    }
  }

  if (app.activeTab == 5) {
    if (settings_notifications_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 7) {
    if (settings_monitors_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 8) {
    if (settings_sound_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 10) {
    if (settings_default_apps_consume_pointer_down(app, contentX, contentW)) return;
  }

  if (app.activeTab == 6) {
    if (settings_wallpaper_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 16) {
    handle_bing_click(app, contentX, contentW);
  }
  if (app.activeTab == 17) {
    if (settings_wifi_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 18) {
    if (settings_wired_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 28) {
    if (settings_vpn_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 19) {
    if (nightlight_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab >= 20 && app.activeTab <= 26) {
    if (settings_mango_consume_pointer_down(app, contentX, contentW, app.activeTab - 20)) return;
  }
  if (app.activeTab == 33) {
    if (app.hyprlandBezierOpen && hyprland_bezier_pointer_down(app, contentX, contentW)) return;
    if (!app.hyprlandBezierOpen && app.hyprlandAnimEditIdx >= 0 && hyprland_anim_popup_pointer_down(app, contentX, contentW)) return;
    const double ly = app.pointerY + settings_scroll_px(app);
    if (app.hyprlandChildTab == 0) {
      constexpr int kLayoutComboW = 140;
      if (app.hyprlandLayoutDropdownOpen) {
        const int cbx = app.hyprlandLayoutComboX;
        const int listTop = app.hyprlandLayoutComboY + kSettingsComboH + 2;
        if (point_in_rect(app.pointerX, ly, cbx, listTop, kLayoutComboW, 4 * kSettingsDdRowH)) {
          const int rr = settings_mode_dd_pointer_row(app.pointerX, ly, cbx, listTop, kLayoutComboW, kSettingsDdRowH, 4);
          if (rr >= 0 && rr < 4) {
            static const char* kHyprLayoutLabels[] = {"dwindle", "master", "scrolling", "monocle"};
            app.hyprlandConfig.general.layout = kHyprLayoutLabels[rr];
            hyprland_commit_cfg(app);
            save_settings(app.settings);
          }
          settings_close_mode_dropdowns(app);
          draw(app);
          return;
        }
        settings_close_mode_dropdowns(app);
        draw(app);
        return;
      }
      if (point_in_rect(app.pointerX, ly, app.hyprlandLayoutComboX, app.hyprlandLayoutComboY,
                        kLayoutComboW, kSettingsComboH)) {
        settings_close_mode_dropdowns(app);
        app.hyprlandLayoutDropdownOpen = true;
        draw(app);
        return;
      }
    }
    if (hyprland_m3_handle_pointer_down(app, static_cast<float>(app.pointerX), static_cast<float>(ly), contentX, contentW)) return;
  }
  if (app.activeTab == 27) {
    if (settings_desktop_widgets_handle_remove_click(app, contentX, contentW)) return;
    if (settings_desktop_widgets_handle_settings_click(app, contentX, contentW)) return;
    if (settings_desktop_widgets_handle_toggle_click(app, contentX, contentW)) return;
    if (settings_desktop_widgets_handle_add_click(app, contentX, contentW)) return;
  }
  if (app.activeTab == 29) {
    if (settings_desktop_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 30) {
    if (settings_time_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 31) {
    if (settings_keyboard_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 32) {
    if (settings_power_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 47) {
    if (settings_bluetooth_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 46) {
    if (settings_accounts_consume_pointer_down(app, contentX, contentW)) return;
  }
  if (app.activeTab == 48) {
    if (settings_autostart_consume_pointer_down(app, contentX, contentW)) return;
  }
  // Note: taskbar widget add/remove/toggle/drag is handled by the M3 Widgets child tab.
}

void on_settings_keyboard(App& app, const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
  if (keyring_prompt_visible(app)) {
    keyring_prompt_handle_key(app, ev.sym, ev.state, ev.utf8.data(), ev.utf8_len);
    draw(app);
    return;
  }
  if (wifi_password_prompt_visible(app)) {
    wifi_password_prompt_handle_key(app, ev.sym, ev.state, ev.utf8.data(), ev.utf8_len);
    draw(app);
    return;
  }
  if (vpn_add_dialog_visible(app)) {
    vpn_add_dialog_handle_key(app, ev.sym, ev.state, ev.utf8.data(), ev.utf8_len);
    draw(app);
    return;
  }

  if (world_clock_popup_visible(app)) {
    world_clock_popup_consume_key(app, ev.sym, ev.state, ev.utf8.data(), ev.utf8_len);
    return;
  }

  // Accounts tab text entry
  if (app.activeTab == 46 && app.accountsActiveField != AccountsField::None) {
    if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    auto& buf = [&]() -> std::string& {
      switch (app.accountsActiveField) {
        case AccountsField::PwNew: return app.accountsPwNew;
        case AccountsField::PwConfirm: return app.accountsPwConfirm;
        case AccountsField::CuUsername: return app.accountsCuUsername;
        case AccountsField::CuFullName: return app.accountsCuFullName;
        case AccountsField::CuPassword: return app.accountsCuPassword;
        case AccountsField::CuConfirm: return app.accountsCuConfirm;
        case AccountsField::Hostname: return app.accountsHostnameEdit;
        default: return app.accountsPwNew;
      }
    }();
    if (ev.sym == XKB_KEY_Return || ev.sym == XKB_KEY_KP_Enter || ev.sym == XKB_KEY_Escape) {
      app.accountsActiveField = AccountsField::None;
      draw(app);
      return;
    }
    if (ev.sym == XKB_KEY_Tab) {
      int next = static_cast<int>(app.accountsActiveField) + 1;
      if (next > static_cast<int>(AccountsField::Hostname)) next = 1;
      app.accountsActiveField = static_cast<AccountsField>(next);
      draw(app);
      return;
    }
    if (ev.sym == XKB_KEY_BackSpace) {
      if (!buf.empty()) buf.pop_back();
      draw(app);
      return;
    }
    if (ev.utf8_len > 0) {
      for (int i = 0; i < ev.utf8_len; ++i) {
        char c = ev.utf8[i];
        if (c >= 32 && c < 127) buf += c;
      }
      draw(app);
    }
    return;
  }

  // Workspace animation speed text entry
  if (app.hyprlandAnimSpeedEditActive && app.activeTab == 33 &&
      app.hyprlandAnimEditIdx >= 0) {
    if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (ev.sym == XKB_KEY_Return || ev.sym == XKB_KEY_KP_Enter) {
      double val = std::strtod(app.hyprlandAnimSpeedEditBuf.c_str(), nullptr);
      if (val > 0.0 && val <= 30.0) {
        app.hyprlandConfig.animations.entries[app.hyprlandAnimEditIdx].speed = val;
        hyprland_commit_cfg(app);
      }
      app.hyprlandAnimSpeedEditActive = false;
      draw(app);
      return;
    }
    if (ev.sym == XKB_KEY_Escape) {
      app.hyprlandAnimSpeedEditActive = false;
      draw(app);
      return;
    }
    if (ev.sym == XKB_KEY_BackSpace) {
      if (!app.hyprlandAnimSpeedEditBuf.empty())
        app.hyprlandAnimSpeedEditBuf.pop_back();
      draw(app);
      return;
    }
    if (ev.sym == XKB_KEY_minus && app.hyprlandAnimSpeedEditBuf.empty()) {
      app.hyprlandAnimSpeedEditBuf += '-';
      draw(app);
      return;
    }
    if (ev.utf8_len > 0) {
      char c = ev.utf8[0];
      if ((c >= '0' && c <= '9') || c == '.') {
        if (c == '.' && app.hyprlandAnimSpeedEditBuf.find('.') != std::string::npos)
          return;
        app.hyprlandAnimSpeedEditBuf += c;
        draw(app);
      }
    }
    return;
  }

  // Autostart tab text entry
  if (app.activeTab == 48 && app.autostartActiveField != AutostartField::None) {
    if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    settings_autostart_consume_key(app, ev.sym, ev.state, ev.utf8.data(), ev.utf8_len);
    return;
  }

  if (!app.widgetPickerOpen) return;
  if (ev.state != WL_KEYBOARD_KEY_STATE_PRESSED && ev.state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  xkb_state* xkb = app.seat.xkb_state_ptr();
  const bool ctrl = xkb && xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE);
  const xkb_keysym_t sym = ev.sym;

  if (sym == XKB_KEY_Escape) {
    eh::settings::widget_picker::close_picker(app);
    draw(app);
    return;
  }
  if (!app.widgetPickerSearchActive) return;

  if (ctrl && app.clipboard.is_available()) {
    if (sym == XKB_KEY_c || sym == XKB_KEY_C) {
      (void)app.clipboard.copy_text(app.widgetPickerFilter);
      return;
    }
    if ((sym == XKB_KEY_v || sym == XKB_KEY_V) && app.wl.display()) {
      std::string pasted = app.clipboard.read_selection_text(app.wl.display());
      pasted.erase(std::remove(pasted.begin(), pasted.end(), '\r'), pasted.end());
      pasted.erase(std::remove(pasted.begin(), pasted.end(), '\n'), pasted.end());
      app.widgetPickerFilter += std::move(pasted);
      app.widgetPickerHoverSlot = -1;
      app.widgetPickerScrollPx = 0;
      draw(app);
      return;
    }
  }

  if (sym == XKB_KEY_BackSpace) {
    while (!app.widgetPickerFilter.empty() && (app.widgetPickerFilter.back() & 0xC0) == 0x80) app.widgetPickerFilter.pop_back();
    if (!app.widgetPickerFilter.empty()) app.widgetPickerFilter.pop_back();
    app.widgetPickerHoverSlot = -1;
    app.widgetPickerScrollPx = 0;
    draw(app);
    return;
  }
  if (ev.utf8_len > 0) {
    app.widgetPickerFilter.append(ev.utf8.data(), static_cast<size_t>(ev.utf8_len));
    app.widgetPickerHoverSlot = -1;
    app.widgetPickerScrollPx = 0;
    draw(app);
  }
}

// Embed dispatch thunks.
namespace eh::settings {

extern std::unique_ptr<App> g_embed;

void embed_dispatch_pointer_motion(wl_surface* localSurface, double sx, double sy) {
  if (!g_embed || !g_embed->surface) return;
  on_pointer_motion(*g_embed, localSurface, sx, sy);
}

void embed_dispatch_pointer_button(wl_surface* localSurface, std::uint32_t button, std::uint32_t state,
                                   std::uint32_t pointer_serial) {
  if (!g_embed || !g_embed->surface) return;
  on_pointer_button(*g_embed, localSurface, button, state, pointer_serial);
}

void embed_dispatch_pointer_axis_vertical(double delta_px) {
  if (!g_embed || !g_embed->surface || !embed_window_visible()) return;
  settings_apply_wheel_scroll_delta(*g_embed, delta_px);
}

void embed_dispatch_pointer_leave() {
  if (!g_embed || !g_embed->surface) return;
  on_pointer_leave(*g_embed);
}

void embed_dispatch_widget_picker_pointer_leave() {
  if (!g_embed || !g_embed->widgetPickerOpen) return;
  if (g_embed->widgetPickerHoverSlot < 0) return;
  g_embed->widgetPickerHoverSlot = -1;
  draw(*g_embed);
}

void embed_dispatch_default_app_picker_pointer_leave() {
  if (!g_embed || !g_embed->defaultAppPickerOpen) return;
  if (g_embed->defaultAppPickerPopupHoverIdx < 0) return;
  g_embed->defaultAppPickerPopupHoverIdx = -1;
  draw(*g_embed);
}

bool embed_try_keyboard(std::uint32_t keycode, std::uint32_t state, xkb_state* xkb) {
  if (!g_embed || !g_embed->surface || !xkb) return false;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return false;

  // Close shortcuts: Alt+F4 or Super+Q — always honoured when settings is visible
  const xkb_keysym_t close_sym = xkb_state_key_get_one_sym(xkb, keycode + 8);
  if ((close_sym == XKB_KEY_F4 &&
       xkb_state_mod_name_is_active(xkb, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE)) ||
      ((close_sym == XKB_KEY_q || close_sym == XKB_KEY_Q) &&
       xkb_state_mod_name_is_active(xkb, "Mod4", XKB_STATE_MODS_EFFECTIVE))) {
    embed_toggle();
    return true;
  }

  if (!g_embed->widgetPickerOpen && !wifi_password_prompt_visible(*g_embed) && !keyring_prompt_visible(*g_embed)) return false;

  eh::wayland::WaylandSeat::KeyboardEvent ev{};
  ev.keycode = keycode;
  ev.state = state;
  ev.sym = xkb_state_key_get_one_sym(xkb, keycode + 8);
  ev.utf8_len = xkb_state_key_get_utf8(xkb, keycode + 8, ev.utf8.data(), ev.utf8.size() - 1);
  if (ev.utf8_len < 0) ev.utf8_len = 0;
  ev.utf8[static_cast<size_t>(ev.utf8_len)] = '\0';

  on_settings_keyboard(*g_embed, ev);
  return true;
}

} // namespace eh::settings
