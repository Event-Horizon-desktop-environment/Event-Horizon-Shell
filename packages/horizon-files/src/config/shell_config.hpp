#pragma once

#include "config/shell_renderer_backend.hpp"
#include "platform/dock/core/dock_settings.hpp"
#include "platform/taskbar/core/taskbar_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace eh::config {

struct MatugenExternalTemplateToggles {
  bool runBundledToml = true;
  bool niri = true;
  bool hyprland = true;
  bool mango = true;

  bool gtkShellCss = true;

  bool gtkEventColorsLight = true;

  bool gtkEventColorsDark = true;
  bool kcolorscheme = true;
  bool qt5ct = true;
  bool qt6ct = true;
  bool kittyTheme = true;
  bool kittyTabs = true;
  bool ghostty = true;
  bool wezterm = true;
  bool alacritty = true;
  bool foot = true;
  bool otterTerm = true;
  bool btop = true;
  bool neovim = true;
  bool vscodeMaterial = true;
  bool vscodeColorThemes = true;
  bool firefox = true;
  bool zenbrowser = true;
  bool vesktop = true;
  bool equibop = true;
  bool pywalfox = true;
  bool steam = true;
  bool dgop = true;
  bool emacs = true;
  bool zed = true;
  bool ptyxis = true;
  bool obs = true;
  bool heroic = true;
  bool fluxer = true;
  bool horizonFiles = true;
  bool horizonPhoto = true;
  bool horizonCalendar = true;
};

struct ShellAppearance {
  bool overlayOpacityAdvanced = false;
  float overlayOpacityMaster = 1.f;
  float overlayOpacityControlCenter = 1.f;

  float overlayOpacityControlCenterInner = 1.f;
  float overlayOpacityAppDrawer = 1.f;

  float overlayOpacityLaunchpad = 1.f;
  float overlayOpacitySettings = 1.f;
  float overlayOpacityDockMenu = 1.f;

  float overlayOpacityDesktopMenu = 1.f;
  float overlayOpacityTrayMenu = 1.f;
  float overlayOpacityCalendar = 1.f;
  float overlayOpacityWeather = 1.f;
  float overlayOpacityTooltip = 1.f;
  float overlayOpacityOverview = 1.f;
  float overlayOpacityWidgetCard = 1.f;

  int launchpadGridColumns = 15;
  int launchpadGridRows = 6;
  int launchpadCellGapPx = 0;
  int launchpadIconFillPct = 58;
  int launchpadLayoutScalePct = 100;
  int launchpadDpiScalePct = 100;
  int launchpadViewMode = 1;
  int launchpadFolderSizePct = 100;
  int launchpadFolderGapPx = 16;

  bool matugenThemingEnabled = false;

  std::string matugenScheme = "scheme-content";

  std::string matugenMode = "dark";

  std::string fontFamily = "Inter";

  bool matugenPaletteOk = false;

  float matugenDockFillR = 0.12f, matugenDockFillG = 0.12f, matugenDockFillB = 0.14f;

  float matugenPanelFillR = 0.08f, matugenPanelFillG = 0.10f, matugenPanelFillB = 0.13f;

  float matugenDrawerDimR = 0.11f, matugenDrawerDimG = 0.11f, matugenDrawerDimB = 0.13f;

  float matugenOutlineR = 0.55f, matugenOutlineG = 0.60f, matugenOutlineB = 0.62f;

  float matugenAccentR = 0.90f, matugenAccentG = 0.90f, matugenAccentB = 0.90f;

  float matugenTextR = 0.92f, matugenTextG = 0.92f, matugenTextB = 0.95f;

  float matugenNotifCriticalBgR = 0.18f, matugenNotifCriticalBgG = 0.06f, matugenNotifCriticalBgB = 0.06f;
  float matugenNotifCriticalOutlineR = 0.70f, matugenNotifCriticalOutlineG = 0.10f, matugenNotifCriticalOutlineB = 0.10f;
  float colorBrightness = 1.0f;
  float colorContrast = 1.0f;
  float colorVibrance = 1.0f;
  float colorGamma = 1.0f;
  MatugenExternalTemplateToggles matugenOutputs{};
};

struct ChromePaintColors {
  double dockFillR{}, dockFillG{}, dockFillB{};
  double panelFillR{}, panelFillG{}, panelFillB{};
  double drawerDimR{}, drawerDimG{}, drawerDimB{};
  double outlineR{}, outlineG{}, outlineB{};
  double accentR{}, accentG{}, accentB{};
  double textR{}, textG{}, textB{};
  double notifCriticalBgR{}, notifCriticalBgG{}, notifCriticalBgB{};
  double notifCriticalOutlineR{}, notifCriticalOutlineG{}, notifCriticalOutlineB{};
};

