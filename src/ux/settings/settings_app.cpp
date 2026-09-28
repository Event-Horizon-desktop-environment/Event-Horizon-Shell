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
#include "ux/settings/settings_tab_monitors/monitors_log.hpp"
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
#include "ux/settings/common/retain/retained_surface.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/settings_tab_workspaces/settings_tab_workspaces.hpp"
#include "ux/settings/settings_tab_bing/settings_tab_bing.hpp"
#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"
#include "ux/settings/settings_tab_panel/settings_tab_panel.hpp"
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
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/common/logo/settings_logo.hpp"
#include "ux/settings/utils/gpu/settings_gpu.hpp"
#include "ux/settings/utils/sound/settings_sound_cache.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/utils/search/settings_search.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

static int settings_search_text_width_px(const char* text, float fontSize, int weight) {
  if (!text || !text[0]) return 0;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return 0;
  }
  cairo_t* mcr = cairo_create(surf);
  auto* layout = pango_cairo_create_layout(mcr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int tw = 0, th = 0;
  pango_layout_get_pixel_size(layout, &tw, &th);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(mcr);
  cairo_surface_destroy(surf);
  return tw;
}

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

static const char* const kPanelsAndUISubs[] = {"Dock", "Panel", "Taskbar", "Launcher", "Desktop", "Desktop Widgets"};
static const char* const kPanelsAndUISubGlyphs[] = {"dock_to_bottom", "dock_to_top", "dock_to_bottom", "apps", "desktop_windows", "widgets"};
static const int kPanelsAndUITabIds[] = {0, 1, 11, 9, 29, 27};
static const char* const kDisplaySubs[] = {"Monitors", "Appearance", "Themes", "Color Themes", "Icons", "Nightlight"};
static const char* const kDisplaySubGlyphs[] = {"monitor", "palette", "palette", "colorize", "photo_library", "dark_mode"};
static const int kDisplayTabIds[] = {7, 2, 45, 50, 44, 19};
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
    {12, "Panels & UI", "dashboard", kPanelsAndUISubs, kPanelsAndUISubGlyphs, 6, true, kPanelsAndUITabIds},
    {13, "Display", "display_settings", kDisplaySubs, kDisplaySubGlyphs, 6, true, kDisplayTabIds},
    {16, "MangoWM", "view_module", kMangoSubs, kMangoSubGlyphs, 7, true, kMangoTabIds},
    {33, "Hyprland", "view_module", nullptr, nullptr, 0, false, nullptr},
    {14, "System", "tune", kSystemSubs, kSystemSubGlyphs, 9, true, kSystemTabIds},
    {15, "Wallpaper Settings", "wallpaper", kWallpaperSettingsSubs, kWallpaperSettingsSubGlyphs, 2, true, kWallpaperSettingsTabIds},
    {17, "Network", "wifi", kNetworkSubs, kNetworkSubGlyphs, 3, true, kNetworkTabIds},
};
static constexpr int kSidebarDefCount = sizeof(kSidebarDefs) / sizeof(kSidebarDefs[0]);

static int compute_sidebar_total_height(const App& app) {
  if (!app.settingsSearchQuery.empty()) {
    const std::vector<int> res = settings_search_collect(app.settingsSearchQuery, app.monitorsTab.kind);
    const int n = static_cast<int>(res.size());
    if (n == 0) return kSidebarTabBaseY + 60;
    return kSidebarTabBaseY + n * kSettingsSearchResultPitch + 8;
  }
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
    debug_log("settings", "frame_done: redraw needAnim=%d needScroll=%d needPendingRedraw=%d needThumb=%d needWallHover=%d coalescedDrag=%d needTog=%d scrollNeedsRedraw=%d tab=%d embedPresentT=%.3f",
              needAnim, needScrollAnim, needPendingRedraw, needThumb ? 1 : 0,
              needWallpaperHoverAnim ? 1 : 0, coalescedWidgetDrag ? 1 : 0, needTogAnim ? 1 : 0,
              app.settingsScrollNeedsRedraw ? 1 : 0, app.activeTab, app.embedPresentT);
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
  debug_log("settings", "schedule_frame: needAnim=%d needScroll=%d pendingRedraw=%d needThumb=%d needWallHover=%d coalescedDrag=%d needTog=%d scrollNeedsRedraw=%d tab=%d embedPresentT=%.3f",
            needAnim, needScrollAnim, needPendingRedraw, needThumb ? 1 : 0,
            needWallpaperHoverAnim ? 1 : 0, needCoalescedWidgetDrag ? 1 : 0, needTogAnim ? 1 : 0,
            app.settingsScrollNeedsRedraw ? 1 : 0, app.activeTab, app.embedPresentT);
  static const wl_callback_listener kListener = {.done = settings_surface_frame_done};
  app.surfaceFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.surfaceFrameCb, &kListener, &app);
}





#include "settings/settings_tab_monitors/settings_monitors_ui.inl"

static void settings_paint_monitors_dropdown_unclipped(App& app, cairo_t* cr, int contentX, int contentW,
                                                       double glassOv) {
  if (app.monitorsActiveDd < 0 || app.monitorsTab.outputs.empty()) return;
  const auto t_dd0 = SettingsBenchClock::now();

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
  eh::settings::monitors_log::MonitorsLog::instance().writef(
      "paint dropdown kind=%d nrows=%d sel=%d total=%lldus", app.monitorsActiveDd, nrows, selIx,
      settings_bench_us(t_dd0, SettingsBenchClock::now()));
}

extern const char* const kWidthModeLabels[];
extern const char* const kPanelWidthModeLabels[];
extern const char* const kThumbThresholdLabels[];
extern const int kThumbThresholdValues[];
extern const int kThumbThresholdCount;

