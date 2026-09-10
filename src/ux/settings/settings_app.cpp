#include <cairo/cairo.h>
#include <wayland-client.h>

#include <ctime>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/common/palette/matugen_external_templates.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"
#include "xdg-shell-client-protocol.h"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/buffer/cairo_cpu_buffer.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/data/default_apps/settings_default_apps.hpp"
#include "ux/settings/settings_tab_default_apps/settings_tab_default_apps.hpp"
#include "ux/settings/settings_tab_ui_layout/settings_tab_layout.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "ux/settings/utils/widget_picker/widget_picker.hpp"
#include "ux/settings/settings_tab_desktop_widgets/world_clock_popup.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/common/asset/asset_loader.hpp"
#include "ux/settings/utils/monitors/settings_monitors_tab.hpp"
#include "services/audio/pipewire_service.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"
#include "services/bing/bing_wallpaper.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/settings_tab_workspaces/settings_tab_workspaces.hpp"
#include "ux/settings/settings_tab_bing/settings_tab_bing.hpp"
#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"
#include "ux/settings/settings_tab_sound/settings_tab_sound.hpp"
#include "ux/settings/settings_tab_notifications/settings_tab_notifications.hpp"
#include "ux/settings/settings_tab_appearance/settings_tab_appearance.hpp"
#include "ux/settings/settings_tab_icons/settings_tab_icons.hpp"
#include "ux/settings/settings_tab_themes/settings_tab_themes.hpp"
#include "ux/settings/settings_tab_color_themes/settings_tab_color_themes.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "ux/settings/settings_tab_monitors/settings_tab_monitors.hpp"
#include "ux/settings/settings_tab_network/settings_tab_network.hpp"
#include "ux/settings/settings_tab_network/dialogs/wifi_password_prompt.hpp"
#include "ux/settings/settings_tab_network/dialogs/keyring_password_prompt.hpp"
#include "ux/settings/settings_tab_network/dialogs/vpn_add_dialog.hpp"
#include "ux/settings/settings_tab_nightlight/settings_tab_nightlight.hpp"
#include "ux/settings/settings_tab_desktop_widgets/settings_tab_desktop_widgets.hpp"
#include "ux/settings/settings_tab_desktop/settings_tab_desktop.hpp"
#include "ux/settings/settings_tab_mango/settings_tab_mango.hpp"
#include "ux/settings/settings_tab_hyprland/settings_tab_hyprland.hpp"
#include "ux/settings/settings_tab_time/settings_tab_time.hpp"
#include "ux/settings/settings_tab_keyboard/settings_tab_keyboard.hpp"
#include "ux/settings/settings_tab_power/settings_tab_power.hpp"
#include "ux/settings/settings_tab_bluetooth/settings_tab_bluetooth.hpp"
#include "ux/settings/settings_tab_accounts/settings_tab_accounts.hpp"
#include "ux/settings/settings_tab_autostart/settings_tab_autostart.hpp"
#include "ux/settings/settings_tab_ui_layout/settings_tab_layout.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/common/logo/settings_logo.hpp"
#include "ux/settings/utils/gpu/settings_gpu.hpp"
#include "ux/settings/utils/sound/settings_sound_cache.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"

static uint64_t g_settings_content_draw_count = 0;

static void settings_sync_draw_chrome(App& app, const eh::config::ShellAppearance& ap) {
    
  // Config snapshot may skip palette generation for perf; use cached palette from app.settings
  const bool haveCached = app.settings.matugenThemingEnabled && app.settings.matugenPaletteOk;
  const bool haveHcCached = app.settings.horizonColorsNative && app.settings.horizonColorsPaletteOk;
  app.drawChromeMatugen = (ap.matugenThemingEnabled && ap.matugenPaletteOk) || haveCached
                       || (ap.horizonColorsNative && ap.horizonColorsPaletteOk) || haveHcCached
                       || ap.customThemeEnabled;
  app.drawChrome = eh::config::derived_chrome_colors(ap);
  // Override drawChrome with cached dynamic colors when snapshot skipped them
  if (haveCached && !(ap.matugenThemingEnabled && ap.matugenPaletteOk)) {
    app.drawChrome.panelFillR = static_cast<double>(app.settings.matugenSurfaceR);
    app.drawChrome.panelFillG = static_cast<double>(app.settings.matugenSurfaceG);
    app.drawChrome.panelFillB = static_cast<double>(app.settings.matugenSurfaceB);
    app.drawChrome.dockFillR = static_cast<double>(app.settings.matugenSurfaceR);
    app.drawChrome.dockFillG = static_cast<double>(app.settings.matugenSurfaceG);
    app.drawChrome.dockFillB = static_cast<double>(app.settings.matugenSurfaceB);
    app.drawChrome.accentR = static_cast<double>(app.settings.matugenAccentR);
    app.drawChrome.accentG = static_cast<double>(app.settings.matugenAccentG);
    app.drawChrome.accentB = static_cast<double>(app.settings.matugenAccentB);
    app.drawChrome.outlineR = static_cast<double>(app.settings.matugenOutlineR);
    app.drawChrome.outlineG = static_cast<double>(app.settings.matugenOutlineG);
    app.drawChrome.outlineB = static_cast<double>(app.settings.matugenOutlineB);
  }
  if (haveHcCached && !(ap.horizonColorsNative && ap.horizonColorsPaletteOk)) {
    app.drawChrome.panelFillR = static_cast<double>(app.settings.hcSurfaceR);
    app.drawChrome.panelFillG = static_cast<double>(app.settings.hcSurfaceG);
    app.drawChrome.panelFillB = static_cast<double>(app.settings.hcSurfaceB);
    app.drawChrome.dockFillR = static_cast<double>(app.settings.hcSurfaceR);
    app.drawChrome.dockFillG = static_cast<double>(app.settings.hcSurfaceG);
    app.drawChrome.dockFillB = static_cast<double>(app.settings.hcSurfaceB);
    app.drawChrome.accentR = static_cast<double>(app.settings.hcAccentR);
    app.drawChrome.accentG = static_cast<double>(app.settings.hcAccentG);
    app.drawChrome.accentB = static_cast<double>(app.settings.hcAccentB);
    app.drawChrome.outlineR = static_cast<double>(app.settings.hcOutlineR);
    app.drawChrome.outlineG = static_cast<double>(app.settings.hcOutlineG);
    app.drawChrome.outlineB = static_cast<double>(app.settings.hcOutlineB);
  }
  // Text is never from the color engine: white in dark mode, black in light mode.
  if (app.drawChromeMatugen) {
    const bool isLight = (ap.matugenMode == "light");
    app.drawChrome.textR = isLight ? 0.0 : 1.0;
    app.drawChrome.textG = isLight ? 0.0 : 1.0;
    app.drawChrome.textB = isLight ? 0.0 : 1.0;
    eh::ui::set_global_accent(static_cast<float>(app.drawChrome.accentR),
                                static_cast<float>(app.drawChrome.accentG),
                                static_cast<float>(app.drawChrome.accentB));
    eh::ui::set_global_surface(static_cast<float>(app.drawChrome.dockFillR),
                                 static_cast<float>(app.drawChrome.dockFillG),
                                 static_cast<float>(app.drawChrome.dockFillB));
    eh::ui::set_global_outline(static_cast<float>(app.drawChrome.outlineR),
                                 static_cast<float>(app.drawChrome.outlineG),
                                 static_cast<float>(app.drawChrome.outlineB));
    eh::ui::set_global_text(static_cast<float>(app.drawChrome.textR),
                              static_cast<float>(app.drawChrome.textG),
                              static_cast<float>(app.drawChrome.textB));
  }
}

extern const double kEmbedSlidePx;

static const char* const kMangoSubs[] = {"Decoration", "Colors", "Animations", "Keybinds", "Layout", "Input", "Misc"};
static const char* const kMangoSubGlyphs[] = {"border_style", "palette", "play_arrow", "keyboard", "grid_view", "mouse", "tune"};
static const int kMangoTabIds[] = {20, 21, 22, 23, 24, 25, 26};



#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
App::App() : settings(load_settings()) {
   
#pragma GCC diagnostic pop
  monitorsTab.kind = detect_compositor_kind();
  sidebarExpanded.insert("MangoWM");
  sidebarExpanded.insert("Panels & UI");
  sidebarExpanded.insert("Display");
  sidebarExpanded.insert("System");
  sidebarExpanded.insert("Wallpaper Settings");

  const std::time_t now = std::time(nullptr);
  const struct tm t = *std::localtime(&now);
  bingSelectedYear  = t.tm_year + 1900;
  bingSelectedMonth = t.tm_mon + 1;
}


bool eh_settings_bench() { return eh::settings::trace::bench(); }

