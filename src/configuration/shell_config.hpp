#pragma once

#include "configuration/shell_renderer_backend.hpp"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/taskbar/core/taskbar_settings.hpp"

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
  bool horizonFiles = true;
  bool horizonPhoto = true;
  bool horizonCalendar = true;
  bool obs = true;
  bool heroic = true;
  bool fluxer = true;
};

struct ShellAppearance {
  bool overlayOpacityAdvanced = false;
  float overlayOpacityMaster = 1.f;
  float overlayOpacityControlCenter = 1.f;

  float overlayOpacityControlCenterInner = 1.f;
  float overlayOpacityAppDrawer = 1.f;

  float overlayOpacityLaunchpad = 1.f;
  float overlayOpacitySettings = 1.f;
  float overlayOpacitySettingsSidebar = 1.f;
  float overlayOpacityDockMenu = 1.f;

  float overlayOpacityDesktopMenu = 1.f;
  float overlayOpacityTrayMenu = 1.f;
  float overlayOpacityCalendar = 1.f;
  float overlayOpacityWeather = 1.f;
  float overlayOpacityTooltip = 1.f;
  float overlayOpacityOverview = 1.f;
  float overlayOpacityWidgetCard = 1.f;
  float overlayOpacityNotifications = 1.f;

  int launchpadGridColumns = 8;
  int launchpadGridRows = 5;
  int launchpadCellGapPx = 0;
  int launchpadIconFillPct = 58;
  int launchpadLayoutScalePct = 100;
  int launchpadDpiScalePct = 100;
  int launchpadViewMode = 1;
  int launchpadFolderSizePct = 100;
  int launchpadFolderGapPx = 16;

  int overviewAxis = 0;             // 0=Vertical, 1=Horizontal
  int overviewCaptureMode = 0;      // 0=Snapshot (procedural), 1=Screencopy (live output capture)
  bool overviewLiveUpdates = false; // Screencopy mode: re-capture workspaces periodically while the overview is open
  bool overviewMultiMonitor = false; // Show the overview on all monitors; each shows only its own workspaces
  int overviewCardScalePct = 50;    // 20-80
  int overviewCardGapPx = 24;       // 8-80
  int overviewScrollDelayMs = 200;  // 50-500
  int overviewCloseBtnSizePx = 34;  // 20-60
  int overviewSearchWidthPx = 420;  // 200-800

  bool matugenThemingEnabled = false;

  bool horizonColorsNative = true;

  // Horizon Colors (native) palette — self-contained, no external generator involved.
  bool horizonColorsPaletteOk = false;
  float hcDockFillR = 0.12f, hcDockFillG = 0.12f, hcDockFillB = 0.14f;
  float hcPanelFillR = 0.08f, hcPanelFillG = 0.10f, hcPanelFillB = 0.13f;
  float hcDrawerDimR = 0.11f, hcDrawerDimG = 0.11f, hcDrawerDimB = 0.13f;
  float hcOutlineR = 0.55f, hcOutlineG = 0.60f, hcOutlineB = 0.62f;
  float hcAccentR = 0.90f, hcAccentG = 0.90f, hcAccentB = 0.90f;
  float hcTextR = 0.92f, hcTextG = 0.92f, hcTextB = 0.95f;
  float hcNotifCriticalBgR = 0.18f, hcNotifCriticalBgG = 0.06f, hcNotifCriticalBgB = 0.06f;
  float hcNotifCriticalOutlineR = 0.70f, hcNotifCriticalOutlineG = 0.10f, hcNotifCriticalOutlineB = 0.10f;

  [[nodiscard]] inline bool anyPaletteActive() const {
    return (matugenThemingEnabled && matugenPaletteOk) || (horizonColorsNative && horizonColorsPaletteOk) || customThemeEnabled;
  }

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

  // Custom theme fields — the manual alternative to the dynamic palette engines.
  bool customThemeEnabled = false;
  std::string activeThemeName;
  std::string activeThemeSource;  // "custom" | "catppuccin" | "google" | "gruvbox"