inline void apply_color_adjustment(double& r, double& g, double& b,
                                    double brightness, double contrast,
                                    double vibrance, double gamma) {
  r *= brightness;
  g *= brightness;
  b *= brightness;

  r = (r - 0.5) * contrast + 0.5;
  g = (g - 0.5) * contrast + 0.5;
  b = (b - 0.5) * contrast + 0.5;

  const double gray = (r + g + b) / 3.0;
  r = gray + (r - gray) * vibrance;
  g = gray + (g - gray) * vibrance;
  b = gray + (b - gray) * vibrance;

  r = std::pow(r, 1.0 / gamma);
  g = std::pow(g, 1.0 / gamma);
  b = std::pow(b, 1.0 / gamma);

  r = std::clamp(r, 0.0, 1.0);
  g = std::clamp(g, 0.0, 1.0);
  b = std::clamp(b, 0.0, 1.0);
}

[[nodiscard]] ChromePaintColors derived_chrome_colors(const ShellAppearance& a);

enum class OverlaySurfaceAlphaKind : std::uint8_t {
  ControlCenter = 0,
  AppDrawer = 1,
  Launchpad = 2,
  Settings = 3,
  DockContextMenu = 4,
  DesktopContextMenu = 5,
  TrayMenu = 6,
  Calendar = 7,
  Weather = 8,
  Tooltip = 9,
  Overview = 10,
};

struct WidgetInstanceConfig {
  std::string type;
  std::unordered_map<std::string, std::string> settings;
};

struct DefaultAppsSettings {
  std::string web;
  std::string mail;
  std::string calendar;
  std::string fileManager;
  std::string terminal;
  std::string music;
  std::string video;
  std::string images;
  std::string pdf;
};

struct ShellNotificationsToastSettings {

  bool layerShellEnabled = true;
  std::string position = "top_right";
  int marginPx = 16;
  int cornerRadiusPx = 12;
  int maxWidthPx = 420;
  int scalePct = 100;
};

struct NightLightSettings {
  bool enabled = false;
  int dayTemperature = 6500;    // K
  int nightTemperature = 4000;  // K
  int scheduleMode = 0;         // 0=manual, 1=sunset, 2=scheduled
  int scheduleStartMin = 20 * 60;  // 20:00 in minutes from midnight
  int scheduleEndMin = 6 * 60;     // 06:00 in minutes from midnight
};

struct ShellNotificationsSettings {

  bool dbusEnabled = true;
  bool doNotDisturb = false;

  std::int32_t defaultTimeoutMs = 6000;
  ShellNotificationsToastSettings toast{};
};

struct IdleBehaviorConfig {
  std::string name;
  int timeout_sec = 0;
  std::string command;
  std::string resume_command;
  bool enabled = true;
};

struct IdleSettings {
  std::vector<IdleBehaviorConfig> behaviors;
};

struct TimeSettings {
  bool use24h = false;
  bool showSeconds = false;
  bool showDate = true;
  int dateFormat = 0;          // 0=weekday+day, 1=full date, 2=ISO, 3=custom
  std::string customFormat;    // custom strftime format (overrides all when non-empty)
  std::string timezone;        // empty = system default
};

struct KeyboardSettings {
  std::string layout = "us";
  std::vector<std::string> layouts = {"us"};
  int switchShortcut = 0;    // 0=Alt+Shift, 1=Ctrl+Shift, 2=Super+Space, etc.
  bool showLayout = false;
  bool numlock = true;
  bool inputMethodEnabled = false;
  int capsLockBehavior = 0;  // 0=default, 1=ctrl, 2=swap_esc, 3=disabled
  int composeKey = 0;        // 0=none, 1=ralt, 2=rctrl, 3=menu, 4=rwin
  bool middleClickPaste = true;
  int repeatRate = 25;       // chars per second
  int repeatDelay = 600;     // ms before repeat starts
};

struct PowerSettings {
  bool displaySleep = true;
  int displaySleepTimeoutMin = 10;
  bool idleSuspend = true;
  int idleSuspendTimeoutMin = 30;
  int powerButtonAction = 0;  // 0=ask, 1=suspend, 2=hibernate, 3=shutdown
  int lidCloseAction = 1;     // 0=nothing, 1=suspend, 2=hibernate
  bool showBatteryPercentage = true;
};

struct AudioSettings {
  std::string default_sink_name;
  std::string default_source_name;
  int default_sink_volume_pct = 100;
  bool default_sink_muted = false;
  int default_source_volume_pct = 100;
  bool default_source_muted = false;
  int engine_clock_rate_hz = 48000;
  int engine_force_rate_hz = 0;
  std::vector<int> engine_allowed_rates_hz;
  int compat_pcm_format = 0;
};

struct FileBrowserSettings {
  double zoom_pct = 100.0;       // 50–200
  bool folders_before_files = true;
  int surface_opacity_pct = 100; // 0–100
  int sidebar_opacity_pct = 100; // 0–100
  int topbar_opacity_pct = 100; // 0–100
  int statusbar_opacity_pct = 100; // 0–100
  int preview_opacity_pct = 100; // 0–100; frame only, not content
  std::string default_terminal;  // empty = use system default
  int view_mode = 0;             // 0=List, 1=Grid
  int sort_field = 0;            // 0=Name, 1=Size, 2=Modified, 3=Type
  bool sort_descending = false;
  bool show_hidden = false;
  bool group_by_type = false;