bool eh_settings_embed_post_open_roundtrip() {
   
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  const char* e = std::getenv("EH_SETTINGS_EMBED_SYNC");
  cached = (e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y')) ? 1 : 0;
  return cached != 0;
}

using SettingsBenchClock = std::chrono::steady_clock;

static inline int64_t settings_bench_us(SettingsBenchClock::time_point a, SettingsBenchClock::time_point b) {
   
  return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

unsigned s_settings_bench_session = 0;
unsigned s_settings_bench_draw_n = 0;
SettingsBenchClock::time_point s_settings_bench_t0{};


bool eh_wallpaper_thumb_debug() { return eh::wallpaper::wallpaper_thumbnail_pipeline_debug_enabled(); }

static bool settings_slider_drag_pending(const App& app) noexcept {
   
  return app.sliderDrag >= 0 || app.wsSliderDrag >= 0 || app.launcherSliderDrag >= 0 ||
         app.mangoSliderDrag >= 0 ||
          app.notifSliderDrag >= 0 ||
         app.timeSliderDrag >= 0 ||
         app.draggingPanelTopGap || app.draggingPanelHeight || app.draggingPanelRadius || app.draggingPanelOpacity ||
         app.appearanceOverlaySliderDrag >= 0 || app.soundVolDragCode >= 0;
}

void draw(App& app);






#include "ux/settings/utils/wallpaper/settings_wallpaper_thumbs.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

[[nodiscard]] static bool ensure_settings_shm_pair(App& app) {
  wl_shm* shm = app.wl.shm();
  if (!shm) {
    debug_log("settings", "ERROR ensure_shm_pair: no wl_shm");
    return false;
  }
  if (!app.buf[0].ensure(shm, eh::shell::kSettingsShmPoolA, app.width, app.height)) {
    debug_log("settings", "ERROR ensure_shm_pair: buf[0] ensure failed %dx%d", app.width, app.height);
    return false;
  }
  if (!app.buf[1].ensure(shm, eh::shell::kSettingsShmPoolB, app.width, app.height)) {
    debug_log("settings", "ERROR ensure_shm_pair: buf[1] ensure failed %dx%d, destroying buf[0]", app.width, app.height);
    app.buf[0].destroy();
    return false;
  }
  return true;
}

[[nodiscard]] static int pick_settings_paint_buffer(App& app) {
  if (!app.buf[0].busy()) return 0;
  if (!app.buf[1].busy()) return 1;
  return -1;
}

static constexpr int kSidebarSubTabIndentX = 28;
static constexpr int kSidebarSubTabTextOffsetX = 12;

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
static const int kPanelsAndUITabIds[] = {0, 11, 9, 29, 27};
static const char* const kDisplaySubs[] = {"Monitors", "Appearance", "Themes", "Color Themes", "Icons", "UI Layout", "Nightlight"};
static const char* const kDisplaySubGlyphs[] = {"monitor", "palette", "palette", "colorize", "photo_library", "view_quilt", "dark_mode"};
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

static int compute_sidebar_total_height(const App& app) {
   
  int h = kSidebarTabBaseY;
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    h += kSidebarTabPitchY;
    if (def.subCount > 0 && app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end())
      h += def.subCount * 36;
  }
  return h;
}

void settings_clamp_sidebar_scroll_px(App& app) {
   
  const int totalH = compute_sidebar_total_height(app);
  const int viewH = app.height - kContentTop - kSpacingL;
  const int maxScroll = std::max(0, totalH - viewH);
  app.sidebarScrollPx = std::clamp(app.sidebarScrollPx, 0, maxScroll);
}

static constexpr int kWidgetPickerSettingsContentInsetX = kSpacingL + kSidebarW + kSpacingL;

static constexpr int kWallpaperFolderPickerRowCount = 4;

static const char* wallpaper_folder_picker_row_label(int i) {
   
  static const char* const kLab[] = {"Native dialog", "KDE — kdialog", "Qt — qarma", "GTK — zenity"};
  if (i < 0 || i >= kWallpaperFolderPickerRowCount) return "";
  return kLab[i];
}

static void wallpaper_folder_picker_modal_geom(const App& app, int* outPx, int* outPy, int* outPw, int* outPh,
                                                 int* outRowPitch, int* outN) {
   
  *outN = kWallpaperFolderPickerRowCount;
  *outRowPitch = 34;
  *outPw = 540;
  *outPh = 56 + *outN * *outRowPitch + 18;
  *outPx = std::max(20, (app.width - *outPw) / 2);
  *outPy = std::max(20, (app.height - *outPh) / 2);
}



static void settings_surface_frame_done(void* data, wl_callback* cb, uint32_t time_ms) {
  wl_callback_destroy(cb);
  auto& app = *static_cast<App*>(data);
  app.surfaceFrameCb = nullptr;
  if (!app.surface) {
    debug_log("settings", "WARN frame_done: no surface");
    return;
  }
  {
    static uint64_t s_frame_seq = 0;
    static float s_prev_embed_t = -1.f;
    static int s_stall_count = 0;
    ++s_frame_seq;
    if (app.embedded) {
      if (app.embedPresentT == s_prev_embed_t) {
        ++s_stall_count;
        if (s_stall_count >= 30) {
          debug_log("settings", "WARN frame_done: embedPresentT STALLED at %.3f for %d frames (seq=%llu)",
                    app.embedPresentT, s_stall_count, (unsigned long long)s_frame_seq);
        }
      } else {
        s_stall_count = 0;
      }
      s_prev_embed_t = app.embedPresentT;
    }
  }
  debug_log("settings", "frame_done: t=%u embed=tick embedPresentT=%.3f", time_ms, app.embedPresentT);
  if (app.embedded) app.embedAnim.tick();
  app.wallpaperHoverAnim.tick();
  app.settingsScroll.tick();
  const bool coalescedWidgetDrag = app.settingsWidgetDragRepaintQueued;
  if (coalescedWidgetDrag) app.settingsWidgetDragRepaintQueued = false;
  const bool needThumb = wallpaper_thumb_needs_followup_frame(app);
  const bool needAnim = app.embedded && app.embedAnim.has_active();
  const bool needWallpaperHoverAnim = app.wallpaperHoverAnim.has_active();
  const bool needScrollAnim = app.settingsScroll.animating();
  const bool needTogAnim = app.wifiToggle.animating();
  const bool hasContentChange = needThumb || needAnim || needWallpaperHoverAnim || coalescedWidgetDrag || needTogAnim || app.settingsScrollNeedsRedraw;
  const bool needPendingRedraw = app.pendingRedraw;
  const bool redrew = hasContentChange || needScrollAnim || needPendingRedraw;
  if (redrew) {
    debug_log("settings", "frame_done: redraw needAnim=%d needScroll=%d needPendingRedraw=%d embedPresentT=%.3f",
              needAnim, needScrollAnim, needPendingRedraw, app.embedPresentT);
    app.settingsScrollNeedsRedraw = false;
    if (hasContentChange) app.tabContentDirty = true;
    app.pendingRedraw = false;
    draw(app);
  }
}

void schedule_settings_surface_frame(App& app) {
    
  if (!app.surface) return;
  if (app.surfaceFrameCb) return;
  const bool needThumb = wallpaper_thumb_needs_followup_frame(app);
  const bool needAnim = app.embedded && app.embedAnim.has_active();
  const bool needWallpaperHoverAnim = app.wallpaperHoverAnim.has_active();
  const bool needScrollAnim = app.settingsScroll.animating();
  const bool needCoalescedWidgetDrag = app.settingsWidgetDragRepaintQueued;
  const bool needTogAnim = app.wifiToggle.animating();
  const bool needPendingRedraw = app.pendingRedraw;
  if (!needThumb && !needAnim && !needWallpaperHoverAnim && !needScrollAnim && !needCoalescedWidgetDrag && !needTogAnim && !needPendingRedraw && !app.settingsScrollNeedsRedraw) {
    debug_log("settings", "schedule_frame: nothing pending, skip");
    return;
  }
  debug_log("settings", "schedule_frame: needAnim=%d needScroll=%d pendingRedraw=%d embedPresentT=%.3f",
            needAnim, needScrollAnim, needPendingRedraw, app.embedPresentT);
  static const wl_callback_listener kListener = {.done = settings_surface_frame_done};
  app.surfaceFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.surfaceFrameCb, &kListener, &app);
}





#include "settings/settings_tab_monitors/settings_monitors_ui.inl"