  float customPrimaryR = 0.769f, customPrimaryG = 0.659f, customPrimaryB = 0.941f;
  float customOnPrimaryR = 1.0f, customOnPrimaryG = 1.0f, customOnPrimaryB = 1.0f;
  float customSecondaryR = 0.239f, customSecondaryG = 0.125f, customSecondaryB = 0.439f;
  float customOnSecondaryR = 1.0f, customOnSecondaryG = 1.0f, customOnSecondaryB = 1.0f;
  float customTertiaryR = 0.910f, customTertiaryG = 0.522f, customTertiaryB = 0.290f;
  float customOnTertiaryR = 1.0f, customOnTertiaryG = 1.0f, customOnTertiaryB = 1.0f;
  float customErrorR = 0.910f, customErrorG = 0.416f, customErrorB = 0.353f;
  float customOnErrorR = 1.0f, customOnErrorG = 1.0f, customOnErrorB = 1.0f;
  float customSurfaceR = 0.102f, customSurfaceG = 0.075f, customSurfaceB = 0.188f;
  float customOnSurfaceR = 1.0f, customOnSurfaceG = 1.0f, customOnSurfaceB = 1.0f;
  float customSurfaceVariantR = 0.133f, customSurfaceVariantG = 0.102f, customSurfaceVariantB = 0.227f;
  float customOnSurfaceVariantR = 1.0f, customOnSurfaceVariantG = 1.0f, customOnSurfaceVariantB = 1.0f;
  float customOutlineR = 0.478f, customOutlineG = 0.416f, customOutlineB = 0.588f;
  float customShadowR = 0.031f, customShadowG = 0.020f, customShadowB = 0.063f;
  float customHoverR = 0.212f, customHoverG = 0.165f, customHoverB = 0.337f;
  float customOnHoverR = 1.0f, customOnHoverG = 1.0f, customOnHoverB = 1.0f;
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
  Notifications = 11,
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
  std::string tunedProfile = "desktop";
  int epp = 2;                // 0=perf, 1=bal_perf, 2=default, 3=bal_power, 4=power
};

struct VramBoostSettings {
  bool enabled = true;          // dmem cgroup VRAM prioritization for the foreground app
  bool onlyFullscreen = true;   // only boost fullscreen windows
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

  struct PerFolder {
    int view_mode = 0;
    int sort_field = 0;
    bool sort_descending = false;
  };
  std::unordered_map<std::string, PerFolder> per_folder;

  // Sidebar favorites: bookmarked folders.
  std::vector<std::string> favorites;

  // Window control button placement.
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
  VramBoostSettings vramBoost{};
  AudioSettings audio{};
  FileBrowserSettings fileBrowser{};

  std::string avatarPath{};

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
[[nodiscard]] std::string state_settings_toml_path();
[[nodiscard]] std::string state_file_browser_toml_path();

// One TOML per component, stored as <component>/<component>.toml under the state dir.
[[nodiscard]] std::string state_component_toml_dir(std::string_view component);
[[nodiscard]] std::string state_component_toml_path(std::string_view component);
[[nodiscard]] std::string state_general_toml_path();
[[nodiscard]] std::string state_shell_toml_path();
[[nodiscard]] std::string state_desktop_toml_path();
[[nodiscard]] std::string state_autostart_toml_path();
[[nodiscard]] std::string state_dock_toml_path();
[[nodiscard]] std::string state_taskbar_toml_path();
[[nodiscard]] std::string state_appearance_toml_path();
[[nodiscard]] std::string state_wallpaper_toml_path();
[[nodiscard]] std::string state_notifications_toml_path();
[[nodiscard]] std::string state_keyboard_toml_path();
[[nodiscard]] std::string state_audio_toml_path();
[[nodiscard]] std::string state_power_toml_path();
[[nodiscard]] std::string state_nightlight_toml_path();
[[nodiscard]] std::string state_time_toml_path();
[[nodiscard]] std::string state_default_apps_toml_path();
[[nodiscard]] std::string state_mpris_toml_path();
[[nodiscard]] std::string state_widgets_toml_path();
[[nodiscard]] std::string state_idle_toml_path();

[[nodiscard]] std::string normalize_wallpaper_path_for_matugen(const std::string& path);

// Parses "vulkan" / "vk" / "gl" (deprecated, maps to vulkan) / "cairo",
// case-insensitively. Anything unknown falls back to Cairo. Used both by config
// load and by the renderer picker in settings.
[[nodiscard]] ShellRendererBackend parse_shell_renderer_string(std::string_view sv);

[[nodiscard]] bool write_state_settings_toml(const ShellConfig& c);

// Read/write the file-browser-specific TOML (separate from main settings).
FileBrowserSettings read_file_browser_toml();
[[nodiscard]] bool write_file_browser_toml(const FileBrowserSettings& fb);

// Read the icon theme directly from the settings.toml on disk. This bypasses
// the in-memory cache so the file browser can pick up external changes.
[[nodiscard]] std::string read_dock_icon_theme_from_disk();

[[nodiscard]] std::optional<timespec> aggregate_config_source_mtime();

void shell_config_invalidate();
void shell_config_invalidate_light();
void shell_config_apply_from_memory(ShellConfig sc);

void shell_config_reload_from_disk_now(bool skip_matugen = false);
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