  struct PerFolder {
    int view_mode = 0;
    int sort_field = 0;
    bool sort_descending = false;
    bool group_by_type = false;
  };
  std::unordered_map<std::string, PerFolder> per_folder;

  // ── Sidebar favorites (bookmarked folders) ──
  std::vector<std::string> favorites;

  // ── Window control button placement ──
  bool window_controls_left = false;
};

struct ShellConfig {
  DockSettings dock{};
  ShellAppearance appearance{};
  ShellRendererBackend renderer = ShellRendererBackend::Vulkan;
  bool wallpaperEnabled = false;
  int wallpaperMode = 0;
  std::string wallpaperImage{};
  std::string wallpaperFolder{};
  std::string wallpaperVideoPlayerCmd{};  // empty = auto-detect video & spawn mpvpaper

  bool bingEnabled = false;
  bool bingDailyEnabled = false;
  std::string bingDownloadPath{};
  int bingFilter = 0;
  std::string bingBlockedKeywords;

  int wallpaperFolderPickerMode = 0;

  std::string mprisBlacklist{};

  std::string mprisPreferred{};

  bool mprisNowPlayingNotify = true;
  NightLightSettings nightLight{};
  ShellNotificationsSettings notifications{};

  eh::shell::taskbar::TaskbarSettings taskbar{};

  bool desktopEnabled = true;
  std::string desktopOutputName{};  // empty=all, or specific output name
  std::string desktopWidgetsOutputName{};
  std::string plasmaTheme{};

  std::unordered_map<std::string, WidgetInstanceConfig> widgets;
  DefaultAppsSettings defaultApps{};

  TimeSettings time{};
  IdleSettings idle{};
  KeyboardSettings keyboard{};
  PowerSettings power{};
  AudioSettings audio{};
  FileBrowserSettings fileBrowser{};

  bool autostartEnabled = true;   // master switch for XDG autostart at login
};

[[nodiscard]] float overlay_surface_alpha_scale(const ShellConfig& sc, OverlaySurfaceAlphaKind kind);

[[nodiscard]] float control_center_inner_glass_alpha_scale(const ShellConfig& sc);

[[nodiscard]] std::string widget_implementation_type(std::string_view instance_id);

[[nodiscard]] bool widget_token_is_system_tray(std::string_view token);

[[nodiscard]] bool widget_instance_enabled(const ShellConfig& sc, std::string_view instance_id);

[[nodiscard]] std::string workspaces_widget_instance_id(const ShellConfig& sc);

void merge_widget_overrides_from_state_file(ShellConfig& merged);

[[nodiscard]] std::string declarative_config_dir();
[[nodiscard]] std::string state_event_horizon_dir();
[[nodiscard]] std::string legacy_ini_path();
[[nodiscard]] std::string state_settings_toml_path();
[[nodiscard]] std::string state_file_browser_toml_path();

[[nodiscard]] std::string normalize_wallpaper_path_for_matugen(const std::string& path);

[[nodiscard]] bool write_state_settings_toml(const ShellConfig& c);

/// Read/write the file-browser-specific TOML (separate from main settings).
FileBrowserSettings read_file_browser_toml();
[[nodiscard]] bool write_file_browser_toml(const FileBrowserSettings& fb);

/// Read the icon theme directly from the settings.toml on disk (bypasses
/// the in-memory cache so the file browser can pick up external changes).
[[nodiscard]] std::string read_dock_icon_theme_from_disk();

[[nodiscard]] std::optional<timespec> aggregate_config_source_mtime();

void shell_config_invalidate();
void shell_config_invalidate_light();
void shell_config_apply_from_memory(ShellConfig sc);

void shell_config_reload_from_disk_now();
void shell_config_restore_matugen_palette(const ShellAppearance& ap);

[[nodiscard]] const ShellConfig& shell_config_snapshot();
[[nodiscard]] const ShellConfig& shell_config_snapshot_skip_matugen();
void shell_config_trigger_async_matugen();

using ShellConfigDragPreviewPatchFn = void (*)(ShellConfig& sc, void* user);
void shell_config_set_settings_drag_preview(const ShellConfig& ui_overlay, ShellConfigDragPreviewPatchFn patch,
                                            void* patch_user);
void shell_config_clear_settings_drag_preview();

using ShellConfigDragPreviewPaintTickFn = void (*)(void* user);
void shell_config_set_drag_preview_paint_tick(ShellConfigDragPreviewPaintTickFn fn, void* user);
void shell_config_clear_drag_preview_paint_tick();

using ShellConfigAppliedHookFn = void (*)(void* user);
void shell_config_set_applied_hook(ShellConfigAppliedHookFn fn, void* user);
void shell_config_clear_applied_hook();

}