static void settings_paint_monitors_dropdown_unclipped(App& app, cairo_t* cr, int contentX, int contentW,
                                                       double glassOv) {
   
  if (app.monitorsActiveDd < 0 || app.monitorsTab.outputs.empty()) return;

  eh::settings_monitors_tab::MonitorsTabLayout monLay{};
  eh::settings_monitors_tab::compute_monitors_tab_layout(static_cast<int>(contentX), contentW, kContentTop,
                                                         settings_content_viewport_h(app), &monLay);
  monitors_clamp_selected(app);
  const auto& srow = app.monitorsTab.outputs[static_cast<size_t>(app.monitorsSelectedIdx)];
  auto scit = app.monitorsTab.caps.find(srow.name);
  const eh::settings_monitors::OutputCaps* scaps = scit != app.monitorsTab.caps.end() ? &scit->second : nullptr;
  const MonitorsFormRows fr = monitors_form_rows(app, srow, scaps);
  const MonitorsFormGeom fg = monitors_form_layout(monLay, static_cast<int>(contentX), contentW, fr);

  int dcx = 0, dcy = 0, dcw = 0, dch = 0;
  if (!monitors_dd_combo_geom(fr, fg, app.monitorsActiveDd, &dcx, &dcy, &dcw, &dch)) return;

  static std::vector<std::string> monDdStorage;
  static std::vector<const char*> monDdPtrs;
  monDdStorage.clear();
  monDdPtrs.clear();
  int nrows = 0;
  int selIx = 0;

  if (app.monitorsActiveDd == 0 && scaps) {
    nrows = static_cast<int>(scaps->resolutions.size());
    for (int i = 0; i < nrows; ++i) monDdStorage.push_back(scaps->resolutions[static_cast<size_t>(i)]);
    for (size_t i = 0; i < scaps->resolutions.size(); ++i) {
      if (scaps->resolutions[i] == srow.resolution) selIx = static_cast<int>(i);
    }
  } else if (app.monitorsActiveDd == 1 && scaps) {
    auto it = scaps->resolution_refresh_hz.find(srow.resolution);
    if (it != scaps->resolution_refresh_hz.end()) {
      nrows = static_cast<int>(it->second.size());
      for (double hz : it->second) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g Hz", hz);
        monDdStorage.emplace_back(buf);
      }
      const double cur = std::strtod(srow.refresh_rate.c_str(), nullptr);
      double bestD = 1e300;
      for (int i = 0; i < nrows; ++i) {
        double d = std::abs(it->second[static_cast<size_t>(i)] - cur);
        if (d < bestD) {
          bestD = d;
          selIx = i;
        }
      }
    }
  } else if (app.monitorsActiveDd == 2) {
    nrows = 8;
    for (int i = 0; i < 8; ++i) monDdStorage.emplace_back(kMonitorTfLabels[static_cast<size_t>(i)]);
    selIx = std::clamp(std::atoi(srow.transform.c_str()), 0, 7);
  } else if (app.monitorsActiveDd == 3) {
    nrows = 2;
    monDdStorage.emplace_back(kMonitorBitdepthDdLabels[0]);
    monDdStorage.emplace_back(kMonitorBitdepthDdLabels[1]);
    selIx = (srow.bitdepth == "10") ? 1 : 0;
  } else if (app.monitorsActiveDd == 4) {
    if (app.monitorsTab.kind == CompositorKind::Mango) {
      nrows = 2;
      monDdStorage.emplace_back("Off");
      monDdStorage.emplace_back("On");
    } else {
      nrows = 3;
      monDdStorage.emplace_back("Off");
      monDdStorage.emplace_back("On");
      monDdStorage.emplace_back("On-demand");
    }
    selIx = std::clamp(srow.vrr.empty() ? 0 : std::atoi(srow.vrr.c_str()), 0, nrows - 1);
  } else if (app.monitorsActiveDd == 5) {
    nrows = kMonitorCmCount;
    for (int i = 0; i < kMonitorCmCount; ++i) monDdStorage.emplace_back(kMonitorCmLabels[i]);
    selIx = monitors_cm_index(srow.cm);
  } else if (app.monitorsActiveDd == 6) {
    nrows = 3;
    for (int i = 0; i < 3; ++i) monDdStorage.emplace_back(kMonSupportsHdrDdLabels[static_cast<size_t>(i)]);
    selIx = monitors_tri_auto_field_sel(srow.supports_hdr);
  } else if (app.monitorsActiveDd == 7) {
    nrows = 3;
    for (int i = 0; i < 3; ++i) monDdStorage.emplace_back(kMonSupportsWideDdLabels[static_cast<size_t>(i)]);
    selIx = monitors_tri_auto_field_sel(srow.supports_wide_color);
  } else if (app.monitorsActiveDd == 8) {
    nrows = 3;
    for (int i = 0; i < 3; ++i) monDdStorage.emplace_back(kMonSdrEotfDdLabels[static_cast<size_t>(i)]);
    selIx = monitors_sdr_eotf_sel(srow.sdr_eotf);
  }

  for (const auto& s : monDdStorage) monDdPtrs.push_back(s.c_str());
  if (nrows <= 0 || static_cast<int>(monDdPtrs.size()) != nrows) return;

  const int list_doc_top =
      monitors_dd_popup_list_doc_top_y(dcy, dch, nrows, settings_scroll_px_int(app), app.height);
  const int list_device_y = list_doc_top - settings_scroll_px_int(app);
  settings_paint_combo_list_popup(app, cr, dcx, list_device_y, dcw, kSettingsDdRowH, nrows, monDdPtrs.data(), selIx,
                                  app.monitorsDdHoverRow, glassOv, 0, 0, true);
}

extern const char* const kWidthModeLabels[];