static void settings_paint_mode_dropdown_popups(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {

  if (app.activeTab == 0 && !dock_m3_is_widgets_child_tab() &&
      !dock_m3_is_appearance_child_tab()) {
    dock_renderer_dd_sync(app, contentX, contentW);
    dock_display_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.rendererDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
    app.dockDisplayDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.activeTab == 1 && !panel_m3_is_appearance_child_tab() && !panel_m3_is_widgets_child_tab()) {
    panel_display_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.panelDisplayDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.activeTab == 11 && !taskbar_m3_is_appearance_child_tab() && !taskbar_m3_is_widgets_child_tab()) {
    taskbar_display_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.taskbarDisplayDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.activeTab == 27) {
    desktop_widgets_display_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.desktopWidgetsDisplayDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
  }

  if (app.activeTab == 5) {
    notif_display_dd_sync(app, contentX, contentW);
    const int scr = settings_scroll_px_int(app);
    app.notifDisplayDd.paint_popup(app, cr, scr, app.width, app.height, glassOv);
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

  if (app.taskbarThumbThresholdDropdownOpen && app.activeTab == 11) {
    const int kVisCardTopM3 = kContentTop + kDockChildTabH + 12;
    const int cbx = contentX + 8 + contentW - 16 - kCardPad - kSettingsComboW;
    const int cby = kVisCardTopM3 + 52 + 13 * kDockVisRowPitch + (kDockVisRowPitch - kSettingsComboH) / 2;
    const int ly = cby + kSettingsComboH + 2 - settings_scroll_px_int(app);
    int selIx = 2;
    const int curV = std::clamp(app.settings.taskbarThumbnailThreshold, 3, 20);
    for (int i = 0; i < kThumbThresholdCount; ++i) {
      if (kThumbThresholdValues[i] == curV) { selIx = i; break; }
    }
    settings_paint_combo_list_popup(app, cr, cbx, ly, kSettingsComboW, kSettingsDdRowH, kThumbThresholdCount,
                                    kThumbThresholdLabels, selIx,
                                    app.taskbarThumbThresholdDropdownHoverRow, glassOv);
  }

  if (app.panelWidthModeDropdownOpen && app.activeTab == 1) {
    const int kVisCardTopM3 = kContentTop + kDockChildTabH + 12;
    const int cbx = contentX + 8 + contentW - 16 - kCardPad - kSettingsComboW;
    const int cby = kVisCardTopM3 + 52 + 1 * kDockVisRowPitch + (kDockVisRowPitch - kSettingsComboH) / 2;
    const int ly = cby + kSettingsComboH + 2 - settings_scroll_px_int(app);
    settings_paint_combo_list_popup(app, cr, cbx, ly, kSettingsComboW, kSettingsDdRowH, 3, kPanelWidthModeLabels,
                                    std::clamp(app.settings.panelWidthMode, 0, 2),
                                    app.panelWidthModeDropdownHoverRow, glassOv);
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

// ── Tier-2 damage tracking (clip-only phase) ────────────────────────────────
// Computes the precise dirty region for the coming paint by diffing a compact
// state snapshot. Painters rasterize clipped to it; commit damages exactly it.
// Buffers persist across frames, so per-SHM-buffer age tracking (frameSeq +
// 4-slot damage history) repaints the union of changes since each buffer was
// last shown. Anything uncertain degrades to full damage (today's behavior).

struct DmgSig {
  int tab = -999, subTab = -999, w = -1, h = -1;
  int matugen = -1, glassQ = -1, sideQ = -1;
  unsigned accQ = 0, outQ = 0, panelQ = 0;
  uint64_t scrollBits = 0;
  int monScroll = INT_MIN, sideScroll = INT_MIN;
  int sel = INT_MIN, dd = INT_MIN, ddHover = INT_MIN, dirty = INT_MIN;
  int nOut = -1;
  uint64_t zoomBits = 0, panXBits = 0, panYBits = 0, normBits = 0;
  std::string status;
  std::vector<std::string> outs;
  uint64_t hovMask = 0;
  int pillHov = INT_MIN;
  int scaleDrag = INT_MIN, hyprDrag = INT_MIN;
  int canvasDrag = INT_MIN, panArmed = INT_MIN;
  int sideHovD = INT_MIN, sideHovS = INT_MIN, sideSelD = INT_MIN, sideSelS = INT_MIN;
  std::string sideExp;
  int kind = -1;
  unsigned modals = 0;
  int mergedFlag = 0;
  std::string searchQ;
  int searchFocus = INT_MIN;
  int searchHover = INT_MIN;
  int searchSel = INT_MIN;
  int searchBarHover = INT_MIN;
};

namespace {
int dmg_no_damage() {
  static const int v = []() {
    const char* e = std::getenv("EH_NO_DAMAGE");
    return (e && e[0] != '\0' && e[0] != '0') ? 1 : 0;
  }();
  return v;
}

uint64_t dmg_dbits(double d) {
  uint64_t u = 0;
  static_assert(sizeof(u) == sizeof(d));
  std::memcpy(&u, &d, sizeof(d));
  return u;
}

uint32_t dmg_rgb(float r, float g, float b) {
  const auto q = [](float v) -> uint32_t {
    return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  return (q(r) << 16) | (q(g) << 8) | q(b);
}

// Sidebar row doc-Y for (defIdx, subIdx) under the CURRENT expand/scroll state.
bool dmg_sb_row_y(const App& app, int defIdx, int subIdx, int* y, int* h) {
  int curY = kSidebarTabBaseY;
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    if (i == defIdx && subIdx < 0) {
      *y = curY;
      *h = kSidebarTabH;
      return true;
    }
    curY += kSidebarTabPitchY;
    const bool expanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
    if (def.subCount > 0 && expanded) {
      for (int j = 0; j < def.subCount; ++j) {
        if (i == defIdx && j == subIdx) {
          *y = curY;
          *h = 36;
          return true;
        }
        curY += 36;
      }
    }
  }
  return false;
}

void dmg_sb_sel(const App& app, int* defIdx, int* subIdx) {  *defIdx = -1;
  *subIdx = -1;
  int di = 0;
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    const bool isExpanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
    bool active = false;
    if (def.isCategory && def.subCount > 0) {
      active = (app.activeTab == def.id);
      if (def.subTabIds) {
        for (int c = 0; c < def.subCount; ++c)
          if (def.subTabIds[c] == app.activeTab) active = true;
      }
    } else {
      active = (app.activeTab == def.id);
    }
    if (active && def.subTabIds) {
      for (int j = 0; j < def.subCount; ++j) {
        if (def.subTabIds[j] == app.activeTab) {
          *defIdx = di;
          *subIdx = j;
          return;
        }
      }
    }
    if (active) {
      *defIdx = di;
      *subIdx = -1;
      return;
    }
    (void)isExpanded;
    ++di;
  }
}

// Damage aging + committed snapshot (Tier 2).
int dmg_seq = 0;
int dmg_last_frame[2] = {-1, -1};
eh::wayland::DamageRegion dmg_hist[4];
int dmg_hist_seq[4] = {-1, -1, -1, -1};
DmgSig dmg_committed;
bool dmg_have_committed = false;
unsigned long long dmg_skips = 0;

void dmg_emit_rect(eh::wayland::DamageRegion& dmg, int x, int y, int w, int h) {
  if (w > 0 && h > 0) dmg.add_rect(x, y, w, h);
}

void dmg_split_fields(const std::string& s, std::vector<std::string>& out) {
  out.clear();
  size_t start = 0;
  for (;;) {
    const size_t p = s.find('\x1f', start);
    if (p == std::string::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, p - start));
    start = p + 1;
  }
}

void dmg_out_sig(const eh::settings_monitors::MonitorRow& r, std::string& out) {
  out.clear();
  out += r.name;
  out.push_back('\x1f');
  out += r.resolution;
  out.push_back('\x1f');
  out += r.refresh_rate;
  out.push_back('\x1f');
  out += r.position;
  out.push_back('\x1f');
  out += r.scale;
  out.push_back('\x1f');
  out += r.transform;
  out.push_back('\x1f');
  out += r.disabled ? '1' : '0';
  out.push_back('\x1f');
  out += r.bitdepth;
  out.push_back('\x1f');
  out += r.cm;
  out.push_back('\x1f');
  out += r.icc;
  out.push_back('\x1f');
  out += r.vrr;
  out.push_back('\x1f');
  out += r.mirror;
  out.push_back('\x1f');
  out += r.sdrbrightness;
  out.push_back('\x1f');
  out += r.sdrsaturation;
  out.push_back('\x1f');
  out += r.sdr_eotf;
  out.push_back('\x1f');
  out += r.supports_hdr;
  out.push_back('\x1f');
  out += r.supports_wide_color;
  out.push_back('\x1f');
  out += r.sdr_min_luminance;
  out.push_back('\x1f');
  out += r.sdr_max_luminance;
  out.push_back('\x1f');
  out += r.min_luminance;
  out.push_back('\x1f');
  out += r.max_luminance;
  out.push_back('\x1f');
  out += r.max_avg_luminance;
}

void dmg_compute_frame(App& app, int contentX, int contentW, double glassOv, double sideOv,
                       int mergedThumbs, DmgSig& cur, eh::wayland::DamageRegion& newDamage) {
  const int W = app.width;
  const int H = app.height;
  cur.tab = app.activeTab;
  cur.subTab = app.activeSubTab;
  cur.w = W;
  cur.h = H;
  cur.matugen = app.drawChromeMatugen ? 1 : 0;
  cur.glassQ = static_cast<int>(std::lround(glassOv * 1000.0));
  cur.sideQ = static_cast<int>(std::lround(sideOv * 1000.0));
  cur.accQ = dmg_rgb(static_cast<float>(app.drawChrome.accentR),
                     static_cast<float>(app.drawChrome.accentG),
                     static_cast<float>(app.drawChrome.accentB));
  cur.outQ = dmg_rgb(static_cast<float>(app.drawChrome.outlineR),
                     static_cast<float>(app.drawChrome.outlineG),
                     static_cast<float>(app.drawChrome.outlineB));
  cur.panelQ = dmg_rgb(static_cast<float>(app.drawChrome.panelFillR),
                       static_cast<float>(app.drawChrome.panelFillG),
                       static_cast<float>(app.drawChrome.panelFillB));
  cur.scrollBits = dmg_dbits(app.settingsScroll.current());
  cur.monScroll = app.settingsMonitorsScrollPx;
  cur.sideScroll = app.sidebarScrollPx;
  cur.kind = static_cast<int>(app.monitorsTab.kind);
  unsigned modals = 0;
  if (app.widgetPickerOpen) modals |= 1u << 0;
  if (app.iconThemePickerOpen) modals |= 1u << 1;
  if (app.wallpaperFolderPickerOpen) modals |= 1u << 2;
  if (app.defaultAppPickerOpen) modals |= 1u << 3;
  if (app.qtColorSchemeDropdownOpen) modals |= 1u << 4;
  if (wifi_password_prompt_visible(app)) modals |= 1u << 5;
  if (keyring_prompt_visible(app)) modals |= 1u << 6;
  if (vpn_add_dialog_visible(app)) modals |= 1u << 7;
  if (world_clock_popup_visible(app)) modals |= 1u << 8;
  if (app.hyprlandBezierOpen) modals |= 1u << 9;
  if (app.hyprlandAnimEditIdx >= 0) modals |= 1u << 10;
  if (app.hyprlandLayoutDropdownOpen) modals |= 1u << 11;
  cur.modals = modals;
  cur.mergedFlag = mergedThumbs > 0 ? 1 : 0;

  const int contentR[4] = {contentX, kContentTop, contentW, H - kContentTop - kSpacingL};
  const int sideR[4] = {kSpacingL, kContentTop, kSidebarW, H - kContentTop - kSpacingL};

  if (dmg_no_damage()) {
    newDamage.mark_full();
    return;
  }
  if (!dmg_have_committed) {
    newDamage.mark_full();
    return;
  }
  const DmgSig& prev = dmg_committed;
  if (cur.w != prev.w || cur.h != prev.h || cur.matugen != prev.matugen ||
      cur.glassQ != prev.glassQ || cur.sideQ != prev.sideQ || cur.accQ != prev.accQ ||
      cur.outQ != prev.outQ || cur.panelQ != prev.panelQ || cur.kind != prev.kind ||
      cur.modals != prev.modals || modals != 0) {
    newDamage.mark_full();
    return;
  }

  const bool needEmbedGroup = app.embedded && app.embedPresentT < 1.0f;
  const bool animatedContent = wallpaper_thumb_needs_followup_frame(app) ||
                               app.wallpaperHoverAnim.has_active() || app.settingsScroll.animating() ||
                               app.wifiToggle.animating() || app.settingsWidgetDragRepaintQueued ||
                               app.settingsScrollNeedsRedraw || needEmbedGroup;
  if (needEmbedGroup) {
    newDamage.mark_full();
    return;
  }

  dmg_sb_sel(app, &cur.sideSelD, &cur.sideSelS);
  {
    const double pyL = app.pointerY + settings_scroll_px(app);
    const int sbX = kSpacingL + 8;
    const int sbW = kSidebarW - 16;
    int hd = -1, hs = -1;
    int cy = kSidebarTabBaseY;
    for (int i = 0; i < kSidebarDefCount; ++i) {
      const auto& def = kSidebarDefs[i];
      if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
      if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
      if (app.pointerX >= sbX && app.pointerX < sbX + sbW && pyL >= cy && pyL < cy + kSidebarTabH) {
        hd = i;
        hs = -1;
      }
      cy += kSidebarTabPitchY;
      const bool expanded = app.sidebarExpanded.find(def.label) != app.sidebarExpanded.end();
      if (def.subCount > 0 && expanded) {
        for (int j = 0; j < def.subCount; ++j) {
          if (app.pointerX >= sbX && app.pointerX < sbX + sbW && pyL >= cy && pyL < cy + 36) {
            hd = i;
            hs = j;
          }
          cy += 36;
        }
      }
    }
    cur.sideHovD = hd;
    cur.sideHovS = hs;
  }
  cur.sideExp.clear();
  for (int i = 0; i < kSidebarDefCount; ++i) {
    if (app.sidebarExpanded.find(kSidebarDefs[i].label) != app.sidebarExpanded.end()) {
      cur.sideExp += kSidebarDefs[i].label;
      cur.sideExp.push_back('\x1f');
    }
  }
  cur.searchQ = app.settingsSearchQuery;
  cur.searchFocus = app.settingsSearchFocused ? 1 : 0;
  cur.searchHover = app.settingsSearchHoverRow;
  cur.searchSel = app.settingsSearchSelectedRow;
  cur.searchBarHover = app.settingsSearchBarHover ? 1 : 0;
  if (cur.searchQ != prev.searchQ || cur.searchFocus != prev.searchFocus ||
      cur.searchHover != prev.searchHover || cur.searchSel != prev.searchSel ||
      cur.searchBarHover != prev.searchBarHover) {
    dmg_emit_rect(newDamage, sideR[0], sideR[1], sideR[2], sideR[3]);
  }
  if (cur.sideScroll != prev.sideScroll || cur.sideExp != prev.sideExp ||
      cur.sideSelD != prev.sideSelD || cur.sideSelS != prev.sideSelS) {
    dmg_emit_rect(newDamage, sideR[0], sideR[1], sideR[2], sideR[3]);
  } else {
    if ((cur.sideHovD != prev.sideHovD || cur.sideHovS != prev.sideHovS) && cur.sideHovD >= 0) {
      int ry = 0, rh = 0;
      if (dmg_sb_row_y(app, cur.sideHovD, cur.sideHovS, &ry, &rh))
        dmg_emit_rect(newDamage, kSpacingL, ry, kSidebarW, rh);
    }
    if ((cur.sideHovD != prev.sideHovD || cur.sideHovS != prev.sideHovS) && prev.sideHovD >= 0) {
      int ry = 0, rh = 0;
      if (dmg_sb_row_y(app, prev.sideHovD, prev.sideHovS, &ry, &rh))
        dmg_emit_rect(newDamage, kSpacingL, ry, kSidebarW, rh);
    }
  }

  if (animatedContent || mergedThumbs > 0) {
    dmg_emit_rect(newDamage, contentR[0], contentR[1], contentR[2], contentR[3]);
    return;
  }

  // Non-monitors tabs: content depends on far more than (tab, subTab, scroll).
  // Child tabs live outside DmgSig (dock/panel/taskbar M3 singletons, plus
  // launcherChildTab, soundChildTab, appearanceChildTab, powerChildTab,
  // hyprlandChildTab, themesSubTab, wallpaperUiSubTab, ...), and so do all
  // Settings values (toggles/sliders/dropdowns) and header hover. Tracking
  // everything is fragile — any omission freezes the UI until resize (e.g.
  // dock Settings -> Widgets switch produced empty damage and skipped the
  // commit). Degrade to full damage here; keep fine-grained tracking only
  // for the monitors tab.
  if (cur.tab != 7) {
    newDamage.mark_full();
    return;
  }

  eh::settings_monitors_tab::MonitorsTabLayout monLay{};
  eh::settings_monitors_tab::compute_monitors_tab_layout(contentX, contentW, kContentTop,
                                                         settings_content_viewport_h(app), &monLay);
  const double pyLogical = app.pointerY + settings_scroll_px(app);
  cur.sel = app.monitorsSelectedIdx;
  cur.dd = app.monitorsActiveDd;
  cur.ddHover = app.monitorsDdHoverRow;
  cur.dirty = app.monitorsTab.dirty ? 1 : 0;
  cur.nOut = static_cast<int>(app.monitorsTab.outputs.size());
  cur.zoomBits = dmg_dbits(app.monitorsCanvasZoom);
  cur.panXBits = dmg_dbits(app.monitorsCanvasPanX);
  cur.panYBits = dmg_dbits(app.monitorsCanvasPanY);
  cur.normBits = dmg_dbits(app.settingsSliderDragNormT);
  cur.status = app.monitorsTab.status;
  cur.scaleDrag = app.monitorsScaleSliderDragIdx;
  cur.hyprDrag = app.monitorsHyprExtraSlider;
  cur.canvasDrag = app.monitorsCanvasDragIdx;
  cur.panArmed = app.monitorsCanvasPanArmed ? 1 : 0;
  cur.outs.clear();
  cur.outs.reserve(app.monitorsTab.outputs.size());
  {
    std::string joined;
    for (const auto& row : app.monitorsTab.outputs) {
      dmg_out_sig(row, joined);
      cur.outs.push_back(joined);
    }
  }

  const bool frameStructural =
      cur.nOut != prev.nOut || cur.sel != prev.sel || cur.tab != prev.tab ||
      cur.subTab != prev.subTab || cur.monScroll != prev.monScroll ||
      cur.scrollBits != prev.scrollBits;
  const int tbY = monLay.toolbar_y;
  const int tbH = monLay.toolbar_btn_h + 24;

  MonitorsFormRows mfr{};
  MonitorsFormGeom mfg{};
  bool haveForm = false;
  if (!app.monitorsTab.outputs.empty() && cur.nOut > 0) {
    size_t six = static_cast<size_t>(std::clamp(app.monitorsSelectedIdx, 0, cur.nOut - 1));
    if (six < app.monitorsTab.outputs.size()) {
      const auto& srow = app.monitorsTab.outputs[six];
      auto scit = app.monitorsTab.caps.find(srow.name);
      const eh::settings_monitors::OutputCaps* scaps =
          scit != app.monitorsTab.caps.end() ? &scit->second : nullptr;
      mfr = monitors_form_rows(app, srow, scaps);
      mfg = monitors_form_layout(monLay, contentX, contentW, mfr);
      haveForm = true;
    }
  }
  auto dmg_sec_rect = [&](const MonitorsSectionGeom& sec) {
    dmg_emit_rect(newDamage, sec.x, sec.y, sec.w, sec.h);
  };
  auto dmg_canvas_rect = [&]() {
    dmg_emit_rect(newDamage, monLay.canvas_x, monLay.canvas_y, monLay.canvas_w, monLay.canvas_h);
  };
  auto dmg_pills_rect = [&]() {
    dmg_emit_rect(newDamage, monLay.canvas_x, monLay.pills_y, monLay.canvas_w, kMonPillH);
  };
  auto dmg_full_form = [&]() {
    if (haveForm)
      dmg_emit_rect(newDamage, mfg.header.x, mfg.header.y, mfg.header.w,
                    mfg.bottom_y - mfg.header.y);
  };

  if (frameStructural) {
    dmg_emit_rect(newDamage, contentR[0], contentR[1], contentR[2], contentR[3]);
    return;
  }
  if (cur.outs != prev.outs) {
    if (!haveForm) {
      dmg_emit_rect(newDamage, contentR[0], contentR[1], contentR[2], contentR[3]);
    } else {
      const size_t selIx =
          static_cast<size_t>(std::clamp(app.monitorsSelectedIdx, 0, cur.nOut - 1));
      bool needCanvas = false, needPills = false, needHeader = false, needFullForm = false;
      bool secDisp = false, secScale = false, secColor = false, secHdr = false, secLum = false;
      bool fallbackFull = false;
      std::vector<std::string> cf, pf;
      for (size_t i = 0; i < cur.outs.size() && i < prev.outs.size(); ++i) {
        if (cur.outs[i] == prev.outs[i]) continue;
        dmg_split_fields(cur.outs[i], cf);
        dmg_split_fields(prev.outs[i], pf);
        if (cf.size() != pf.size() || cf.size() < 22) {
          fallbackFull = true;
          break;
        }
        const bool isSel = (i == selIx);
        for (size_t f = 0; f < cf.size(); ++f) {
          if (cf[f] == pf[f]) continue;
          switch (static_cast<int>(f)) {
            case 0:
              needCanvas = true;
              needPills = true;
              if (isSel) needHeader = true;
              break;
            case 1:
              needCanvas = true;
              if (isSel) secDisp = true;
              break;
            case 2:
              if (isSel) secDisp = true;
              break;
            case 3:
              needCanvas = true;
              break;
            case 4:
            case 5:
              needCanvas = true;
              if (isSel) secScale = true;
              break;
            case 6:
              needCanvas = true;
              if (isSel) {
                needHeader = true;
                needFullForm = true;
              }
              break;
            case 7:
              needCanvas = true;
              if (isSel) secColor = true;
              break;
            case 8:
            case 9:
              if (isSel) secColor = true;
              break;
            case 10:
              needCanvas = true;
              if (isSel) secDisp = true;
              break;
            case 11:
              if (isSel) needFullForm = true;
              break;
            case 12:
            case 13:
            case 14:
            case 16:
              if (isSel) secHdr = true;
              break;
            case 15:
              needCanvas = true;
              if (isSel) secHdr = true;
              break;
            case 17:
            case 18:
            case 19:
            case 20:
            case 21:
              if (isSel) secLum = true;
              break;
            default:
              fallbackFull = true;
              break;
          }
          if (fallbackFull) break;
        }
        if (fallbackFull) break;
      }
      if (fallbackFull) {
        dmg_emit_rect(newDamage, contentR[0], contentR[1], contentR[2], contentR[3]);
      } else {
        if (needCanvas) dmg_canvas_rect();
        if (needPills) dmg_pills_rect();
        if (needHeader) dmg_sec_rect(mfg.header);
        if (needFullForm) {
          dmg_full_form();
        } else {
          if (secDisp) dmg_sec_rect(mfg.display);
          if (secScale) dmg_sec_rect(mfg.scale);
          if (secColor && mfg.has_color) dmg_sec_rect(mfg.color);
          if (secHdr && mfg.has_hdr) dmg_sec_rect(mfg.hdr);
          if (secLum && mfg.has_luminance) dmg_sec_rect(mfg.luminance);
        }
      }
    }
  }

  if (cur.dirty != prev.dirty || cur.status != prev.status)
    dmg_emit_rect(newDamage, contentX, tbY, contentW, tbH);

  cur.hovMask = 0;
  if (point_in_rect(app.pointerX, pyLogical, monLay.toolbar_refresh_x, monLay.toolbar_y,
                    monLay.toolbar_btn_w, monLay.toolbar_btn_h))
    cur.hovMask |= (1ULL << 0);
  if (point_in_rect(app.pointerX, pyLogical, monLay.toolbar_portals_x, monLay.toolbar_y,
                    monLay.toolbar_portals_w, monLay.toolbar_btn_h))
    cur.hovMask |= (1ULL << 1);
  if (point_in_rect(app.pointerX, pyLogical, monLay.toolbar_apply_x, monLay.toolbar_y,
                    monLay.toolbar_btn_w, monLay.toolbar_btn_h))
    cur.hovMask |= (1ULL << 2);
  if (point_in_rect(app.pointerX, pyLogical, monLay.toolbar_revert_x, monLay.toolbar_y,
                    monLay.toolbar_btn_w, monLay.toolbar_btn_h))
    cur.hovMask |= (1ULL << 3);
  if (point_in_rect(app.pointerX, pyLogical, monLay.center_btn_x, monLay.aux_btn_y,
                    monLay.aux_btn_w, monLay.aux_btn_h))
    cur.hovMask |= (1ULL << 4);
  if (point_in_rect(app.pointerX, pyLogical, monLay.align_top_btn_x, monLay.aux_btn_y,
                    monLay.aux_btn_w, monLay.aux_btn_h))
    cur.hovMask |= (1ULL << 5);
  if (haveForm) {
    for (int k = 0; k < 9; ++k) {
      int cx = 0, cy = 0, cw = 0, ch = 0;
      if (monitors_dd_combo_geom(mfr, mfg, k, &cx, &cy, &cw, &ch) &&
          point_in_rect(app.pointerX, pyLogical, cx, cy, cw, ch))
        cur.hovMask |= (1ULL << (6 + k));
    }
    {
      int trX = 0, trY = 0, trW = 0;
      monitors_form_scale_track_geom(app, monLay, contentX, contentW, &trX, &trY, &trW);
      if (point_in_rect(app.pointerX, pyLogical, trX - 6, trY, trW + 12, 28))
        cur.hovMask |= (1ULL << 15);
    }
    if (mfg.has_hdr) {
      const struct {
        int row;
        int bit;
      } hsDefs[] = {{mfr.h_sdr_b, 16},
                    {mfr.h_sdr_s, 17},
                    {mfr.l_sdr_min, 18},
                    {mfr.l_sdr_max, 19},
                    {mfr.l_min, 20},
                    {mfr.l_max, 21},
                    {mfr.l_avg, 22}};
      for (const auto& hs : hsDefs) {
        if (hs.row < 0) continue;
        if (hs.bit >= 18 && !mfg.has_luminance) continue;
        const MonitorsSectionGeom* sec = (hs.bit < 18) ? &mfg.hdr : &mfg.luminance;
        int trX = 0, trY = 0, trW = 0;
        monitors_form_slider_track_geom_content(sec->content_y0, sec->x, sec->w, hs.row, &trX,
                                                &trY, &trW);
        if (point_in_rect(app.pointerX, pyLogical, trX - 6, trY, trW + 12, 28))
          cur.hovMask |= (1ULL << hs.bit);
      }
    }
    if (mfg.has_color && mfr.c_icc >= 0 && cur.nOut > 0) {
      size_t six = static_cast<size_t>(std::clamp(app.monitorsSelectedIdx, 0, cur.nOut - 1));
      if (six < app.monitorsTab.outputs.size()) {
        const auto& srow = app.monitorsTab.outputs[six];
        const int rowY = mfg.color.content_y0 + mfr.c_icc * kMonFormRowPitch;
        const int elY = rowY + (kMonFormRowPitch - kSettingsComboH) / 2;
        const int valX = mfg.color.x + kMonFormLabelColW + kMonFormValRailPx;
        const int valW = mfg.color.w - kMonFormLabelColW - kMonFormValRailPx - kCardPad;
        const bool hasIcc = !srow.icc.empty();
        const int pathW = valW - 80 - (hasIcc ? 64 : 0) - kSpacingM - (hasIcc ? kSpacingM : 0);
        const int browseX = valX + pathW + kSpacingM;
        if (point_in_rect(app.pointerX, pyLogical, browseX, elY, 80, kSettingsComboH))
          cur.hovMask |= (1ULL << 23);
        if (hasIcc &&
            point_in_rect(app.pointerX, pyLogical, browseX + 80 + kSpacingM, elY, 64,
                          kSettingsComboH))
          cur.hovMask |= (1ULL << 24);
      }
    }
  }
  {
    int px = monLay.canvas_x;
    cur.pillHov = -1;
    for (size_t i = 0; i < app.monitorsTab.outputs.size(); ++i) {
      const std::string& nm = app.monitorsTab.outputs[i].name;
      const int pillW = static_cast<int>(nm.size()) * 8 + 24;
      if (point_in_rect(app.pointerX, pyLogical, px, monLay.pills_y, pillW, kMonPillH))
        cur.pillHov = static_cast<int>(i);
      px += pillW + 8;
    }
  }

  const uint64_t hovChanged = cur.hovMask ^ prev.hovMask;
  if (hovChanged) {
    for (int b = 0; b < 25; ++b) {
      if (!(hovChanged & (1ULL << b))) continue;
      if (b < 4) {
        const int xs[4] = {monLay.toolbar_refresh_x, monLay.toolbar_portals_x, monLay.toolbar_apply_x,
                           monLay.toolbar_revert_x};
        dmg_emit_rect(newDamage, xs[b] - 2, tbY - 2, monLay.toolbar_btn_w + 4,
                      monLay.toolbar_btn_h + 4);
      } else if (b < 6) {
        const int xs[2] = {monLay.center_btn_x, monLay.align_top_btn_x};
        dmg_emit_rect(newDamage, xs[b - 4] - 2, monLay.aux_btn_y - 2, monLay.aux_btn_w + 4,
                      monLay.aux_btn_h + 4);
      } else if (b < 15) {
        if (!haveForm) continue;
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (monitors_dd_combo_geom(mfr, mfg, b - 6, &cx, &cy, &cw, &ch))
          dmg_emit_rect(newDamage, cx - 2, cy - 2, cw + 4, ch + 4);
      } else if (b == 15) {
        if (!haveForm) continue;
        int trX = 0, trY = 0, trW = 0;
        monitors_form_scale_track_geom(app, monLay, contentX, contentW, &trX, &trY, &trW);
        dmg_emit_rect(newDamage, trX - 8, trY - 14, trW + 76, 56);
      } else if (b < 23) {
        if (!haveForm) continue;
        const int kind = b - 16;
        const MonitorsSectionGeom* sec = (kind < 2) ? &mfg.hdr : &mfg.luminance;
        int row = -1;
        if (kind == 0) row = mfr.h_sdr_b;
        else if (kind == 1) row = mfr.h_sdr_s;
        else if (kind == 2) row = mfr.l_sdr_min;
        else if (kind == 3) row = mfr.l_sdr_max;
        else if (kind == 4) row = mfr.l_min;
        else if (kind == 5) row = mfr.l_max;
        else if (kind == 6) row = mfr.l_avg;
        if (row >= 0) {
          int trX = 0, trY = 0, trW = 0;
          monitors_form_slider_track_geom_content(sec->content_y0, sec->x, sec->w, row, &trX,
                                                  &trY, &trW);
          dmg_emit_rect(newDamage, trX - 8, trY - 70, trW + 200, 130);
        }
      }
    }
  }
  if (cur.pillHov != prev.pillHov) {
    for (int pi : {cur.pillHov, prev.pillHov}) {
      if (pi < 0 || static_cast<size_t>(pi) >= app.monitorsTab.outputs.size()) continue;
      int px = monLay.canvas_x;
      for (int i = 0; i < pi; ++i)
        px += static_cast<int>(app.monitorsTab.outputs[static_cast<size_t>(i)].name.size()) * 8 +
              24 + 8;
      const int pw =
          static_cast<int>(app.monitorsTab.outputs[static_cast<size_t>(pi)].name.size()) * 8 + 24;
      dmg_emit_rect(newDamage, px - 2, monLay.pills_y - 2, pw + 4, kMonPillH + 4);
    }
  }

  if (cur.zoomBits != prev.zoomBits || cur.panXBits != prev.panXBits ||
      cur.panYBits != prev.panYBits || cur.canvasDrag != prev.canvasDrag ||
      cur.panArmed != prev.panArmed) {
    dmg_emit_rect(newDamage, monLay.canvas_x, monLay.canvas_y, monLay.canvas_w, monLay.canvas_h);
  }

  if (cur.dd != prev.dd || cur.ddHover != prev.ddHover) {
    if (haveForm) {
      for (int kk : {cur.dd, prev.dd}) {
        if (kk < 0) continue;
        int cx = 0, cy = 0, cw = 0, ch = 0;
        if (!monitors_dd_combo_geom(mfr, mfg, kk, &cx, &cy, &cw, &ch)) continue;
        dmg_emit_rect(newDamage, cx - 2, cy - 2, cw + 4, ch + 4);
        int nrows = 0;
        size_t six = static_cast<size_t>(std::clamp(app.monitorsSelectedIdx, 0, cur.nOut - 1));
        if (six < app.monitorsTab.outputs.size()) {
          const auto& srow = app.monitorsTab.outputs[six];
          auto scit = app.monitorsTab.caps.find(srow.name);
          const eh::settings_monitors::OutputCaps* scaps =
              scit != app.monitorsTab.caps.end() ? &scit->second : nullptr;
          if (kk == 0 && scaps) nrows = static_cast<int>(scaps->resolutions.size());
          else if (kk == 1 && scaps) {
            auto it = scaps->resolution_refresh_hz.find(srow.resolution);
            if (it != scaps->resolution_refresh_hz.end()) nrows = static_cast<int>(it->second.size());
          } else if (kk == 2)
            nrows = 8;
          else if (kk == 3)
            nrows = 2;
          else if (kk == 4)
            nrows = app.monitorsTab.kind == CompositorKind::Mango ? 2 : 3;
          else if (kk == 5)
            nrows = kMonitorCmCount;
          else if (kk >= 6 && kk <= 8)
            nrows = 3;
        }
        if (nrows > 0) {
          const int ly = monitors_dd_popup_list_doc_top_y(cy, ch, nrows, settings_scroll_px_int(app),
                                                          app.height);
          dmg_emit_rect(newDamage, cx, ly - settings_scroll_px_int(app), cw,
                        nrows * kSettingsDdRowH + 4);
        }
      }
    } else {
      if (haveForm)
        dmg_emit_rect(newDamage, mfg.header.x, mfg.header.y, mfg.header.w,
                      mfg.bottom_y - mfg.header.y);
    }
  }

  if (cur.normBits != prev.normBits || cur.scaleDrag != prev.scaleDrag ||
      cur.hyprDrag != prev.hyprDrag) {
    if (!haveForm) {
    } else if (cur.hyprDrag >= 0 || prev.hyprDrag >= 0) {
      const int k = cur.hyprDrag >= 0 ? cur.hyprDrag : prev.hyprDrag;
      if (k < 2)
        dmg_emit_rect(newDamage, mfg.hdr.x, mfg.hdr.y - 70, mfg.hdr.w, mfg.hdr.h + 70);
      else
        dmg_emit_rect(newDamage, mfg.luminance.x, mfg.luminance.y - 70, mfg.luminance.w,
                      mfg.luminance.h + 70);
    } else if (cur.scaleDrag >= 0 || prev.scaleDrag >= 0) {
      dmg_emit_rect(newDamage, mfg.scale.x, mfg.scale.y - 70, mfg.scale.w, mfg.scale.h + 70);
    } else {
      dmg_emit_rect(newDamage, mfg.header.x, mfg.header.y, mfg.header.w,
                    mfg.bottom_y - mfg.header.y);
    }
  }
}
}  // namespace

void draw(App& app) {
  if (!app.surface) {    debug_log("settings", "WARN draw: no surface, skipping");
    return;
  }
  // Tier-1 coalescing: a draw arriving <8ms after the previous painted frame,
  // with no intervening user input, no size change and no active
  // animation/pending work, is deferred to the next frame callback (~5us
  // instead of ~5ms). Input uses strict < so same-millisecond presses still
  // paint; the deferred frame always repaints via pendingRedraw, bounding any
  // staleness to one frame tick. Service/config/duplicate bursts collapse.
  {
    const std::uint64_t nowMs = eh::shell::now_mono_ms();
    const bool animated = wallpaper_thumb_needs_followup_frame(app) ||
                          (app.embedded && app.embedAnim.has_active()) ||
                          app.wallpaperHoverAnim.has_active() || app.settingsScroll.animating() ||
                          app.wifiToggle.animating() || app.settingsScrollNeedsRedraw ||
                          app.settingsWidgetDragRepaintQueued || app.pendingRedraw;
    if (app.lastDrawMonoMs != 0 && nowMs - app.lastDrawMonoMs < 8 &&
        app.lastInputMonoMs < app.lastDrawMonoMs && !animated &&
        app.width == app.lastPaintedW && app.height == app.lastPaintedH) {
      ++app.coalescedSkips;
      app.pendingRedraw = true;
      schedule_settings_surface_frame(app);
      if (app.wl.display()) (void)wl_display_flush(app.wl.display());
      return;
    }
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
  if (app.activeTab == 3) app.activeTab = 7;  // UI Layout page removed; fall back to Monitors.
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
  long long us_sidebar = 0;
  unsigned sbHits = 0, sbMiss = 0;
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
  app.lastDrawMonoMs = eh::shell::now_mono_ms();
  app.lastPaintedW = app.width;
  app.lastPaintedH = app.height;
  debug_log("settings", "draw: paint slot=%s needEmbedGroup=%d embedPresentT=%.3f", gpu_path ? "vk" : std::to_string(paintBi).c_str(), (app.embedded && app.embedPresentT < 1.0f) ? 1 : 0, app.embedPresentT);
  cairo_t* const cr = gpu_path ? app.glRaster.cairo() : app.buf[static_cast<size_t>(paintBi)].cairo();
  cairo_save(cr);

  const bool needEmbedGroup = app.embedded && app.embedPresentT < 1.0f;
  // No explicit CLEAR pass: the fullscreen paint_src_bg() SOURCE fill below
  // overwrites every pixel, so clearing first just burns a 13.9MB pass.
  // (Groups start transparent on their own.)
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (needEmbedGroup) {
    cairo_push_group_with_content(cr, CAIRO_CONTENT_COLOR_ALPHA);
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
  else if (app.activeTab == 1) settings_clamp_panel_scroll_px(app);
  else if (app.activeTab == 11) settings_clamp_taskbar_scroll_px(app);
  else if (app.activeTab == 46) settings_clamp_accounts_scroll_px(app);
  else if (app.activeTab == 48) settings_clamp_autostart_scroll_px(app);
  else if (app.activeTab == 47) app.settingsBluetoothScrollPx = std::max(0, app.settingsBluetoothScrollPx);
  else if (app.activeTab >= 20 && app.activeTab <= 26) settings_clamp_mango_scroll_px(app);
  else if (app.activeTab == 33) settings_clamp_hyprland_scroll_px(app);
  else if (app.activeTab == 9) settings_clamp_launcher_scroll_px(app);
  if (app.activeTab == 2) settings_clamp_appearance_scroll_px(app);
  if (app.activeTab == 30) settings_clamp_time_scroll_px(app);
  if (app.activeTab == 6) settings_clamp_wallpaper_scroll_px(app);
  if (app.activeTab == 7) settings_clamp_monitors_scroll_px(app);
  if (app.activeTab == 44) settings_clamp_icons_scroll_px(app);
  if (app.activeTab == 45) settings_clamp_themes_scroll_px(app);
  if (app.activeTab == 50) app.settingsColorThemesScrollPx = std::max(0, app.settingsColorThemesScrollPx);
   int dockPanelScrollPxPaint = app.activeTab == 0   ? app.settingsDockScrollPx
                                 : app.activeTab == 1 ? app.settingsPanelScrollPx
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
  : app.activeTab == 30 ? app.settingsTimeScrollPx
  : app.activeTab == 46 ? app.settingsAccountsScrollPx
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

  DmgSig dmgCur;
  eh::wayland::DamageRegion dmgNew;
  dmg_compute_frame(app, contentX, contentW, glassOv, settingsSidebarOv, mergedThumbs, dmgCur,
                    dmgNew);
  eh::wayland::DamageRegion dmgNeed;
  int dmgS = -1;
  if (!vk_path) {
    if (dmg_last_frame[paintBi] < 0) {
      dmgNeed.mark_full();
    } else {
      dmgNeed.union_with(dmgNew);
      for (int k = 0; k < 4; ++k) {
        if (dmg_hist_seq[k] > dmg_last_frame[paintBi]) dmgNeed.union_with(dmg_hist[k]);
      }
      if (dmg_seq + 1 - dmg_last_frame[paintBi] > 4) dmgNeed.mark_full();
    }
  } else {
    dmgNeed = dmgNew;
  }
  if (dmgNeed.empty()) {
    ++dmg_skips;
    app.pendingRedraw = false;
    cairo_restore(cr);
    return;
  }
  dmgS = ++dmg_seq;
  dmg_hist[dmgS % 4] = dmgNew;
  dmg_hist_seq[dmgS % 4] = dmgS;
  dmgNeed.expand(2, app.width, app.height);
  dmgNeed.clip_to(app.width, app.height);
  if (!dmgNeed.full()) {
    for (const auto& sp : dmgNeed.spans()) cairo_rectangle(cr, sp.x, sp.y, sp.w, sp.h);
    cairo_clip(cr);
  }

  paint_src_bg(app, cr, 0.78 * glassOv);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  const auto t_sb0 = SettingsBenchClock::now();
  // Retained sidebar: bg panel + per-row visuals are pure functions of
  // (geometry, selection, hover, theme, scale). Same pattern as the monitors
  // tab caches; rows keyed by item identity so expand/collapse just reuses.
  struct SbCache {
    eh::settings::retain::SurfEntry bg{};
    int bgW = 0, bgH = 0;
    uint64_t bgKey = 0;
    std::unordered_map<uint64_t, eh::settings::retain::SurfEntry> rows;
    unsigned long long bytes = 0;
  };
  static SbCache s_sbCache;
  const double sbDs = eh::settings::retain::device_scale(cr);
  const int sbDsQ = sbDs > 0.0 ? static_cast<int>(std::lround(sbDs * 128.0)) : 128;
  app.paintDamageCount = 0;
  app.paintDamageActive = false;
  if (!dmgNeed.full()) {
    app.paintDamageActive = true;
    int dmgFill = 0;
    for (const auto& sp : dmgNeed.spans()) {
      if (dmgFill >= App::kPaintDamageMax) break;
      app.paintDamage[dmgFill].x = sp.x;
      app.paintDamage[dmgFill].y = sp.y;
      app.paintDamage[dmgFill].w = sp.w;
      app.paintDamage[dmgFill].h = sp.h;
      ++dmgFill;
    }
    app.paintDamageCount = dmgFill;
  }
  bool sbSkip = false;
  if (!dmgNeed.full()) {
    sbSkip = true;
    for (const auto& sp : dmgNeed.spans()) {
      if (sp.x < sidebarX + sidebarW && sp.x + sp.w > sidebarX &&
          sp.y < kContentTop + sidebarViewH && sp.y + sp.h > kContentTop) {
        sbSkip = false;
        break;
      }
    }
  }
  float sbAr = static_cast<float>(Theme::AccR);
  float sbAg = static_cast<float>(Theme::AccG);
  float sbAb = static_cast<float>(Theme::AccB);
  if (app.drawChromeMatugen) {
    sbAr = app.drawChrome.accentR;
    sbAg = app.drawChrome.accentG;
    sbAb = app.drawChrome.accentB;
  }
  const uint64_t sbAccQ = (static_cast<uint64_t>(eh::settings::retain::q8(sbAr)) << 16) |
                          (static_cast<uint64_t>(eh::settings::retain::q8(sbAg)) << 8) |
                          static_cast<uint64_t>(eh::settings::retain::q8(sbAb));
  uint64_t sbExpMask = 0;
  for (int ei = 0; ei < kSidebarDefCount; ++ei) {
    if (app.sidebarExpanded.find(kSidebarDefs[ei].label) != app.sidebarExpanded.end())
      sbExpMask |= (1ULL << ei);
  }
  if (!sbSkip) {
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
    const float bgA = static_cast<float>(0.78 * settingsSidebarOv);
    uint64_t bkey = 1469598103934665603ULL;
    bkey = eh::settings::retain::mix(bkey, static_cast<uint64_t>(static_cast<uint32_t>(sbW)));
    bkey = eh::settings::retain::mix(bkey, static_cast<uint64_t>(static_cast<uint32_t>(sbH)));
    bkey = eh::settings::retain::mix(bkey, static_cast<uint64_t>(static_cast<uint32_t>(sbDsQ)));
    bkey = eh::settings::retain::mix(bkey, static_cast<uint64_t>(app.drawChromeMatugen ? 1 : 0));
    bkey = eh::settings::retain::mix(bkey, eh::settings::retain::q8(bgR));
    bkey = eh::settings::retain::mix(bkey, eh::settings::retain::q8(bgG));
    bkey = eh::settings::retain::mix(bkey, eh::settings::retain::q8(bgB));
    bkey = eh::settings::retain::mix(bkey, eh::settings::retain::q8(bgA));
    const int bsw = std::max(1, static_cast<int>(std::ceil(sbW * sbDs)));
    const int bsh = std::max(1, static_cast<int>(std::ceil(sbH * sbDs)));
    if (!(s_sbCache.bg.surf && s_sbCache.bgKey == bkey && s_sbCache.bgW == static_cast<int>(sbW) &&
          s_sbCache.bgH == static_cast<int>(sbH) && s_sbCache.bg.sw == bsw &&
          s_sbCache.bg.sh == bsh)) {
      eh::settings::retain::destroy(s_sbCache.bg, &s_sbCache.bytes);
      s_sbCache.bg.sw = bsw;
      s_sbCache.bg.sh = bsh;
      s_sbCache.bg.key = bkey;
      s_sbCache.bgW = static_cast<int>(sbW);
      s_sbCache.bgH = static_cast<int>(sbH);
      eh::settings::retain::render(s_sbCache.bg, bsw, bsh, sbDs, bkey, [&](cairo_t* t) {
        m3::Box sbBox;
        sbBox.setColor(bgR, bgG, bgB, bgA);
        sbBox.setRadius(18.0f);
        sbBox.setGeometry(0, 0, static_cast<float>(sbW), static_cast<float>(sbH));
        sbBox.setGlassy(true);
        sbBox.paint(t);
      });
      s_sbCache.bytes +=
          static_cast<unsigned long long>(bsw) * static_cast<unsigned long long>(bsh) * 4ULL;
      ++sbMiss;
    } else {
      ++sbHits;
    }
    eh::settings::retain::blit(cr, s_sbCache.bg, sbX, sbY, sbDs);
  }

  cairo_round_rect(cr, sbX, sbY, sbW, sbH, 18.0);
  cairo_clip(cr);
  cairo_translate(cr, 0.0, -static_cast<double>(app.sidebarScrollPx));

  auto draw_sidebar_tab = [&](cairo_t* dst, int ty, int indentX, bool sel, bool hover,
                              const char* label, const char* glyph, int ox = 0, int oy = 0) {
    const int tabX = sidebarX + 8 + indentX + ox;
    const int tabW = sidebarW - 16 - indentX;
    const int tabH = 40;
    const int tyy = ty + oy;
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
      box.setGeometry(static_cast<float>(tabX), static_cast<float>(tyy),
                      static_cast<float>(tabW), static_cast<float>(tabH));
      box.setGlassy(true);
      box.paint(dst);
    }
    material_symbols_draw_glyph(dst, static_cast<double>(tabX + 18), static_cast<double>(tyy + 20), 18.0, glyph,
                               Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(dst, tabX + 44, tyy + 26, label, 14, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  };

  auto draw_sidebar_subtab = [&](cairo_t* dst, int ty, bool sel, bool hover, const char* label,
                                 const char* glyph, int ox = 0, int oy = 0) {
    const int tabX = sidebarX + kSidebarSubTabIndentX + ox;
    const int tabW = sidebarW - kSidebarSubTabIndentX - 16;
    const int tabH = 36;
    const int tyy = ty + oy;
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
      box.setGeometry(static_cast<float>(tabX), static_cast<float>(tyy),
                      static_cast<float>(tabW), static_cast<float>(tabH));
      box.setGlassy(true);
      box.paint(dst);
    }
    if (glyph)
      material_symbols_draw_glyph(dst, static_cast<double>(tabX + 14), static_cast<double>(tyy + 18), 16.0, glyph,
                                  Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
    settings_show_text(dst, tabX + 32, tyy + 23, label, 12, CAIRO_FONT_WEIGHT_BOLD, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  };

  // Row key: identity + visual state + theme + scale. Position-free (blitted
  // at the live ty), so expand/collapse and scroll reuse entries.
  auto sb_row_key = [&](int sub, int di, int sj, bool sel, bool hov, int w, int h) -> uint64_t {
    uint64_t k = 1469598103934665603ULL;
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(0x5B1D));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(sub));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(di));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(sj + 1));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(sel ? 1 : 0));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(hov ? 1 : 0));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(app.drawChromeMatugen ? 1 : 0));
    k = eh::settings::retain::mix(k, (sbAccQ >> 16) & 0xFFULL);
    k = eh::settings::retain::mix(k, (sbAccQ >> 8) & 0xFFULL);
    k = eh::settings::retain::mix(k, sbAccQ & 0xFFULL);
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(static_cast<uint32_t>(w)));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(static_cast<uint32_t>(h)));
    k = eh::settings::retain::mix(k, static_cast<uint64_t>(static_cast<uint32_t>(sbDsQ)));
    return k;
  };

  auto sb_row_cached = [&](int ty, int bx, int w, int h, uint64_t key,
                           auto&& paintAtOrigin) {
    auto it = s_sbCache.rows.find(key);
    const int sw = std::max(1, static_cast<int>(std::ceil(w * sbDs)));
    const int sh = std::max(1, static_cast<int>(std::ceil(h * sbDs)));
    if (it != s_sbCache.rows.end() && it->second.surf && it->second.sw == sw &&
        it->second.sh == sh) {
      // Blit at the live position (clip+scroll translate are active on cr).
      eh::settings::retain::blit(cr, it->second, static_cast<double>(bx),
                                 static_cast<double>(ty), sbDs);
      ++sbHits;
      return;
    }
    if (s_sbCache.rows.size() >= 128) {
      for (auto& kv : s_sbCache.rows) eh::settings::retain::destroy(kv.second, &s_sbCache.bytes);
      s_sbCache.rows.clear();
    }
    eh::settings::retain::SurfEntry e;
    eh::settings::retain::render(e, sw, sh, sbDs, key,
                                [&](cairo_t* t) { paintAtOrigin(t); });
    s_sbCache.bytes +=
        static_cast<unsigned long long>(sw) * static_cast<unsigned long long>(sh) * 4ULL;
    eh::settings::retain::blit(cr, e, static_cast<double>(bx), static_cast<double>(ty), sbDs);
    ++sbMiss;
    s_sbCache.rows.emplace(key, e);
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

  const int sbViewTop = app.sidebarScrollPx + kContentTop;
  const int sbViewBot = sbViewTop + sidebarViewH;
  if (app.settingsSearchQuery.empty()) {
  int currentY = kSidebarTabBaseY;
  auto sb_row_visible = [&](int y, int h) -> bool {
    return y < sbViewBot && y + h > sbViewTop;
  };
  for (int i = 0; i < kSidebarDefCount; ++i) {
    const auto& def = kSidebarDefs[i];
    if (def.id == 16 && app.monitorsTab.kind != CompositorKind::Mango) continue;
    if (def.id == 33 && app.monitorsTab.kind != CompositorKind::Hyprland) continue;
    const bool isExpanded = (sbExpMask & (1ULL << i)) != 0;
    const bool hasChildren = def.subCount > 0;
    const bool isCategory = def.isCategory;
    const bool isSelected = isCategory ? (hasChildren ? (app.activeTab == def.id || subtab_active(def)) : false)
                                        : (app.activeTab == def.id);
    const bool tabHover = sidebar_hit_test(currentY, kSidebarTabH);
    if (sb_row_visible(currentY, kSidebarTabH)) {
    {
      const int tabX = sidebarX + 8;
      const int tabW = sidebarW - 16;
      const uint64_t key = sb_row_key(0, i, -1, isSelected, tabHover, tabW, kSidebarTabH);
      sb_row_cached(currentY, tabX, tabW, kSidebarTabH, key, [&](cairo_t* t) {
        draw_sidebar_tab(t, currentY, 0, isSelected, tabHover, def.label, def.glyph, -tabX,
                         -currentY);
      });
    }
    }
    currentY += kSidebarTabPitchY;
    if (hasChildren && isExpanded) {
      for (int j = 0; j < def.subCount; ++j) {
        const bool subSel = def.subTabIds ? (app.activeTab == def.subTabIds[j])
                                          : (app.activeTab == def.id && app.activeSubTab == j);
        const bool subHover = sidebar_hit_test(currentY, 36);
        const char* subGlyph = def.subGlyphs ? def.subGlyphs[j] : nullptr;
        const int subX = sidebarX + kSidebarSubTabIndentX;
        const int subW = sidebarW - kSidebarSubTabIndentX - 16;
        const int subTy = currentY;
        if (sb_row_visible(subTy, 36)) {
        const uint64_t key = sb_row_key(1, i, j, subSel, subHover, subW, 36);
        sb_row_cached(subTy, subX, subW, 36, key, [&](cairo_t* t) {
          draw_sidebar_subtab(t, subTy, subSel, subHover, def.subLabels[j], subGlyph, -subX,
                              -subTy);
        });
        }
        currentY += 36;
      }
    }
  }
  } else {
  // ── Search results (flat, every tab + setting) ──
  const std::vector<int> searchRes = settings_search_collect(app.settingsSearchQuery, app.monitorsTab.kind);
  const int resN = static_cast<int>(searchRes.size());
  if (app.settingsSearchSelectedRow >= resN && resN > 0)
    const_cast<App&>(app).settingsSearchSelectedRow = resN - 1;
  if (app.settingsSearchHoverRow >= resN)
    const_cast<App&>(app).settingsSearchHoverRow = -1;
  if (resN == 0) {
    settings_show_text(cr, sidebarX + 24, kSidebarTabBaseY + 30, "No matching settings",
                       13, 700, Theme::TextR, Theme::TextG, Theme::TextB, 0.75);
    settings_show_text(cr, sidebarX + 24, kSidebarTabBaseY + 50, "Try a different search term",
                       11, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45);
  } else {
    for (int ri = 0; ri < resN; ++ri) {
      const SettingsSearchEntry& e = kSettingsSearchEntries[searchRes[static_cast<size_t>(ri)]];
      const int ry = kSidebarTabBaseY + ri * kSettingsSearchResultPitch;
      if (ry + kSettingsSearchResultH < sbViewTop || ry > sbViewBot) continue;
      const int rx = sidebarX + 8;
      const int rw = sidebarW - 16;
      const bool sel = (ri == app.settingsSearchSelectedRow);
      const bool hov = (ri == app.settingsSearchHoverRow) ||
                       (app.settingsSearchHoverRow < 0 && ri == app.settingsSearchSelectedRow && app.settingsSearchFocused);
      {
        m3::Box box;
        float r, g, b, a;
        if (app.drawChromeMatugen) { r = app.drawChrome.accentR; g = app.drawChrome.accentG; b = app.drawChrome.accentB; }
        else { r = static_cast<float>(Theme::AccR); g = static_cast<float>(Theme::AccG); b = static_cast<float>(Theme::AccB); }
        if (sel || hov) a = sel ? 0.20f : 0.12f;
        else { r = 0; g = 0; b = 0; a = 0; }
        if (e.tab == app.activeTab && !sel && !hov) { a = 0.07f; }
        box.setColor(r, g, b, a);
        box.setRadius(8.0f);
        box.setGeometry(static_cast<float>(rx), static_cast<float>(ry),
                        static_cast<float>(rw), static_cast<float>(kSettingsSearchResultH));
        box.setGlassy(true);
        box.paint(cr);
      }
      material_symbols_draw_glyph(cr, static_cast<double>(rx + 18), static_cast<double>(ry + 24),
                                  18.0, e.glyph && e.glyph[0] ? e.glyph : "tune",
                                  Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
      settings_show_text(cr, rx + 42, ry + 21, e.title, 13, 700,
                         Theme::TextR, Theme::TextG, Theme::TextB, 0.95);
      settings_show_text(cr, rx + 42, ry + 39, e.section, 11, 400,
                         Theme::TextR, Theme::TextG, Theme::TextB, 0.5);
    }
  }
  }
  // Undo scroll so the search bar stays fixed on top of scrolled rows.
  cairo_translate(cr, 0.0, static_cast<double>(app.sidebarScrollPx));
  // ── Global search bar (fixed, top-left, above the scroll region) ──
  {
    int sx = 0, sy = 0, sw = 0, sh = 0;
    settings_search_bar_geom(&sx, &sy, &sw, &sh);
    const bool sFocused = app.settingsSearchFocused;
    const bool sHover = app.settingsSearchBarHover;
    {
      m3::Box box;
      float r, g, b, a;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.accentR; g = app.drawChrome.accentG; b = app.drawChrome.accentB;
      } else {
        r = static_cast<float>(Theme::AccR); g = static_cast<float>(Theme::AccG); b = static_cast<float>(Theme::AccB);
      }
      if (sFocused) a = 0.22f;
      else if (sHover) a = 0.12f;
      else { r = 1.0f; g = 1.0f; b = 1.0f; a = 0.07f; }
      box.setColor(r, g, b, a);
      box.setRadius(10.0f);
      box.setGeometry(static_cast<float>(sx), static_cast<float>(sy),
                      static_cast<float>(sw), static_cast<float>(sh));
      box.setGlassy(true);
      box.paint(cr);
    }
    material_symbols_draw_glyph(cr, static_cast<double>(sx + 18),
                                static_cast<double>(sy) + static_cast<double>(sh) * 0.5 + 0.5,
                                18.0, "search", Theme::TextR, Theme::TextG, Theme::TextB,
                                app.settingsSearchQuery.empty() ? 0.45 : 0.9);
    const int textX = sx + 44;
    const int textBaselineY = sy + (sh + 15) / 2;
    cairo_save(cr);
    const int clearW = !app.settingsSearchQuery.empty() ? 30 : 0;
    cairo_rectangle(cr, textX, sy + 5, sw - (textX - sx) - clearW - 8, sh - 10);
    cairo_clip(cr);
    if (app.settingsSearchQuery.empty()) {
      settings_show_text(cr, textX, textBaselineY, "Search settings",
                         13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.42);
    } else {
      settings_show_text(cr, textX, textBaselineY, app.settingsSearchQuery.c_str(),
                         13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.92);
    }
    if (sFocused) {
      const int tw = settings_search_text_width_px(app.settingsSearchQuery.c_str(), 13, 400);
      const double caretX = static_cast<double>(textX + tw) + 1.5;
      cairo_set_line_width(cr, 1.6);
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.85);
      cairo_move_to(cr, caretX, static_cast<double>(sy) + 9.0);
      cairo_line_to(cr, caretX, static_cast<double>(sy + sh) - 9.0);
      cairo_stroke(cr);
    }
    cairo_restore(cr);
    if (!app.settingsSearchQuery.empty()) {
      const int cbS = 22;
      const int cbX = sx + sw - cbS - 7;
      const int cbY = sy + (sh - cbS) / 2;
      const bool cbHover = app.pointerX >= cbX && app.pointerX < cbX + cbS &&
                           app.pointerY >= cbY && app.pointerY < cbY + cbS;
      m3::Box cb;
      if (cbHover) cb.setColor(1, 1, 1, 0.16f);
      else cb.setColor(1, 1, 1, 0.08f);
      cb.setRadius(static_cast<float>(cbS) / 2.0f);
      cb.setGeometry(static_cast<float>(cbX), static_cast<float>(cbY),
                     static_cast<float>(cbS), static_cast<float>(cbS));
      cb.setGlassy(true);
      cb.paint(cr);
      material_symbols_draw_glyph(cr, cbX + cbS * 0.5, cbY + cbS * 0.5 + 0.5, 14.0, "close",
                                  Theme::TextR, Theme::TextG, Theme::TextB, 0.8);
    }
  }
  }
  if (!sbSkip) cairo_restore(cr);
  us_sidebar = settings_bench_us(t_sb0, SettingsBenchClock::now());

  paint_src_glass_hi(app, cr, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, contentX - kSpacingS + 0.5, kContentTop);
  cairo_line_to(cr, contentX - kSpacingS + 0.5, app.height - kSpacingL);
  cairo_stroke(cr);

  const uint64_t contentDrawCountBefore = g_settings_content_draw_count;

  if (app.activeTab == 0 || app.activeTab == 1 || app.activeTab == 11) {
    const std::unordered_set<std::string>& widgetDisabled =
        app.activeTab == 0 ? app.settings.dockWidgetSlotsDisabled
        : app.activeTab == 1 ? app.settings.panelWidgetSlotsDisabled
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

  if (app.activeTab == 1) {
    paint_panel_tab(app, cr, contentX, contentW, glassOv, dockMatA, paintPointerYOffset, 0);
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
    if (time_zone_picker_visible(app)) {
      paint_time_zone_picker(app, cr);
    }
  } else if (app.activeTab == 31) {
    {
      const int viewH = app.height - kContentTop - kSpacingL;
      const int maxScroll = std::max(0, keyboard_tab::content_bottom(app) + kSpacingL - viewH);
      app.settingsKeyboardScrollPx = std::clamp(app.settingsKeyboardScrollPx, 0, maxScroll);
    }
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
      const int cbh = keyboard_tab::kComboH;
      const int behaviorTop = keyboard_tab::behavior_card_top(app);
      int ddX = rightRail - keyboard_tab::kComboW;
      int ddW = keyboard_tab::kComboW;
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
        ddY = behaviorTop + 52 + 0 * kSliderRowH + 40 + cbh + 2;
        nItems = kCapsLockCount;
        labels = kCapsLockLabels;
      } else if (app.settingsKeyboardDdKind == 3) {
        ddY = behaviorTop + 52 + 1 * kSliderRowH + 40 + cbh + 2;
        nItems = kComposeKeyCount;
        labels = kComposeKeyLabels;
      } else if (app.settingsKeyboardDdKind == 4) {
        const int n = static_cast<int>(app.settings.keyboardLayouts.size());
        ddX = contentX + 8 + kCardPad;
        ddY = keyboard_tab::sources_card_top() + 52 + n * keyboard_tab::kSourcesRowH + 6 + 34 + 2;
        nItems = static_cast<int>(keyboard_available_layouts(app).size());
        layoutLabels = nullptr;
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
            ddStrStorage.push_back(keyboard_layout_display_name(l));
          }
          for (const auto& s : ddStrStorage) {
            ddLabels.push_back(s.c_str());
          }
          labels = ddLabels.data();
        } else if (app.settingsKeyboardDdKind == 4) {
          for (const auto& l : keyboard_available_layouts(app)) {
            ddStrStorage.push_back(keyboard_layout_display_name(l));
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
          case 4: selIdx = -1; break;
        }
        const int ddScreenY = ddY - settings_scroll_px_int(app);
        settings_paint_combo_list_popup(app, cr, ddX, ddScreenY, ddW, kSettingsDdRowH, nItems,
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
    eh::wayland::DamageRegion dmgEffective;
    if (!settings_present_vk_raster_damaged(app, app.width, app.height, &transient, dmgNeed,
                                            &dmgEffective)) {
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
              dmg_hist[dmgS % 4] = dmgNew;
              dmg_hist_seq[dmgS % 4] = dmgS;
              dmg_last_frame[fb] = dmgS;
              dmg_committed = std::move(dmgCur);
              dmg_have_committed = true;
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
    dmg_hist[dmgS % 4] = dmgNew;
    dmg_hist_seq[dmgS % 4] = dmgS;
    dmg_committed = std::move(dmgCur);
    dmg_have_committed = true;
    if (dmgEffective.full() || dmgEffective.empty()) {
      wl_surface_damage_buffer(app.surface, 0, 0, INT32_MAX, INT32_MAX);
    } else {
      for (const auto& sp : dmgEffective.spans())
        wl_surface_damage_buffer(app.surface, sp.x, sp.y, sp.w, sp.h);
    }
    eh::settings::widget_picker::queue_caret_frame(app);
    world_clock_popup_queue_caret_frame(app);
    wl_surface_commit(app.surface);
  } else {
    eh::wayland::ShmBuffer& pb = app.buf[static_cast<size_t>(paintBi)];
    wl_surface_attach(app.surface, pb.wl(), 0, 0);
    if (dmgNeed.full()) {
      wl_surface_damage_buffer(app.surface, 0, 0, app.width, app.height);
    } else {
      for (const auto& sp : dmgNeed.spans())
        wl_surface_damage_buffer(app.surface, sp.x, sp.y, sp.w, sp.h);
    }
    pb.mark_busy();
    dmg_hist[dmgS % 4] = dmgNew;
    dmg_hist_seq[dmgS % 4] = dmgS;
    dmg_last_frame[paintBi] = dmgS;
    dmg_committed = std::move(dmgCur);
    dmg_have_committed = true;
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
  // Always-on Monitors render log (NOT gated behind EH_BENCH): render times
  // for the full draw when the Monitors tab is visible, so paint (from
  // paint_monitors_tab) can be compared against shell_snapshot / cairo_body /
  // commit. Goes to ~/.local/state/event-horizon/Horizon-monitors.log.
  if (app.activeTab == 7) {
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
    const double fps_inst = us_total > 0 ? 1000000.0 / static_cast<double>(us_total) : 0.0;
    eh::settings::monitors_log::MonitorsLog::instance().writef(
        "render %dx%d slot=%s shell_snapshot=%lldus sync_gpu=%lldus thumb_merge=%lldus "
        "want_vk=%lldus buffers=%lldus buf_prep=%lldus glass_alpha=%lldus cairo_body=%lldus "
        "commit=%lldus total=%lldus fps_inst=%.1f cause=%s embedded=%d merged_thumbs=%d sidebar=%lldus sb=%u/%u coalesced=%llu dmgspans=%zu dmgfull=%d dmgskip=%llu nOut=%zu dirty=%d",
        app.width, app.height, vk_path ? "vk" : std::to_string(paintBi).c_str(), us_shell_snap,
        us_sync_gpu, us_thumbs, us_want_vk, us_buffers, us_buf_prep, us_glass_alpha, us_cairo_body,
        us_commit, us_total, fps_inst, eh::settings::monitors_log::mon_cause_last(),
        app.embedded ? 1 : 0, mergedThumbs, us_sidebar, sbHits, sbMiss, app.coalescedSkips,
        dmgNeed.span_count(), dmgNeed.full() ? 1 : 0, dmg_skips,
        app.monitorsTab.outputs.size(), app.monitorsTab.dirty ? 1 : 0);
    if (us_total >= 16000) {
      eh::settings::monitors_log::MonitorsLog::instance().writef(
          "slow render total=%lldus cairo_body=%lldus commit=%lldus (>16ms)", us_total, us_cairo_body,
          us_commit);
    }
  }
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
      s_log = fopen((std::string(eh_log::dir()) + "/settings-bench.log").c_str(), "w");
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