static void settings_paint_mode_dropdown_popups(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {

  if (app.activeTab == 0 && !dock_m3_is_widgets_child_tab() &&
      !dock_m3_is_appearance_child_tab()) {
    dock_renderer_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.rendererDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.activeTab == 2 && app.appearanceChildTab == kAppearanceGeneral) {
    matugen_scheme_dd_sync(app, contentX, contentW);
    matugen_mode_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.matugenSchemeDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
    app.matugenModeDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.taskbarWidthModeDropdownOpen && app.activeTab == 11) {
    const int kVisCardTopM3 = kContentTop + kDockChildTabH + 12;
    const int cbx = contentX + 8 + contentW - 16 - kCardPad - kSettingsComboW;
    const int cby = kVisCardTopM3 + 52 + 1 * kDockVisRowPitch + (kDockVisRowPitch - kSettingsComboH) / 2;
    const int ly = cby + kSettingsComboH + 2 - settings_scroll_px_int(app);
    settings_paint_combo_list_popup(app, cr, cbx, ly, kSettingsComboW, kSettingsDdRowH, 3, kWidthModeLabels,
                                    std::clamp(app.settings.taskbarWidthMode, 0, 2),
                                    app.taskbarWidthModeDropdownHoverRow, glassOv);
  }

  // Qt color scheme dropdown (themes tab, QT sub-tab)
  if (app.qtColorSchemeDropdownOpen && app.activeTab == 45) {
    const auto qtSchemes = eh::theming::list_qt_color_schemes();
    const int nSchemes = static_cast<int>(qtSchemes.size());
    const int scr = settings_scroll_px_int(app);
    const int subTab = app.themesSubTab;
    if (subTab == 1 && nSchemes > 0) {
      // rebuild grid bottom
      const int cols = std::max(1, (contentW - 32) / 240);
      const int qtCount = static_cast<int>(eh::theming::known_qt_styles().size());
      const int qtRows = (qtCount + cols - 1) / cols;
      const int gridTop = kContentTop + kTabBarH + kSpacingL + 60;
      const int gridBottom = gridTop + qtRows * (150 + 8) - 8;
      int cbx, cby, cbw, cbh;
      // Use the same geometry helper from themes tab - declare locally
      auto qcs_combo_geom = [&](int& x, int& y, int& w, int& h) {
        x = contentX + 20 + 140;
        w = contentW - 20 - 140 - 28;
        if (w < 100) w = 100;
        y = gridBottom + 12;
        h = 38;
      };
      qcs_combo_geom(cbx, cby, cbw, cbh);
      const int ly = cby + cbh + 2 - scr;

      // Build labels vector
      std::vector<const char*> labels;
      labels.reserve(static_cast<size_t>(nSchemes) + 1);
      labels.push_back("None");
      for (const auto& s : qtSchemes) labels.push_back(s.c_str());

      const std::string& currentScheme = get_pending_qt_color_scheme();
      int selIx = 0;
      for (int i = 0; i < nSchemes; ++i) {
        if (qtSchemes[static_cast<size_t>(i)] == currentScheme) { selIx = i + 1; break; }
      }

      settings_paint_combo_list_popup(app, cr, cbx, ly, cbw, kSettingsDdRowH, nSchemes + 1, labels.data(),
                                      selIx, app.qtColorSchemeDropdownHoverRow, glassOv);
    }
  }
}

void draw(App& app) {
  if (!app.surface) {
    debug_log("settings", "WARN draw: no surface, skipping");
    return;
  }
  eh::settings::widget_picker::destroy_caret_frame(app);
  {
    const int b0 = app.buf[0].busy();
    const int b1 = app.buf[1].busy();
    debug_log("settings", "draw: ENTER tab=%d %dx%d pickerOpen=%d bufs=[%d,%d] pendingRedraw=%d embedPresentT=%.3f",
              app.activeTab, app.width, app.height, app.widgetPickerOpen ? 1 : 0, b0, b1,
              app.pendingRedraw ? 1 : 0, app.embedPresentT);
  }
  if (app.activeTab > 48 && app.activeTab != 50) app.activeTab = 44;
  if (app.activeTab != 10) app.defaultAppsPillRectValid = false;
  if (app.activeTab == 7 && !app.monitorsTabDidInitialRefresh) {
    app.monitorsTab.refresh_from_system();
    app.monitorsTabDidInitialRefresh = true;
  }
  if (app.activeTab == 8) eh::audio::PipeWireService::instance().start();
  if (app.activeTab >= 20 && app.activeTab <= 26) {
    if (!app.monitorsTabDidInitialRefresh) {
      app.monitorsTab.refresh_from_system();
      app.monitorsTabDidInitialRefresh = true;
    }
    if (app.mangoConfigLines.empty()) {
      auto mcf = eh::settings_mango::read_mango_config();
      app.mangoConfig = mcf.cfg;
      app.mangoConfigLines = mcf.lines;
    }
  }
  if (app.activeTab == 33) {
    if (!app.monitorsTabDidInitialRefresh) {
      app.monitorsTab.refresh_from_system();
      app.monitorsTabDidInitialRefresh = true;
    }
    if (!hyprland_m3_has_active_slider(app, app.hyprlandChildTab))
      app.hyprlandConfig = eh::settings_hyprland::read_config();
  }
  if (app.activeTab == 17 || app.activeTab == 18) {
    auto& nm = eh::net::NetworkManagerService::instance();
    nm.start();
    static bool nmCbRegistered = false;
    if (!nmCbRegistered) {
      nmCbRegistered = true;
      nm.setChangeCallback([&app](const eh::net::Snapshot&) {
        debug_log("settings", "nm callback: settingsDeferRedraw");
        app.settingsDeferRedraw = true;
      });
    }
    static auto lastNRefresh = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - lastNRefresh >= std::chrono::seconds{1}) {
      lastNRefresh = now;
      nm.refresh();
    }
  }
  if (app.activeTab == 47) {
    auto& bt = eh::bt::BluezService::instance();
    bt.start();
    static bool btCbRegistered = false;
    if (!btCbRegistered) {
      btCbRegistered = true;
      bt.set_change_callback([&app]() {
        debug_log("settings", "bt callback: settingsDeferRedraw");
        app.settingsDeferRedraw = true;
      });
    }

  }
  
  if (app.defaultAppPickerLayerWlSurface || app.defaultAppPickerLayer || app.defaultAppPickerXdgWlSurface)
    default_app_picker_teardown_layer(app);
  const SettingsBenchClock::time_point t_draw_enter = SettingsBenchClock::now();

  const bool monitors_live_drag =
      app.activeTab == 7 && app.pointerLeftDown &&
      (app.monitorsCanvasPanArmed || app.monitorsCanvasDragIdx >= 0 || app.monitorsScaleSliderDragIdx >= 0 ||
       app.monitorsHyprExtraSlider >= 0);
  const bool reuse_paint_shell_snapshot =
      app.settings_paint_shell_snapshot_valid &&
      (settings_slider_drag_pending(app) || app.widgetDragging || monitors_live_drag ||
       !app.pointerLeftDown);
  const eh::config::ShellConfig draw_sc = [&]() -> eh::config::ShellConfig {
      if (reuse_paint_shell_snapshot) return app.settings_paint_shell_snapshot;
    eh::config::ShellConfig sc = eh::config::shell_config_snapshot_skip_matugen();
    app.settings_paint_shell_snapshot = sc;
    app.settings_paint_shell_snapshot_valid = true;
    return sc;
  }();
  const SettingsBenchClock::time_point t_after_shell_snapshot = SettingsBenchClock::now();
  const bool force_wallpaper_vk = app.activeTab == 6;
  if (!force_wallpaper_vk) settings_sync_renderer_backend_state(app, draw_sc);
  const SettingsBenchClock::time_point t_after_sync_renderer = SettingsBenchClock::now();

  const bool sliderDrag = settings_slider_drag_pending(app);
  const int mergedThumbs =
      (monitors_live_drag || sliderDrag) ? 0 : wallpaper_merge_async_thumbnails(app);
  const SettingsBenchClock::time_point t_after_thumb_merge = SettingsBenchClock::now();

  bool want_vk = settings_want_vk(app, draw_sc);
  if (force_wallpaper_vk) want_vk = true;
  const SettingsBenchClock::time_point t_after_want_vk = SettingsBenchClock::now();
  bool vk_path = false;
  if (want_vk && settings_ensure_vk_raster(app, app.width, app.height)) vk_path = true;
  const bool gpu_path = vk_path;
  debug_log("settings", "draw: want_vk=%d gpu_path=%d embedPresentT=%.3f", want_vk, gpu_path, app.embedPresentT);

  int paintBi = -1;
  if (!gpu_path) {
    if (!ensure_settings_shm_pair(app)) {
      if (app.wl.display()) (void)wl_display_flush(app.wl.display());
      return;
    }
    paintBi = pick_settings_paint_buffer(app);
    if (paintBi < 0) {
      debug_log("settings", "draw: DEFERRED both buf busy embedPresentT=%.3f", app.embedPresentT);
      app.pendingRedraw = true;
      schedule_settings_surface_frame(app);
      if (eh_settings_bench()) {
        ++s_settings_bench_draw_n;
        const int64_t us_snap = settings_bench_us(t_draw_enter, t_after_shell_snapshot);
        const int64_t us_sync = settings_bench_us(t_after_shell_snapshot, t_after_sync_renderer);
        const int64_t us_th = settings_bench_us(t_after_sync_renderer, t_after_thumb_merge);
        const int64_t us_wg = settings_bench_us(t_after_thumb_merge, t_after_want_vk);
        std::cerr << "[settings-bench] draw#" << s_settings_bench_draw_n << " session=" << s_settings_bench_session
                  << " DEFERRED(busy) embedded=" << (app.embedded ? 1 : 0) << " tab=" << app.activeTab
                  << " shell_snapshot=" << us_snap << "us sync_gpu_backend=" << us_sync << "us thumb_merge=" << us_th
                  << "us want_gpu=" << us_wg << "us merged_thumbs=" << mergedThumbs << "\n";
      }
      eh::settings::widget_picker::present_layer_if_needed(app);
      if (app.wl.display()) (void)wl_display_flush(app.wl.display());
      return;
    }
  }
  const SettingsBenchClock::time_point t_after_ensure = SettingsBenchClock::now();
  debug_log("settings", "draw: paint slot=%s needEmbedGroup=%d embedPresentT=%.3f", gpu_path ? "vk" : std::to_string(paintBi).c_str(), (app.embedded && app.embedPresentT < 1.0f) ? 1 : 0, app.embedPresentT);
  cairo_t* const cr = gpu_path ? app.glRaster.cairo() : app.buf[static_cast<size_t>(paintBi)].cairo();
  cairo_save(cr);

  const bool needEmbedGroup = app.embedded && app.embedPresentT < 1.0f;
  if (needEmbedGroup) {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR_ALPHA);
  } else {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  }
  const SettingsBenchClock::time_point t_after_cairo_buf_prep = SettingsBenchClock::now();

  const SettingsBenchClock::time_point t_before_glass_alpha = SettingsBenchClock::now();
  const double glassOv =
      static_cast<double>(eh::config::overlay_surface_alpha_scale(draw_sc, eh::config::OverlaySurfaceAlphaKind::Settings));
  const double settingsSidebarOv =
      static_cast<double>(std::clamp(draw_sc.appearance.overlayOpacitySettingsSidebar, 0.f, 1.f));
  const SettingsBenchClock::time_point t_after_glass_alpha = SettingsBenchClock::now();
  settings_sync_draw_chrome(app, draw_sc.appearance);
  app.icons.set_icon_theme(app.settings.iconTheme);
  (void)app.icons.refresh_auto_theme_if_needed();
  const double dockMatRaw = static_cast<double>(std::clamp(app.settings.dockOpacity, 0, 100)) / 100.0;
  const double dockMatA = dockMatRaw * glassOv;

  if (app.activeTab == 0) settings_clamp_dock_scroll_px(app);
  else if (app.activeTab == 11) settings_clamp_taskbar_scroll_px(app);
  else if (app.activeTab == 46) app.settingsAccountsScrollPx = std::max(0, app.settingsAccountsScrollPx);
  else if (app.activeTab == 48) settings_clamp_autostart_scroll_px(app);
  else if (app.activeTab == 47) app.settingsBluetoothScrollPx = std::max(0, app.settingsBluetoothScrollPx);
  else if (app.activeTab >= 20 && app.activeTab <= 26) settings_clamp_mango_scroll_px(app);
  else if (app.activeTab == 33) settings_clamp_hyprland_scroll_px(app);
  else if (app.activeTab == 9) settings_clamp_launcher_scroll_px(app);
  if (app.activeTab == 2) settings_clamp_appearance_scroll_px(app);
  if (app.activeTab == 6) settings_clamp_wallpaper_scroll_px(app);
  if (app.activeTab == 7) settings_clamp_monitors_scroll_px(app);
  if (app.activeTab == 44) settings_clamp_icons_scroll_px(app);
  if (app.activeTab == 45) settings_clamp_themes_scroll_px(app);
  if (app.activeTab == 50) app.settingsColorThemesScrollPx = std::max(0, app.settingsColorThemesScrollPx);
   int dockPanelScrollPxPaint = app.activeTab == 0   ? app.settingsDockScrollPx
                                 : app.activeTab == 11 ? app.settingsTaskbarScrollPx
                                 : app.activeTab == 6 ? app.settingsWallpaperScrollPx
                                : app.activeTab == 7 ? app.settingsMonitorsScrollPx
                               : app.activeTab == 8 ? app.settingsSoundScrollPx
                               : app.activeTab == 2 ? app.settingsAppearanceScrollPx
                               : app.activeTab == 44 ? app.settingsIconsScrollPx
                                : app.activeTab == 45 ? app.settingsThemesScrollPx
                               : app.activeTab == 50 ? app.settingsColorThemesScrollPx
                               : app.activeTab == 17 ? app.settingsNetworkScrollPx
                              : app.activeTab == 31 ? app.settingsKeyboardScrollPx
                              : app.activeTab == 47 ? app.settingsBluetoothScrollPx
  : app.activeTab == 48 ? app.autostartScrollPx
                                : (app.activeTab >= 20 && app.activeTab <= 26) ? app.settingsMangoScrollPx
                                : (app.activeTab == 33) ? app.settingsHyprlandScrollPx
                                : (app.activeTab == 9) ? app.settingsLauncherScrollPx
                                                                               : 0;
  settings_scroll_sync_after_clamp(app);
  // Snap animated scroll on tab switch — avoid showing old tab's position
  if (app.settingsScrollLastActiveTab != app.activeTab) {
    app.settingsScroll.snap_to(static_cast<double>(dockPanelScrollPxPaint));
    app.settingsScrollLastActiveTab = app.activeTab;
    app.tabContentDirty = true;
    app.blankLoggedForActiveTab = false;
  }
  // Use animated scroll position (updated each frame by the ScrollController).
  const double paintPointerYOffset = settings_scroll_px(app);

  paint_src_bg(app, cr, 0.78 * glassOv);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  constexpr int kHeaderH = 56;

  const int sidebarW = kSidebarW;
  const int sidebarX = kSpacingL;
  const int contentX = sidebarX + sidebarW + kSpacingL;
  const int contentW = app.width - contentX - kSpacingL;

  const double cardX = static_cast<double>(contentX + 8);
  const double cardW = static_cast<double>(contentW - 16);

  const int sidebarTabX = sidebarX + 8;
  const int sidebarTabW = sidebarW - 16;
  const int sidebarViewH = app.height - kContentTop - kSpacingL;

  cairo_save(cr);

  const double sbX = static_cast<double>(sidebarX);
  const double sbY = static_cast<double>(kContentTop);
  const double sbW = static_cast<double>(sidebarW);
  const double sbH = static_cast<double>(sidebarViewH);
  {
    float bgR, bgG, bgB;
    if (app.drawChromeMatugen) {
      bgR = static_cast<float>(app.drawChrome.panelFillR * 0.35);
      bgG = static_cast<float>(app.drawChrome.panelFillG * 0.35);
      bgB = static_cast<float>(app.drawChrome.panelFillB * 0.35);
    } else {
      bgR = static_cast<float>(Theme::BgR * 0.35);
      bgG = static_cast<float>(Theme::BgG * 0.35);
      bgB = static_cast<float>(Theme::BgB * 0.35);
    }
    m3::Box sbBox;
    sbBox.setColor(bgR, bgG, bgB, static_cast<float>(0.78 * settingsSidebarOv));
    sbBox.setRadius(18.0f);
    sbBox.setGeometry(static_cast<float>(sbX), static_cast<float>(sbY),
                      static_cast<float>(sbW), static_cast<float>(sbH));
    sbBox.setGlassy(true);
    sbBox.paint(cr);
  }

  cairo_round_rect(cr, sbX, sbY, sbW, sbH, 18.0);
  cairo_clip(cr);
  cairo_translate(cr, 0.0, -static_cast<double>(app.sidebarScrollPx));

  auto draw_sidebar_tab = [&](int ty, int indentX, bool sel, bool hover, const char* label, const char* glyph) {
    const int tabX = sidebarX + 8 + indentX;
    const int tabW = sidebarW - 16 - indentX;
    const int tabH = 40;
    {
      m3::Box box;
      float r, g, b, a;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.accentR;
        g = app.drawChrome.accentG;
        b = app.drawChrome.accentB;
      } else {
        r = static_cast<float>(Theme::AccR);
        g = static_cast<float>(Theme::AccG);
        b = static_cast<float>(Theme::AccB);
      }
      if (sel) a = 0.20f;
      else if (hover) a = 0.12f;
      else { r = 0; g = 0; b = 0; a = 0; }
      box.setColor(r, g, b, a);
      box.setRadius(8.0f);
      box.setGeometry(static_cast<float>(tabX), static_cast<float>(ty),
                      static_cast<float>(tabW), static_cast<float>(tabH));
      box.setGlassy(true);
      box.paint(cr);
    }
    material_symbols_draw_glyph(cr, static_cast<double>(tabX + 18), static_cast<double>(ty + 20), 18.0, glyph,
                               Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(cr, tabX + 44, ty + 26, label, 14, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  };

  auto draw_sidebar_subtab = [&](int ty, bool sel, bool hover, const char* label, const char* glyph) {
    const int tabX = sidebarX + kSidebarSubTabIndentX;
    const int tabW = sidebarW - kSidebarSubTabIndentX - 16;
    const int tabH = 36;
    {
      m3::Box box;
      float r, g, b, a;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.accentR;
        g = app.drawChrome.accentG;
        b = app.drawChrome.accentB;
      } else {
        r = static_cast<float>(Theme::AccR);
        g = static_cast<float>(Theme::AccG);
        b = static_cast<float>(Theme::AccB);
      }
      if (sel) a = 0.20f;
      else if (hover) a = 0.12f;
      else { r = 0; g = 0; b = 0; a = 0; }
      box.setColor(r, g, b, a);
      box.setRadius(6.0f);
      box.setGeometry(static_cast<float>(tabX), static_cast<float>(ty),
                      static_cast<float>(tabW), static_cast<float>(tabH));
      box.setGlassy(true);
      box.paint(cr);
    }
    if (glyph)
      material_symbols_draw_glyph(cr, static_cast<double>(tabX + 14), static_cast<double>(ty + 18), 16.0, glyph,
                                  Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(cr, tabX + 32, ty + 23, label, 12, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  };

  auto subtab_active = [&](const SidebarItemDef& def) -> bool {
    if (!def.subTabIds) return false;
    for (int c = 0; c < def.subCount; c++)
      if (def.subTabIds[c] == app.activeTab) return true;
    return false;
  };

  auto sidebar_hit_test = [&](int y, int h) -> bool {
    const int scrolledY = y - app.sidebarScrollPx;
    return app.pointerX >= sidebarTabX && app.pointerX < sidebarTabX + sidebarTabW &&
           app.pointerY >= scrolledY && app.pointerY < scrolledY + h;
  };

  int currentY = kSidebarTabBaseY;
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    const bool isExpanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
    const bool hasChildren = def.subCount > 0;
    const bool isCategory = def.isCategory;
    const bool isSelected = isCategory ? (hasChildren ? (app.activeTab == def.id || subtab_active(def)) : false)
                                        : (app.activeTab == def.id);
    const bool tabHover = sidebar_hit_test(currentY, kSidebarTabH);
    draw_sidebar_tab(currentY, 0, isSelected, tabHover, def.label, def.glyph);
    currentY += kSidebarTabPitchY;
    if (hasChildren && isExpanded) {
      for (int j = 0; j < def.subCount; ++j) {
        const bool subSel = def.subTabIds ? (app.activeTab == def.subTabIds[j])
                                          : (app.activeTab == def.id && app.activeSubTab == j);
        const bool subHover = sidebar_hit_test(currentY, 36);
        const char* subGlyph = def.subGlyphs ? def.subGlyphs[j] : nullptr;
        draw_sidebar_subtab(currentY, subSel, subHover, def.subLabels[j], subGlyph);
        currentY += 36;
      }
    }
  }

  cairo_restore(cr);

  paint_src_glass_hi(app, cr, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, contentX - kSpacingS + 0.5, kContentTop);
  cairo_line_to(cr, contentX - kSpacingS + 0.5, app.height - kSpacingL);
  cairo_stroke(cr);

  const uint64_t contentDrawCountBefore = g_settings_content_draw_count;

  if (app.activeTab == 0 || app.activeTab == 11) {
    const std::unordered_set<std::string>& widgetDisabled =
        app.activeTab == 0 ? app.settings.dockWidgetSlotsDisabled
        : app.settings.taskbarWidgetSlotsDisabled;

    auto paint_one_row_surface_c = [&](const std::string& wid, double rx, double ry, double rw, double rh,
                                       double tgX, double tgY, double rmX, double rmY, bool hoverToggle, bool hoverRemove,
                                       bool slotOn, double matBoost) {
      {
        m3::Box box;
        float r, g, b;
        if (app.drawChromeMatugen) {
          r = app.drawChrome.panelFillR;
          g = app.drawChrome.panelFillG;
          b = app.drawChrome.panelFillB;
        } else {
          r = static_cast<float>(Theme::BgR);
          g = static_cast<float>(Theme::BgG);
          b = static_cast<float>(Theme::BgB);
        }
        box.setColor(r, g, b, static_cast<float>(std::min(1.0, (0.9 + matBoost))));
        box.setRadius(10.0f);
        box.setGeometry(static_cast<float>(rx), static_cast<float>(ry),
                        static_cast<float>(rw), static_cast<float>(rh));
        box.setGlassy(true);
        box.paint(cr);
      }

      settings_draw_drag_handle_row(app, cr, rx + 8.0, ry + rh * 0.5, 1.0);

      const double iw = 32.0;
      const double ix = rx + 32.0;
      const double iy = ry + (rh - iw) * 0.5;
      {
        m3::Box box;
        box.setColor(1.0f, 1.0f, 1.0f, 0.05f);
        box.setRadius(8.0f);
        box.setGeometry(static_cast<float>(ix), static_cast<float>(iy),
                        static_cast<float>(iw), static_cast<float>(iw));
        box.setGlassy(true);
        box.paint(cr);
      }
      double gR = 0.88, gG = 0.93, gB = 0.96;
      if (app.drawChromeMatugen) {
        gR = 0.5 * app.drawChrome.accentR + 0.5 * 0.92;
        gG = 0.5 * app.drawChrome.accentG + 0.5 * 0.95;
        gB = 0.5 * app.drawChrome.accentB + 0.5 * 0.98;
      }
      material_symbols_draw_glyph(cr, ix + iw * 0.5, iy + iw * 0.5 + 0.5, 23.0, dock_widget_material_ligature(wid), gR,
                                  gG, gB, 0.95);

      const double txtX = ix + iw + 8.0;
      const std::string title = widget_display_title(wid);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 13);
      settings_draw_trimmed_text_line(cr, title, txtX, ry + 32.5, 44, 1.0, 13.f, 700);
      settings_draw_row_remove_button(app, cr, rx + rmX, ry + rmY, kWidgetRowRemoveHit, hoverRemove, 1.0);
      settings_draw_widget_accent_toggle(app, cr, tgX, tgY, hoverToggle, 1.0, slotOn);
    };

    // Ensure the cache surface exists for tabs 0/1/11.
    {
      const int tabIdx = app.activeTab;
      const int anchor = widget_section_y0_for_tab(app, 2, tabIdx);
      const auto* rr = widgets_for_section_for_tab(app, 2, tabIdx);
      const int extent = anchor + widget_section_block_height(static_cast<int>(rr->size())) + kSpacingXL;
      const int cacheNeededH = std::max(app.height, extent + kSpacingXL);
      if (app.tabContentCache && (app.tabContentCacheW != contentW || app.tabContentCacheH < cacheNeededH)) {
        cairo_surface_destroy(app.tabContentCache);
        app.tabContentCache = nullptr;
      }
      if (!app.tabContentCache) {
        app.tabContentCache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, std::max(1, contentW), std::max(1, cacheNeededH));
        if (cairo_surface_status(app.tabContentCache) == CAIRO_STATUS_SUCCESS) {
          app.tabContentCacheW = contentW;
          app.tabContentCacheH = cacheNeededH;
        } else {
          cairo_surface_destroy(app.tabContentCache);
          app.tabContentCache = nullptr;
        }
        app.tabContentDirty = true;
      }
    }

    {
      cairo_save(cr);
      cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                      static_cast<double>(contentW),
                      static_cast<double>(app.height - kContentTop - kSpacingL));
      cairo_clip(cr);
      cairo_translate(cr, 0.0, -paintPointerYOffset);

  if (app.activeTab == 0) {
    const int autoBarHeightPx = settings_dock_preview_auto_bar_px(app.settings);
    const int chDock = static_cast<int>(settings_dock_widgets_card_fill_height_px(app));
    paint_dock_tab(app, cr, contentX, contentW, glassOv, dockMatA, paintPointerYOffset, chDock, autoBarHeightPx);
  }

  if (app.activeTab == 11) {
    paint_taskbar_tab(app, cr, contentX, contentW, glassOv, dockMatA, paintPointerYOffset, 0);
  }

    settings_paint_mode_dropdown_popups(app, cr, contentX, contentW, glassOv);

      cairo_restore(cr);
    }

    // Ghost rendering for old-style rows (dock non-widgets tabs only)
    // The dock and taskbar Widgets tabs render their own card-style ghost
    if ((app.activeTab == 0 && !dock_m3_is_widgets_child_tab()) &&
        app.widgetDragging && app.widgetDragSection >= 0 && app.widgetDragFromIndex >= 0) {
      const auto* vghost = widgets_for_section_const(app, app.widgetDragSection);
      const size_t from = static_cast<size_t>(app.widgetDragFromIndex);
      if (from < vghost->size()) {
        const std::string& w = (*vghost)[from];
        double rowIxUnused = 0;
        double rowIw = 0;
        widget_section_row_inner_geometry(app, &rowIxUnused, &rowIw);
        const double rw = rowIw;
        const double rmXg = rw - kWidgetRowRightPad - kWidgetRowRemoveHit;
        const double rmYg = static_cast<double>(kWidgetRowH) * 0.5 - kWidgetRowRemoveHit * 0.5;
        const double tgXg = rmXg - kWidgetRowToggleRemoveGap - kWidgetRowToggleW;
        const double tgYg = static_cast<double>(kWidgetRowH) * 0.5 - kWidgetRowToggleH * 0.5;
        const double gx = app.pointerX - app.widgetDragGrabDx;
        const double gy = app.pointerY - app.widgetDragGrabDy;
        const bool ghostOn = widget_slot_enabled_for_settings(widgetDisabled, w);
        paint_one_row_surface_c(w, gx, gy, rw, static_cast<double>(kWidgetRowH), gx + tgXg, gy + tgYg, rmXg, rmYg, false,
                                false, ghostOn, 0.12);
      }
    }
  }

  if (app.activeTab == 2) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_appearance_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset);
    cairo_restore(cr);
  } else if (app.activeTab == 44) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_icons_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset);
    cairo_restore(cr);
  } else if (app.activeTab == 45) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_themes_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset);
    cairo_restore(cr);
  } else if (app.activeTab == 50) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_color_themes_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
  } else if (app.activeTab == 3) {
    paint_layout_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 4) {
    paint_workspaces_tab(app, cr, contentX, contentW, glassOv, paintPointerYOffset, dockMatA);
  } else if (app.activeTab == 5) {
    paint_notifications_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 6) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_wallpaper_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset);
    cairo_restore(cr);
  }
  if (app.activeTab == 16) {
    paint_bing_tab(app, cr, contentX, contentW, glassOv);
  }
  if (app.activeTab == 7) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_monitors_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset);
    cairo_restore(cr);
    settings_paint_monitors_dropdown_unclipped(app, cr, static_cast<int>(contentX), contentW, glassOv);
  } else if (app.activeTab == 8) {
    paint_sound_tab(app, cr, contentX, contentW, glassOv, paintPointerYOffset);
  } else if (app.activeTab == 9) {
    settings_clamp_launcher_scroll_px(app);
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_launcher_tab_m3(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
  } else if (app.activeTab == 10) {
    paint_default_apps_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 17) {
    paint_wifi_tab(app, cr, contentX, contentW, glassOv, paintPointerYOffset);
  } else if (app.activeTab == 18) {
    paint_wired_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 28) {
    paint_vpn_tab(app, cr, contentX, contentW, glassOv);
  } else   if (app.activeTab == 19) {
    paint_nightlight_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 27) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_desktop_widgets_tab(app, cr, contentX, contentW, glassOv, paintPointerYOffset);
    cairo_restore(cr);
  } else if (app.activeTab == 29) {
    paint_desktop_tab(app, cr, contentX, contentW, glassOv);
  } else if (app.activeTab == 30) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_time_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
    if (app.timeFormatDropdownOpen) {
      int cbx, cby, cbw, cbh;
      time_date_format_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
      const int listTop = cby + cbh + 2 - settings_scroll_px_int(app);
      settings_paint_combo_list_popup(app, cr, cbx, listTop, cbw, kSettingsDdRowH, kDateFormatCount,
                                      kDateFormatLabels, app.settings.timeDateFormat,
                                      app.timeFormatDropdownHoverRow, glassOv);
    }
  } else if (app.activeTab == 31) {
    app.settingsKeyboardScrollPx = std::max(0, std::min(app.settingsKeyboardScrollPx, 400));
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_keyboard_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
    if (app.settingsKeyboardDdKind >= 0) {
      const int rightRail = contentX + contentW - 32;
      const int cbx = rightRail - keyboard_tab::kComboW;
      const int cbw = keyboard_tab::kComboW;
      const int cbh = keyboard_tab::kComboH;
      int ddY = 0;
      int nItems = 0;
      const char* const* labels = nullptr;
      const std::vector<std::string>* layoutLabels = nullptr;
      if (app.settingsKeyboardDdKind == 0) {
        ddY = keyboard_tab::kBodyTop + 0 * kSliderRowH + 40 + cbh + 2;
        nItems = static_cast<int>(app.settings.keyboardLayouts.size());
        layoutLabels = &app.settings.keyboardLayouts;
      } else if (app.settingsKeyboardDdKind == 1) {
        ddY = keyboard_tab::kBodyTop + 1 * kSliderRowH + 40 + cbh + 2;
        nItems = kSwitchShortcutCount;
        labels = kSwitchShortcutLabels;
      } else if (app.settingsKeyboardDdKind == 2) {
        ddY = keyboard_tab::kBehaviorCardTop + 52 + 0 * kSliderRowH + 40 + cbh + 2;
        nItems = kCapsLockCount;
        labels = kCapsLockLabels;
      } else if (app.settingsKeyboardDdKind == 3) {
        ddY = keyboard_tab::kBehaviorCardTop + 52 + 1 * kSliderRowH + 40 + cbh + 2;
        nItems = kComposeKeyCount;
        labels = kComposeKeyLabels;
      }
      if (nItems > 0) {
        static std::vector<const char*> ddLabels;
        static std::vector<std::string> ddStrStorage;
        ddLabels.clear();
        ddStrStorage.clear();
        ddStrStorage.reserve(nItems);
        ddLabels.reserve(nItems);
        if (layoutLabels) {
          for (const auto& l : *layoutLabels) {
            ddStrStorage.push_back(l);
          }
          for (const auto& s : ddStrStorage) {
            ddLabels.push_back(s.c_str());
          }
          labels = ddLabels.data();
        }
        int selIdx = 0;
        switch (app.settingsKeyboardDdKind) {
          case 0: {
            auto it = std::find(app.settings.keyboardLayouts.begin(), app.settings.keyboardLayouts.end(), app.settings.keyboardLayout);
            if (it != app.settings.keyboardLayouts.end()) selIdx = static_cast<int>(it - app.settings.keyboardLayouts.begin());
            break;
          }
          case 1: selIdx = app.settings.keyboardSwitchShortcut; break;
          case 2: selIdx = app.settings.keyboardCapsLockBehavior; break;
          case 3: selIdx = app.settings.keyboardComposeKey; break;
        }
        const int ddScreenY = ddY - settings_scroll_px_int(app);
        settings_paint_combo_list_popup(app, cr, cbx, ddScreenY, cbw, kSettingsDdRowH, nItems,
                                        labels, selIdx, app.settingsKeyboardDdHoverRow, glassOv);
      }
    }
  } else if (app.activeTab == 32) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_power_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
    if (app.powerChildTab == 0) {
      if (app.powerBtnDropdownOpen) {
        int cbx, cby, cbw, cbh;
        power_btn_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
        const int listTop = cby + cbh + 2 - settings_scroll_px_int(app);
        static const char* kBtnLabels[] = {"Ask what to do", "Suspend", "Hibernate", "Shut down"};
        settings_paint_combo_list_popup(app, cr, cbx, listTop, cbw, kSettingsDdRowH, 4,
                                        kBtnLabels, app.settings.powerPowerButtonAction,
                                        app.powerBtnDropdownHoverRow, glassOv, 0, 0, true);
      }
      if (app.lidCloseDropdownOpen) {
        int cbx, cby, cbw, cbh;
        lid_close_combo_geom(contentX, contentW, cbx, cby, cbw, cbh);
        const int listTop = cby + cbh + 2 - settings_scroll_px_int(app);
        static const char* kLidLabels[] = {"Do nothing", "Suspend", "Hibernate"};
        settings_paint_combo_list_popup(app, cr, cbx, listTop, cbw, kSettingsDdRowH, 3,
                                        kLidLabels, app.settings.powerLidCloseAction,
                                        app.lidCloseDropdownHoverRow, glassOv, 0, 0, true);
      }
    }
    if (app.powerChildTab == 1) {
      const int scr = settings_scroll_px_int(app);
      power_gov_dd_sync(app, contentX, contentW);
      app.powerGovDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
      power_epp_dd_sync(app, contentX, contentW);
      app.powerEppDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
      power_tuned_dd_sync(app, contentX, contentW);
      app.powerTunedDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
    }

  } else if (app.activeTab == 47) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_bluetooth_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
  } else if (app.activeTab == 46) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_accounts_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
  } else if (app.activeTab == 48) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_autostart_tab(app, cr, contentX, contentW, glassOv);
    cairo_restore(cr);
  }
  if (app.activeTab >= 20 && app.activeTab <= 26) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_mango_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset,
                    app.activeTab - 20);
    cairo_restore(cr);
  }

  if (app.activeTab == 33) {
    cairo_save(cr);
    cairo_rectangle(cr, static_cast<double>(contentX), static_cast<double>(kContentTop),
                    static_cast<double>(contentW),
                    static_cast<double>(app.height - kContentTop - kSpacingL));
    cairo_clip(cr);
    cairo_translate(cr, 0.0, -paintPointerYOffset);
    paint_hyprland_tab(app, cr, contentX, contentW, cardX, cardW, glassOv, dockMatA, paintPointerYOffset,
                       app.activeTab - 33);
    cairo_restore(cr);
  }

  if (!app.blankLoggedForActiveTab && g_settings_content_draw_count == contentDrawCountBefore) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "BLANK TAB: tab=%d activeSubTab=%d - no content rendered in content area",
                  app.activeTab, app.activeSubTab);
    debug_log("settings", "%s", buf);
    app.blankLoggedForActiveTab = true;
  }

  if (app.hyprlandBezierOpen && app.activeTab == 33) {
    paint_hyprland_bezier_editor(app, cr, contentX, contentW);
  } else if (app.hyprlandAnimEditIdx >= 0 && app.activeTab == 33) {
    paint_hyprland_anim_popup(app, cr, contentX, contentW);
  }

  if (app.hyprlandLayoutDropdownOpen && app.activeTab == 33) {
    constexpr int kLayoutComboW = 140;
    int layoutBx = app.hyprlandLayoutComboX;
    int listTop = app.hyprlandLayoutComboY + kSettingsComboH + 2 - settings_scroll_px_int(app);
    static const char* kHyprLayoutLabels[] = {"dwindle", "master", "scrolling", "monocle"};
    int layoutIdx = 0;
    for (int i = 0; i < 4; ++i)
      if (app.hyprlandConfig.general.layout == kHyprLayoutLabels[i]) { layoutIdx = i; break; }
    settings_paint_combo_list_popup(app, cr, layoutBx, listTop, kLayoutComboW, kSettingsDdRowH, 4,
                                    kHyprLayoutLabels, layoutIdx,
                                    app.hyprlandLayoutDropdownHoverRow, glassOv);
  }

  if (wifi_password_prompt_visible(app)) {
    wifi_password_prompt_paint(app, cr);
  }

  if (keyring_prompt_visible(app)) {
    keyring_prompt_paint(app, cr);
  }

  if (vpn_add_dialog_visible(app)) {
    vpn_add_dialog_paint(app, cr);
  }

  if (app.widgetPickerOpen) {
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "draw: painting widget picker overlay %dx%d inset=%d",
                    app.width, app.height, kWidgetPickerSettingsContentInsetX);
    }
    eh::settings::widget_picker::paint(app, cr, app.width, app.height, kWidgetPickerSettingsContentInsetX);
  }

  if (world_clock_popup_visible(app)) {
    world_clock_popup_paint(app, cr);
  }

  if (app.iconThemePickerOpen) {

    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_rectangle(cr, 0, 0, app.width, app.height);
    cairo_fill(cr);

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

    {
      m3::Box box;
      float r, g, b;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.panelFillR;
        g = app.drawChrome.panelFillG;
        b = app.drawChrome.panelFillB;
      } else {
        r = static_cast<float>(Theme::BgR);
        g = static_cast<float>(Theme::BgG);
        b = static_cast<float>(Theme::BgB);
      }
      box.setColor(r, g, b, 0.96f);
      box.setRadius(14.0f);
      box.setGeometry(static_cast<float>(px), static_cast<float>(py),
                      static_cast<float>(pw), static_cast<float>(ph));
      box.setGlassy(true);
      box.paint(cr);
    }

    settings_show_text(cr, px + 18, py + 30, "Icon theme", 14, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    std::string sub = "System theme: " + sys;
    settings_show_text(cr, px + 18, py + 46, sub.c_str(), 11, CAIRO_FONT_WEIGHT_NORMAL, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    const int listY0 = py + 56;
    for (int i = 0; i < n; ++i) {
      const int ry = listY0 + i * rowPitch;
      const bool hov = (app.pointerX >= px + 10 && app.pointerX < px + pw - 10 &&
                        app.pointerY >= ry && app.pointerY < ry + rowPitch);
      cairo_rectangle(cr, px + 10, ry, pw - 20, rowPitch);
      paint_src_glass_hi(app, cr, hov ? 0.06 : 0.00);
      cairo_fill(cr);

      const auto& t = themes[static_cast<size_t>(i)];
      std::string line = t.name + "  [" + t.id + "]";

      settings_show_text(cr, px + 18, ry + rowPitch - 12, line.c_str(), 12, CAIRO_FONT_WEIGHT_NORMAL, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }
  }

  if (app.wallpaperFolderPickerOpen) {
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_rectangle(cr, 0, 0, app.width, app.height);
    cairo_fill(cr);

    int px = 0;
    int py = 0;
    int pw = 0;
    int ph = 0;
    int rowPitch = 0;
    int n = 0;
    wallpaper_folder_picker_modal_geom(app, &px, &py, &pw, &ph, &rowPitch, &n);

    {
      m3::Box box;
      float r, g, b;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.panelFillR;
        g = app.drawChrome.panelFillG;
        b = app.drawChrome.panelFillB;
      } else {
        r = static_cast<float>(Theme::BgR);
        g = static_cast<float>(Theme::BgG);
        b = static_cast<float>(Theme::BgB);
      }
      box.setColor(r, g, b, 0.96f);
      box.setRadius(14.0f);
      box.setGeometry(static_cast<float>(px), static_cast<float>(py),
                      static_cast<float>(pw), static_cast<float>(ph));
      box.setGlassy(true);
      box.paint(cr);
    }

    settings_show_text(cr, px + 18, py + 30, "Wallpaper folder picker", 14, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    settings_show_text(cr, px + 18, py + 46, "Choose how the folder button opens; saved with settings.", 11, CAIRO_FONT_WEIGHT_NORMAL, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    const int listY0 = py + 56;
    for (int i = 0; i < n; ++i) {
      const int ry = listY0 + i * rowPitch;
      const bool hov = (app.pointerX >= px + 10 && app.pointerX < px + pw - 10 &&
                        app.pointerY >= ry && app.pointerY < ry + rowPitch);
      cairo_rectangle(cr, px + 10, ry, pw - 20, rowPitch);
      const bool sel = std::clamp(app.settings.wallpaperFolderPickerMode, 0, 3) == i;
      paint_src_glass_hi(app, cr, (hov ? 0.08 : 0.0) + (sel ? 0.06 : 0.0));
      cairo_fill(cr);

      settings_show_text(cr, px + 18, ry + rowPitch - 12, wallpaper_folder_picker_row_label(i), 12, sel ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    }
  }

  if (app.defaultAppPickerOpen && app.activeTab == 10)
    settings_paint_default_app_picker_popup(app, cr, contentX, contentW, glassOv);

  settings_paint_mode_dropdown_popups(app, cr, contentX, contentW, glassOv);

  if (app.activeTab == 9 && app.launcherViewModeDropdownOpen) {
    int vcx, vcy, vcw, vch;
    launcher_view_mode_combo_geom(contentX, contentW, &vcx, &vcy, &vcw, &vch);
    const int ly = vcy + vch + 2;
    settings_paint_combo_list_popup(app, cr, vcx, ly, vcw, kSettingsDdRowH, kViewModeCount, kViewModeLabels,
                                    app.settings.launchpadViewMode, app.launcherViewModeDropdownHoverRow, glassOv);
  }

  if (needEmbedGroup) {
    debug_log("settings", "draw: embed group pop+translate %.3f embedPresentT=%.3f",
              (1.0 - static_cast<double>(app.embedPresentT)) * kEmbedSlidePx, app.embedPresentT);
    cairo_pop_group_to_source(cr);
    cairo_translate(cr, 0.0, (1.0 - static_cast<double>(app.embedPresentT)) * kEmbedSlidePx);
    cairo_paint(cr);
  }

  // Title bar buttons (minimize, maximize, close).
  if (app.embedded) {
    constexpr int kBtnSize = 20;
    constexpr int kBtnGap  = 8;
    if (!app.btnMin) app.btnMin = eh::shell::asset::load_asset_svg("UI", "btn-minimize.svg", kBtnSize);
    if (!app.btnMax) app.btnMax = eh::shell::asset::load_asset_svg("UI", "btn-maximize.svg", kBtnSize);
    if (!app.btnClose) app.btnClose = eh::shell::asset::load_asset_svg("UI", "btn-close.svg", kBtnSize);
    const int btnY = (kHeaderH - kBtnSize) / 2;
    int bx = app.width - kSpacingL;
    auto paint_btn = [&](cairo_surface_t* surf, int idx) {
      bx -= kBtnSize;
      const int hovered = app.btnHoverIdx == idx;
      if (hovered) {
        cairo_save(cr);
        cairo_arc(cr, bx + kBtnSize / 2.0, btnY + kBtnSize / 2.0, kBtnSize / 2.0 + 2, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.15);
        cairo_fill(cr);
        cairo_restore(cr);
      }
      if (surf) {
        cairo_save(cr);
        cairo_translate(cr, bx, btnY);
        cairo_set_source_surface(cr, surf, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
      }
      bx -= kBtnGap;
    };
    paint_btn(app.btnClose, 2);
    paint_btn(app.btnMax, 1);
    paint_btn(app.btnMin, 0);
  }

  cairo_restore(cr);

  cairo_surface_flush(gpu_path ? app.glRaster.cairo_surface() : app.buf[static_cast<size_t>(paintBi)].cairo_surface());

  const SettingsBenchClock::time_point t_after_cairo = SettingsBenchClock::now();
  if (vk_path) {
    bool transient = false;
    if (!settings_present_vk_raster(app, app.width, app.height, &transient)) {
      if (transient) {
        app.settingsDeferRedraw = true;
        if (app.width > 0 && app.height > 0 && ensure_settings_shm_pair(app)) {
          const int fb = pick_settings_paint_buffer(app);
          if (fb >= 0) {
            auto& pb = app.buf[static_cast<size_t>(fb)];
            const int copyH = std::min(app.glRaster.height(), pb.height());
            const int srcStride = app.glRaster.stride();
            const int dstStride = pb.stride();
            unsigned char* src = app.glRaster.data();
            unsigned char* dst = static_cast<unsigned char*>(pb.data());
            if (src && dst && copyH > 0) {
              const int copyRow = std::min(srcStride, dstStride);
              debug_log("settings", "draw: VK fallback copy to SHM buf=%d embedPresentT=%.3f", fb, app.embedPresentT);
              for (int y = 0; y < copyH; ++y)
                std::memcpy(dst + y * dstStride, src + y * srcStride, static_cast<size_t>(copyRow));
              cairo_surface_flush(pb.cairo_surface());
              wl_surface_attach(app.surface, pb.wl(), 0, 0);
              wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
              pb.mark_busy();
              schedule_settings_surface_frame(app);
              eh::settings::widget_picker::queue_caret_frame(app);
              world_clock_popup_queue_caret_frame(app);
              wl_surface_commit(app.surface);
              if (app.wl.display()) (void)wl_display_flush(app.wl.display());
              if (app.embedded) {
                app.last_committed_draw_w = app.width;
                app.last_committed_draw_h = app.height;
              }
              if (eh_settings_bench()) {
                ++s_settings_bench_draw_n;
                char line[1024];
                snprintf(line, sizeof(line),
                  "[settings-bench] draw#%u session=%u embedded=%d tab=%d %dx%d VK_FALLBACK_SHM\n",
                  s_settings_bench_draw_n, s_settings_bench_session, app.embedded ? 1 : 0, app.activeTab,
                  app.width, app.height);
                std::cerr << line;
              }
              return;
            }
          }
        }
      }
      if (app.wl.display()) (void)wl_display_flush(app.wl.display());
      return;
    }
    debug_log("settings", "draw: VK commit embedPresentT=%.3f", app.embedPresentT);
    wl_surface_damage_buffer(app.surface, 0, 0, INT32_MAX, INT32_MAX);
    eh::settings::widget_picker::queue_caret_frame(app);
    world_clock_popup_queue_caret_frame(app);
    wl_surface_commit(app.surface);
  } else {
    eh::wayland::ShmBuffer& pb = app.buf[static_cast<size_t>(paintBi)];
    wl_surface_attach(app.surface, pb.wl(), 0, 0);
    wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
    pb.mark_busy();
    debug_log("settings", "draw: SHM commit buf=%d embedPresentT=%.3f", paintBi, app.embedPresentT);
    {
      static bool s_dumped = false;
      if (!s_dumped && app.embedded) {
        s_dumped = true;
        cairo_surface_write_to_png(pb.cairo_surface(), "/tmp/settings_first_draw.png");
        debug_log("settings", "draw: dumped first SHM buffer to /tmp/settings_first_draw.png (w=%d h=%d)", pb.width(), pb.height());
      }
    }
    schedule_settings_surface_frame(app);
    eh::settings::widget_picker::queue_caret_frame(app);
    world_clock_popup_queue_caret_frame(app);
    wl_surface_commit(app.surface);
  }
  if (app.wl.display()) (void)wl_display_flush(app.wl.display());
  if (app.embedded) {
    app.last_committed_draw_w = app.width;
    app.last_committed_draw_h = app.height;
    debug_log("settings", "draw: committed %dx%d", app.width, app.height);
  }
  const SettingsBenchClock::time_point t_after_wl = SettingsBenchClock::now();
  if (eh_settings_bench()) {
    ++s_settings_bench_draw_n;
    const int64_t us_shell_snap = settings_bench_us(t_draw_enter, t_after_shell_snapshot);
    const int64_t us_sync_gpu = settings_bench_us(t_after_shell_snapshot, t_after_sync_renderer);
    const int64_t us_thumbs = settings_bench_us(t_after_sync_renderer, t_after_thumb_merge);
    const int64_t us_want_vk = settings_bench_us(t_after_thumb_merge, t_after_want_vk);
    const int64_t us_buffers = settings_bench_us(t_after_want_vk, t_after_ensure);
    const int64_t us_buf_prep = settings_bench_us(t_after_ensure, t_after_cairo_buf_prep);
    const int64_t us_glass_alpha = settings_bench_us(t_before_glass_alpha, t_after_glass_alpha);
    const int64_t us_cairo_body = settings_bench_us(t_after_glass_alpha, t_after_cairo);
    const int64_t us_commit = settings_bench_us(t_after_cairo, t_after_wl);
    const int64_t us_total = settings_bench_us(t_draw_enter, t_after_wl);
    const int64_t us_sess = settings_bench_us(s_settings_bench_t0, t_after_wl);
    const double fps_inst = us_total > 0 ? 1000000.0 / static_cast<double>(us_total) : 0.0;
    const double fps_avg = us_sess > 0 ? static_cast<double>(s_settings_bench_draw_n) * 1000000.0 / static_cast<double>(us_sess) : 0.0;
    char line[1024];
    snprintf(line, sizeof(line),
      "[settings-bench] draw#%u session=%u embedded=%d tab=%d %dx%d slot=%s"
      " shell_snapshot=%ldus sync_gpu_backend=%ldus thumb_merge=%ldus want_vk=%ldus"
      " buffers_shm_or_vk=%ldus cairo_clear_push=%ldus glass_alpha_read=%ldus"
      " cairo_body=%ldus wl_attach_commit_flush=%ldus total=%ldus"
      " since_session_start=%ldus fps_inst=%.1f fps_avg=%.1f merged_thumbs=%d\n",
      s_settings_bench_draw_n, s_settings_bench_session, app.embedded ? 1 : 0, app.activeTab,
      app.width, app.height, vk_path ? "vk" : std::to_string(paintBi).c_str(),
      us_shell_snap, us_sync_gpu, us_thumbs, us_want_vk,
      us_buffers, us_buf_prep, us_glass_alpha,
      us_cairo_body, us_commit, us_total,
      us_sess, fps_inst, fps_avg, mergedThumbs);
    std::cerr << line;
    static FILE* s_log = nullptr;
    if (!s_log) {
      s_log = fopen("/tmp/eh_settings_bench.log", "w");
      if (s_log) {
        time_t now_t = time(nullptr);
        fprintf(s_log, "# Event Horizon Settings Bench  session=%u  %s", s_settings_bench_session, ctime(&now_t));
        fprintf(s_log, "# draw# session embedded tab WxH slot shell_snapshot sync_gpu thumb_merge want_vk"
                       " buffers buf_prep glass_alpha cairo_body commit total since_start"
                       " fps_inst fps_avg merged_thumbs\n");
      }
    }
    if (s_log) { fputs(line, s_log); fflush(s_log); }
  }
}


