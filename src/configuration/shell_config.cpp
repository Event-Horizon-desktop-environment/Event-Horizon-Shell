#include "configuration/shell_config.hpp"

#include "desktop_shell/common/palette/hyprland_border_matugen.hpp"
#include "desktop_shell/common/palette/matugen_external_templates.hpp"
#include "desktop_shell/common/palette/horizon_colors_templates.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

#define TOML_IMPLEMENTATION
#include <toml++/toml.hpp>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "bootstrap/thread/thread_pool.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"
#include <mutex>
#include <optional>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <string_view>

namespace eh::config {

static bool path_ends_with_video_ext(const std::string& s) {
  constexpr const char* exts[] = {".mp4", ".webm", ".mkv", ".mov", ".avi"};
  for (const char* ext : exts) {
    size_t elen = std::strlen(ext);
    if (s.size() < elen) continue;
    size_t off = s.size() - elen;
    bool match = true;
    for (size_t i = 0; i < elen; ++i) {
      if (std::tolower(static_cast<unsigned char>(s[off + i])) != static_cast<unsigned char>(ext[i])) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

static std::string video_frame_png_for_matugen() {
  const char* state = std::getenv("XDG_STATE_HOME");
  if (!state || !state[0]) state = std::getenv("HOME");
  if (state && state[0]) {
    std::string base(state);
    if (std::getenv("XDG_STATE_HOME")) base += "/event-horizon/video-frame.png";
    else base += "/.local/state/event-horizon/video-frame.png";
    return base;
  }
  return "/tmp/event-horizon/video-frame.png";
}

std::string normalize_wallpaper_path_for_matugen(const std::string& path) {
    
  if (path.empty()) return path;
  if (path_ends_with_video_ext(path)) {
    const std::string frame = video_frame_png_for_matugen();
    std::error_code ec;
    if (std::filesystem::exists(frame, ec)) return frame;
  }
  std::error_code ec;
  std::filesystem::path p(path);
  if (!std::filesystem::exists(p, ec) || ec) return path;
  std::filesystem::path c = std::filesystem::weakly_canonical(p, ec);
  return ec ? path : c.string();
}

ShellRendererBackend parse_shell_renderer_string(std::string_view sv) {
   
  std::string low(sv);
  for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (low == "gl") {
    static std::atomic<bool> s_warned_renderer_gl{};
    if (!s_warned_renderer_gl.exchange(true)) {
      std::cerr << "[shell-config] renderer=\"gl\" is no longer supported; using vulkan\n";
    }
    return ShellRendererBackend::Vulkan;
  }
  if (low == "vulkan" || low == "vk") return ShellRendererBackend::Vulkan;
  return ShellRendererBackend::Cairo;
}

namespace {

std::mutex g_mu;
std::atomic<std::uint64_t> g_load_gen{0};

std::optional<ShellConfig> g_cache;
std::optional<ShellConfig> g_cache_no_matugen;

ShellConfig g_drag_preview_merge{};
bool g_drag_preview_active = false;
ShellConfigDragPreviewPaintTickFn g_drag_preview_paint_tick = nullptr;
void* g_drag_preview_paint_tick_user = nullptr;
ShellConfigAppliedHookFn g_applied_hook = nullptr;
void* g_applied_hook_user = nullptr;

void merge_settings_drag_fields_onto(ShellConfig& base, const ShellConfig& ui) {
   
  base.dock = ui.dock;
  base.taskbar = ui.taskbar;
  base.appearance = ui.appearance;
  base.notifications = ui.notifications;
  base.defaultApps = ui.defaultApps;
  base.wallpaperEnabled = ui.wallpaperEnabled;
  base.wallpaperMode = ui.wallpaperMode;
  base.wallpaperImage = ui.wallpaperImage;
  base.wallpaperFolder = ui.wallpaperFolder;
  base.wallpaperFolderPickerMode = ui.wallpaperFolderPickerMode;
  base.wallpaperVideoPlayerCmd = ui.wallpaperVideoPlayerCmd;
}

std::string trim(std::string_view s) {
   
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
  if (start == s.size()) return {};
  size_t end = s.size();
  while (end > start && (s[end-1] == '\n' || s[end-1] == '\r' || s[end-1] == ' ' || s[end-1] == '\t')) end--;
  return std::string(s.substr(start, end - start));
}

void deep_merge_toml(toml::table& base, const toml::table& overlay) {
   
  for (const auto& [k, v] : overlay) {
    if (const auto* vt = v.as_table()) {
      if (auto* existing = base[k].as_table()) {
        deep_merge_toml(*existing, *vt);
      } else {
        base.insert_or_assign(k, *vt);
      }
    } else {
      base.insert_or_assign(k, v);
    }
  }
}

std::vector<std::string> toml_string_array(const toml::array* a) {
   
  std::vector<std::string> out;
  if (a == nullptr) return out;
  for (const auto& el : *a) {
    if (const auto* s = el.as_string()) out.push_back(std::string(s->get()));
  }
  return out;
}


void normalize_legacy_tray_tokens(ShellConfig& c) {
   
  auto fix = [](std::vector<std::string>& v) {
    for (auto& s : v) {
      if (s == "system_tray") s = "tray";
    }
  };
  fix(c.dock.leftWidgets);
  fix(c.dock.centerWidgets);
  fix(c.dock.rightWidgets);
}

bool dock_tray_list_token_local(const ShellConfig& c, const std::string& token) {
   
  if (token == "tray" || token == "system_tray") return true;
  const auto it = c.widgets.find(token);
  return it != c.widgets.end() && it->second.type == "system_tray";
}

void dock_migrate_legacy_autosep_slots(ShellConfig& c) {
   
  auto fix = [&](std::vector<std::string>& v) {
    for (size_t i = 0; i + 1 < v.size(); ++i) {
      if (dock_tray_list_token_local(c, v[i]) && v[i + 1] == "settings_button") {
        v.insert(v.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::string("spacer"));
        ++i;
      }
    }
  };
  fix(c.dock.leftWidgets);
  fix(c.dock.centerWidgets);
  fix(c.dock.rightWidgets);
}

void ensure_builtin_tray_widget(ShellConfig& c) {
   
  if (c.widgets.find("tray") == c.widgets.end()) {
    c.widgets["tray"] = WidgetInstanceConfig{.type = "system_tray", .settings = {}};
  }
}

void ensure_builtin_media_widget(ShellConfig& c) {
   
  if (c.widgets.find("media") == c.widgets.end()) {
    c.widgets["media"] = WidgetInstanceConfig{.type = "media", .settings = {}};
  }
}

void ensure_builtin_spacer_widget(ShellConfig& c) {
   
  if (c.widgets.find("spacer") == c.widgets.end()) {
    c.widgets["spacer"] = WidgetInstanceConfig{.type = "spacer", .settings = {}};
  }
}

void ensure_builtin_trash_widget(ShellConfig& c) {
   
  if (c.widgets.find("trash") == c.widgets.end()) {
    c.widgets["trash"] = WidgetInstanceConfig{.type = "trash", .settings = {}};
  }
}

void apply_dock_defaults_if_needed(ShellConfig& c) {
   
  if (c.dock.leftWidgets.empty() && c.dock.centerWidgets.empty() && c.dock.rightWidgets.empty()) {
    c.dock.leftWidgets = {"smenu", "media"};
    c.dock.centerWidgets = {"launchpad", "pinned_apps", "running_apps", "trash"};
    c.dock.rightWidgets = {"tray", "weather", "clock", "control_center", "settings_button"};
  }
}

void apply_taskbar_defaults_if_needed(ShellConfig& c) {
   
  if (c.taskbar.leftWidgets.empty() && c.taskbar.centerWidgets.empty() && c.taskbar.rightWidgets.empty()) {
    c.taskbar.leftWidgets = {"smenu", "media"};
    c.taskbar.centerWidgets = {"launchpad", "pinned_apps", "running_apps", "trash"};
    c.taskbar.rightWidgets = {"tray", "weather", "clock", "control_center", "settings_button"};
  }
}

void apply_toml_overlay(ShellConfig& c, const toml::table& root) {
   
  debug_log("settings", "apply_toml_overlay: BEGIN");
  if (const auto* g = root.get_as<toml::table>("general")) {
    if (auto s = (*g)["icon_theme"].value<std::string>()) {
      c.dock.iconTheme = *s;
    }
    if (auto s = (*g)["avatar_path"].value<std::string>()) {
      c.avatarPath = *s;
    }
  }

  if (const auto* sh = root.get_as<toml::table>("shell")) {
    if (auto s = (*sh)["renderer"].value<std::string>()) {
      c.renderer = parse_shell_renderer_string(std::string_view(*s));
    }
    if (auto s = (*sh)["font_family"].value<std::string>())
      c.appearance.fontFamily = *s;
    if (auto d = (*sh)["ui_scale"].value<double>()) {
      if (std::isfinite(*d)) c.dock.shellUiScale = std::clamp(*d, 0.5, 2.0);
    } else if (auto i = (*sh)["ui_scale"].value<int64_t>()) {
      c.dock.shellUiScale = std::clamp(static_cast<double>(*i), 0.5, 2.0);
    }
  }

  if (const auto* dock = root.get_as<toml::table>("dock")) {
    if (auto v = (*dock)["autohide"].value<bool>()) c.dock.dockAutoHide = *v;
    if (auto v = (*dock)["show_dock"].value<bool>()) c.dock.dockShowDock = *v;
    if (auto v = (*dock)["dock_widgets_enabled"].value<bool>()) c.dock.dockWidgetsEnabled = *v;
    if (auto v = (*dock)["group_apps"].value<bool>()) c.dock.dockGroupApps = *v;
    if (auto v = (*dock)["dock_tooltips_enabled"].value<bool>()) c.dock.dockTooltipsEnabled = *v;
    if (auto v = (*dock)["dock_pinned_apps_tray_pill"].value<bool>()) c.dock.dockPinnedAppsTrayPill = *v;
    if (auto v = (*dock)["dock_running_apps_tray_pill"].value<bool>()) c.dock.dockRunningAppsTrayPill = *v;
    if (auto v = (*dock)["radius"].value<int64_t>()) c.dock.dockRadius = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(50)));
    if (auto v = (*dock)["icon_size"].value<int64_t>()) c.dock.dockIconSize = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(50)));
    if (auto v = (*dock)["icon_spacing"].value<int64_t>())
      c.dock.dockIconSpacing = static_cast<int>(std::clamp(*v, INT64_C(-50), INT64_C(50)));
    if (auto v = (*dock)["bottom_gap"].value<int64_t>()) c.dock.dockBottomGap = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(25)));
    if (auto v = (*dock)["exclusive_zone_gap"].value<int64_t>())
      c.dock.dockExclusiveZoneGap = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*dock)["border_enabled"].value<bool>()) c.dock.dockBorderEnabled = *v;
    if (auto v = (*dock)["border_size"].value<int64_t>()) c.dock.dockBorderSize = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(12)));
    if (auto v = (*dock)["border_hue"].value<int64_t>()) c.dock.dockBorderHue = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(359)));
    if (auto v = (*dock)["border_opacity"].value<int64_t>()) c.dock.dockBorderOpacity = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*dock)["opacity"].value<int64_t>()) c.dock.dockOpacity = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*dock)["slot_pill_opacity"].value<int64_t>()) c.dock.slotPillOpacity = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*dock)["liquid_glass"].value<bool>()) c.dock.dockLiquidGlass = *v;
    if (auto v = (*dock)["colored_glass"].value<bool>()) c.dock.dockColoredGlass = *v;
    if (auto d = (*dock)["scale"].value<double>()) {
      if (std::isfinite(*d)) c.dock.dockScale = std::clamp(*d, 0.5, 2.0);
    } else if (auto i = (*dock)["scale"].value<int64_t>()) {
      c.dock.dockScale = std::clamp(static_cast<double>(*i), 0.5, 2.0);
    }
    if (auto v = (*dock)["bar_follows_icons"].value<bool>()) c.dock.dockBarFollowsIcons = *v;
    if (auto v = (*dock)["manual_bar_height"].value<int64_t>())
      c.dock.dockManualBarHeightPx = static_cast<int>(std::clamp(*v, INT64_C(32), INT64_C(200)));
    if (const auto* a = (*dock)["pinned_apps"].as_array()) c.dock.pinnedApps = toml_string_array(a);
    if (const auto* a = (*dock)["start_menu_pinned_apps"].as_array()) c.dock.startMenuPinnedApps = toml_string_array(a);
    if (const auto* a = (*dock)["drawer_pinned_apps"].as_array()) c.dock.drawerPinnedApps = toml_string_array(a);
    if (const auto* a = (*dock)["left_widgets"].as_array()) c.dock.leftWidgets = toml_string_array(a);
    if (const auto* a = (*dock)["center_widgets"].as_array()) c.dock.centerWidgets = toml_string_array(a);
    if (const auto* a = (*dock)["right_widgets"].as_array()) c.dock.rightWidgets = toml_string_array(a);
    if (auto s = (*dock)["output"].value<std::string>()) c.dock.outputName = trim(*s);
  }

  if (const auto* tb = root.get_as<toml::table>("taskbar")) {
    if (auto v = (*tb)["enabled"].value<bool>()) c.taskbar.enabled = *v;
    if (auto v = (*tb)["width_mode"].value<int64_t>()) c.taskbar.widthMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(2)));
    if (auto v = (*tb)["height"].value<int64_t>()) c.taskbar.height = static_cast<int>(std::clamp(*v, INT64_C(24), INT64_C(120)));
    if (auto v = (*tb)["radius"].value<int64_t>()) c.taskbar.radius = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(50)));
    if (auto v = (*tb)["opacity"].value<int64_t>()) c.taskbar.opacity = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*tb)["icon_size"].value<int64_t>()) c.taskbar.iconSize = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(96)));
    if (auto v = (*tb)["icon_spacing"].value<int64_t>()) c.taskbar.iconSpacing = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(50)));
    if (auto v = (*tb)["floating_amount"].value<int64_t>()) c.taskbar.floatingAmount = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(50)));
    if (auto v = (*tb)["edge_gap"].value<int64_t>()) c.taskbar.edgeGap = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(25)));
    if (auto v = (*tb)["exclusive_zone_gap"].value<int64_t>())
      c.taskbar.exclusiveZoneGap = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto d = (*tb)["scale"].value<double>()) {
      if (std::isfinite(*d)) c.taskbar.scale = std::clamp(*d, 0.5, 2.0);
    } else if (auto i = (*tb)["scale"].value<int64_t>()) {
      c.taskbar.scale = std::clamp(static_cast<double>(*i), 0.5, 2.0);
    }
    if (const auto* a = (*tb)["left_widgets"].as_array()) c.taskbar.leftWidgets = toml_string_array(a);
    if (const auto* a = (*tb)["center_widgets"].as_array()) c.taskbar.centerWidgets = toml_string_array(a);
    if (const auto* a = (*tb)["right_widgets"].as_array()) c.taskbar.rightWidgets = toml_string_array(a);
    if (const auto* a = (*tb)["pinned_apps"].as_array()) c.taskbar.pinnedApps = toml_string_array(a);
    if (auto v = (*tb)["group_apps"].value<bool>()) c.taskbar.groupApps = *v;
    if (auto v = (*tb)["position_top"].value<bool>()) c.taskbar.positionTop = *v;
    if (auto s = (*tb)["output"].value<std::string>()) c.taskbar.outputName = eh::shell::trim_output_assign(*s);
    if (auto v = (*tb)["slot_pill_opacity"].value<int64_t>()) c.taskbar.slotPillOpacity = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*tb)["auto_hide"].value<bool>()) c.taskbar.autoHide = *v;
    if (auto v = (*tb)["tooltips"].value<bool>()) c.taskbar.tooltipsEnabled = *v;
    if (auto v = (*tb)["pinned_apps_tray_pill"].value<bool>()) c.taskbar.pinnedAppsTrayPill = *v;
    if (auto v = (*tb)["running_apps_tray_pill"].value<bool>()) c.taskbar.runningAppsTrayPill = *v;
    if (auto v = (*tb)["widgets_enabled"].value<bool>()) c.taskbar.widgetsEnabled = *v;
    if (auto v = (*tb)["border"].value<bool>()) c.taskbar.border = *v;
    if (auto v = (*tb)["border_size"].value<int64_t>()) c.taskbar.borderSize = static_cast<int>(std::clamp(*v, INT64_C(1), INT64_C(12)));
    if (auto s = (*tb)["icon_theme"].value<std::string>()) c.taskbar.iconTheme = trim(*s);
  }

  if (const auto* dt = root.get_as<toml::table>("desktop")) {
    if (auto v = (*dt)["enabled"].value<bool>()) c.desktopEnabled = *v;
    if (auto s = (*dt)["output"].value<std::string>()) c.desktopOutputName = eh::shell::trim_output_assign(*s);
  }

  if (const auto* as = root.get_as<toml::table>("autostart")) {
    if (auto v = (*as)["enabled"].value<bool>()) c.autostartEnabled = *v;
  }

  if (const auto* wp = root.get_as<toml::table>("wallpaper")) {
    if (auto v = (*wp)["enabled"].value<bool>()) c.wallpaperEnabled = *v;
    if (auto v = (*wp)["mode"].value<int64_t>()) c.wallpaperMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(4)));
    if (auto s = (*wp)["image"].value<std::string>()) c.wallpaperImage = *s;
    if (auto s = (*wp)["folder"].value<std::string>()) c.wallpaperFolder = *s;
    if (auto v = (*wp)["folder_picker_mode"].value<int64_t>())
      c.wallpaperFolderPickerMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto s = (*wp)["player_cmd"].value<std::string>()) c.wallpaperVideoPlayerCmd = *s;
    if (auto v = (*wp)["bing_enabled"].value<bool>()) c.bingEnabled = *v;
    if (auto v = (*wp)["bing_daily_enabled"].value<bool>()) c.bingDailyEnabled = *v;
    if (auto s = (*wp)["bing_download_path"].value<std::string>()) c.bingDownloadPath = *s;
    if (auto v = (*wp)["bing_filter"].value<int64_t>())
      c.bingFilter = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto s = (*wp)["bing_blocked_keywords"].value<std::string>()) c.bingBlockedKeywords = *s;
  }

  if (const auto* mp = root.get_as<toml::table>("mpris")) {
    if (auto s = (*mp)["blacklist"].value<std::string>()) c.mprisBlacklist = *s;
    if (auto s = (*mp)["preferred"].value<std::string>()) c.mprisPreferred = *s;
    if (auto v = (*mp)["now_playing_notify"].value<bool>()) c.mprisNowPlayingNotify = *v;
  }

  if (const auto* nt = root.get_as<toml::table>("notifications")) {
    if (auto v = (*nt)["dbus"].value<bool>()) c.notifications.dbusEnabled = *v;
    if (auto v = (*nt)["dbus_enabled"].value<bool>()) c.notifications.dbusEnabled = *v;
    if (auto v = (*nt)["do_not_disturb"].value<bool>()) c.notifications.doNotDisturb = *v;
    if (auto v = (*nt)["default_timeout_ms"].value<int64_t>()) {
      c.notifications.defaultTimeoutMs =
          static_cast<std::int32_t>(std::clamp(*v, INT64_C(1000), INT64_C(600000)));
    }
    if (const auto* toast = (*nt)["toast"].as_table()) {
      if (auto v = (*toast)["enabled"].value<bool>()) c.notifications.toast.layerShellEnabled = *v;
      if (auto v = (*toast)["layer_shell_enabled"].value<bool>()) c.notifications.toast.layerShellEnabled = *v;
      if (auto v = (*toast)["margin_px"].value<int64_t>()) {
        c.notifications.toast.marginPx = static_cast<int>(std::clamp(*v, INT64_C(8), INT64_C(64)));
      }
      if (auto v = (*toast)["corner_radius_px"].value<int64_t>()) {
        c.notifications.toast.cornerRadiusPx = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(40)));
      }
      if (auto v = (*toast)["max_width_px"].value<int64_t>()) {
        c.notifications.toast.maxWidthPx = static_cast<int>(std::clamp(*v, INT64_C(200), INT64_C(900)));
      }
      if (auto v = (*toast)["scale_pct"].value<int64_t>()) {
        c.notifications.toast.scalePct = static_cast<int>(std::clamp(*v, INT64_C(50), INT64_C(200)));
      }
      if (auto s = (*toast)["position"].value<std::string>()) {
        c.notifications.toast.position = *s;
      }
    }
  }

  if (const auto* da = root.get_as<toml::table>("default_apps")) {
    if (auto s = (*da)["web"].value<std::string>()) c.defaultApps.web = trim(*s);
    if (auto s = (*da)["mail"].value<std::string>()) c.defaultApps.mail = trim(*s);
    if (auto s = (*da)["calendar"].value<std::string>()) c.defaultApps.calendar = trim(*s);
    if (auto s = (*da)["file_manager"].value<std::string>()) c.defaultApps.fileManager = trim(*s);
    if (auto s = (*da)["terminal"].value<std::string>()) c.defaultApps.terminal = trim(*s);
    if (auto s = (*da)["music"].value<std::string>()) c.defaultApps.music = trim(*s);
    if (auto s = (*da)["video"].value<std::string>()) c.defaultApps.video = trim(*s);
    if (auto s = (*da)["images"].value<std::string>()) c.defaultApps.images = trim(*s);
    if (auto s = (*da)["pdf"].value<std::string>()) c.defaultApps.pdf = trim(*s);
  }

  if (const auto* tm = root.get_as<toml::table>("time")) {
    if (auto v = (*tm)["use_24h"].value<bool>()) c.time.use24h = *v;
    if (auto v = (*tm)["show_seconds"].value<bool>()) c.time.showSeconds = *v;
    if (auto v = (*tm)["show_date"].value<bool>()) c.time.showDate = *v;
    if (auto v = (*tm)["date_format"].value<int64_t>()) c.time.dateFormat = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto s = (*tm)["custom_format"].value<std::string>()) c.time.customFormat = *s;
    if (auto s = (*tm)["timezone"].value<std::string>()) c.time.timezone = *s;
  }

  if (const auto* kb = root.get_as<toml::table>("keyboard")) {
    if (auto s = (*kb)["layout"].value<std::string>()) c.keyboard.layout = *s;
    if (const auto* a = (*kb)["layouts"].as_array()) c.keyboard.layouts = toml_string_array(a);
    if (auto v = (*kb)["switch_shortcut"].value<int64_t>())
      c.keyboard.switchShortcut = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(6)));
    if (auto v = (*kb)["show_layout"].value<bool>()) c.keyboard.showLayout = *v;
    if (auto v = (*kb)["numlock"].value<bool>()) c.keyboard.numlock = *v;
    if (auto v = (*kb)["input_method"].value<bool>()) c.keyboard.inputMethodEnabled = *v;
    if (auto v = (*kb)["caps_lock_behavior"].value<int64_t>())
      c.keyboard.capsLockBehavior = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto v = (*kb)["compose_key"].value<int64_t>())
      c.keyboard.composeKey = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(4)));
    if (auto v = (*kb)["middle_click_paste"].value<bool>()) c.keyboard.middleClickPaste = *v;
    if (auto v = (*kb)["repeat_rate"].value<int64_t>())
      c.keyboard.repeatRate = static_cast<int>(std::clamp(*v, INT64_C(15), INT64_C(50)));
    if (auto v = (*kb)["repeat_delay"].value<int64_t>())
      c.keyboard.repeatDelay = static_cast<int>(std::clamp(*v, INT64_C(150), INT64_C(1000)));
  }

  if (const auto* ap = root.get_as<toml::table>("appearance")) {
    if (auto v = (*ap)["overlay_opacity_advanced"].value<bool>()) c.appearance.overlayOpacityAdvanced = *v;
    auto assign_overlay = [&](const char* key, float& out) {
      if (auto d = (*ap)[key].value<double>()) {
        out = static_cast<float>(std::clamp(*d, 0.0, 1.0));
      } else if (auto i = (*ap)[key].value<int64_t>()) {
        out = static_cast<float>(std::clamp(*i, INT64_C(0), INT64_C(100))) / 100.f;
      }
    };
    assign_overlay("overlay_opacity_master", c.appearance.overlayOpacityMaster);
    assign_overlay("overlay_opacity_control_center", c.appearance.overlayOpacityControlCenter);
    assign_overlay("overlay_opacity_control_center_inner", c.appearance.overlayOpacityControlCenterInner);
    assign_overlay("overlay_opacity_app_drawer", c.appearance.overlayOpacityAppDrawer);
    assign_overlay("overlay_opacity_launchpad", c.appearance.overlayOpacityLaunchpad);
    assign_overlay("overlay_opacity_settings", c.appearance.overlayOpacitySettings);
    assign_overlay("overlay_opacity_settings_sidebar", c.appearance.overlayOpacitySettingsSidebar);
    assign_overlay("overlay_opacity_dock_menu", c.appearance.overlayOpacityDockMenu);
    assign_overlay("overlay_opacity_desktop_menu", c.appearance.overlayOpacityDesktopMenu);
    assign_overlay("overlay_opacity_tray_menu", c.appearance.overlayOpacityTrayMenu);
    assign_overlay("overlay_opacity_calendar", c.appearance.overlayOpacityCalendar);
    assign_overlay("overlay_opacity_weather", c.appearance.overlayOpacityWeather);
    assign_overlay("overlay_opacity_tooltip", c.appearance.overlayOpacityTooltip);
    assign_overlay("overlay_opacity_widget_card", c.appearance.overlayOpacityWidgetCard);
    assign_overlay("overlay_opacity_notifications", c.appearance.overlayOpacityNotifications);
    assign_overlay("overlay_opacity_overview", c.appearance.overlayOpacityOverview);
    if (auto v = (*ap)["launchpad_grid_columns"].value<int64_t>()) {
      c.appearance.launchpadGridColumns = static_cast<int>(std::clamp(*v, INT64_C(4), INT64_C(12)));
    }
    if (auto v = (*ap)["launchpad_grid_rows"].value<int64_t>()) {
      c.appearance.launchpadGridRows = static_cast<int>(std::clamp(*v, INT64_C(3), INT64_C(10)));
    }
    if (auto v = (*ap)["launchpad_cell_gap_px"].value<int64_t>()) {
      c.appearance.launchpadCellGapPx = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(24)));
    }
    if (auto v = (*ap)["launchpad_icon_fill_pct"].value<int64_t>()) {
      c.appearance.launchpadIconFillPct = static_cast<int>(std::clamp(*v, INT64_C(30), INT64_C(95)));
    }
    if (auto v = (*ap)["launchpad_layout_scale_pct"].value<int64_t>()) {
      c.appearance.launchpadLayoutScalePct = static_cast<int>(std::clamp(*v, INT64_C(70), INT64_C(150)));
    }
    if (auto v = (*ap)["launchpad_dpi_scale_pct"].value<int64_t>()) {
      c.appearance.launchpadDpiScalePct = static_cast<int>(std::clamp(*v, INT64_C(50), INT64_C(300)));
    }
    if (auto v = (*ap)["launchpad_view_mode"].value<int64_t>()) {
      c.appearance.launchpadViewMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1)));
    }
    if (auto v = (*ap)["overview_axis"].value<int64_t>()) {
      c.appearance.overviewAxis = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1)));
    }
    if (auto v = (*ap)["overview_capture_mode"].value<int64_t>()) {
      c.appearance.overviewCaptureMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1)));
    }
    if (auto v = (*ap)["overview_live_updates"].value<bool>()) {
      c.appearance.overviewLiveUpdates = *v;
    }
    if (auto v = (*ap)["overview_multi_monitor"].value<bool>()) {
      c.appearance.overviewMultiMonitor = *v;
    }
    if (auto v = (*ap)["overview_card_scale_pct"].value<int64_t>()) {
      c.appearance.overviewCardScalePct = static_cast<int>(std::clamp(*v, INT64_C(20), INT64_C(80)));
    }
    if (auto v = (*ap)["overview_card_gap_px"].value<int64_t>()) {
      c.appearance.overviewCardGapPx = static_cast<int>(std::clamp(*v, INT64_C(8), INT64_C(80)));
    }
    if (auto v = (*ap)["overview_scroll_delay_ms"].value<int64_t>()) {
      c.appearance.overviewScrollDelayMs = static_cast<int>(std::clamp(*v, INT64_C(50), INT64_C(500)));
    }
    if (auto v = (*ap)["overview_close_btn_size_px"].value<int64_t>()) {
      c.appearance.overviewCloseBtnSizePx = static_cast<int>(std::clamp(*v, INT64_C(20), INT64_C(60)));
    }
    if (auto v = (*ap)["overview_search_width_px"].value<int64_t>()) {
      c.appearance.overviewSearchWidthPx = static_cast<int>(std::clamp(*v, INT64_C(200), INT64_C(800)));
    }
    if (auto v = (*ap)["matugen_theming"].value<bool>()) c.appearance.matugenThemingEnabled = *v;
    if (auto v = (*ap)["horizon_colors_native"].value<bool>()) c.appearance.horizonColorsNative = *v;
    if (auto s = (*ap)["matugen_scheme"].value<std::string>())
      c.appearance.matugenScheme = eh::matugen::normalize_matugen_scheme(*s);
    if (auto s = (*ap)["matugen_mode"].value<std::string>())
      c.appearance.matugenMode = eh::matugen::normalize_matugen_mode(*s);
    if (const auto* mtp = (*ap)["matugen_templates"].as_table()) {
      auto tb = [&](const char* key, bool& out) {
        if (auto v = (*mtp)[key].value<bool>()) out = *v;
      };
      MatugenExternalTemplateToggles& mo = c.appearance.matugenOutputs;
      std::optional<bool> legacy_gtk;
      if (auto v = (*mtp)["gtk"].value<bool>()) legacy_gtk = *v;
      std::optional<bool> legacy_kitty;
      if (auto v = (*mtp)["kitty"].value<bool>()) legacy_kitty = *v;
      std::optional<bool> legacy_vscode;
      if (auto v = (*mtp)["vscode"].value<bool>()) legacy_vscode = *v;

      tb("run_bundled", mo.runBundledToml);
      tb("niri", mo.niri);
      tb("hyprland", mo.hyprland);
      tb("mango", mo.mango);
      if (auto v = (*mtp)["gtk_shell_css"].value<bool>()) mo.gtkShellCss = *v;
      else if (legacy_gtk) mo.gtkShellCss = *legacy_gtk;
      if (auto v = (*mtp)["gtk_event_colors_light"].value<bool>()) mo.gtkEventColorsLight = *v;
      else if (legacy_gtk) mo.gtkEventColorsLight = *legacy_gtk;
      if (auto v = (*mtp)["gtk_event_colors_dark"].value<bool>()) mo.gtkEventColorsDark = *v;
      else if (legacy_gtk) mo.gtkEventColorsDark = *legacy_gtk;
      tb("kcolorscheme", mo.kcolorscheme);
      tb("qt5ct", mo.qt5ct);
      tb("qt6ct", mo.qt6ct);
      if (auto v = (*mtp)["kitty_theme"].value<bool>()) mo.kittyTheme = *v;
      else if (legacy_kitty) mo.kittyTheme = *legacy_kitty;
      if (auto v = (*mtp)["kitty_tabs"].value<bool>()) mo.kittyTabs = *v;
      else if (legacy_kitty) mo.kittyTabs = *legacy_kitty;
      tb("ghostty", mo.ghostty);
      tb("wezterm", mo.wezterm);
      tb("alacritty", mo.alacritty);
      tb("foot", mo.foot);
      tb("otter_term", mo.otterTerm);
      tb("btop", mo.btop);
      tb("neovim", mo.neovim);
      if (auto v = (*mtp)["vscode_material"].value<bool>()) mo.vscodeMaterial = *v;
      else if (legacy_vscode) mo.vscodeMaterial = *legacy_vscode;
      if (auto v = (*mtp)["vscode_color_themes"].value<bool>()) mo.vscodeColorThemes = *v;
      else if (legacy_vscode) mo.vscodeColorThemes = *legacy_vscode;
      tb("firefox", mo.firefox);
      tb("zenbrowser", mo.zenbrowser);
      tb("vesktop", mo.vesktop);
      tb("equibop", mo.equibop);
      tb("pywalfox", mo.pywalfox);
      tb("steam", mo.steam);
      tb("dgop", mo.dgop);
      tb("emacs", mo.emacs);
      tb("zed", mo.zed);
      tb("ptyxis", mo.ptyxis);
      tb("horizon_files", mo.horizonFiles);
      tb("horizon_photo", mo.horizonPhoto);
      tb("horizon_calendar", mo.horizonCalendar);
      tb("obs", mo.obs);
      tb("heroic", mo.heroic);
      tb("fluxer", mo.fluxer);
    }
    auto read_cf = [&](const char* key, float& out) {
      if (auto d = (*ap)[key].value<double>())
        out = static_cast<float>(std::clamp(*d, 0.0, 1.0));
    };
    if (auto v = (*ap)["color_theme_enabled"].value<bool>()) c.appearance.customThemeEnabled = *v;
    if (auto s = (*ap)["color_theme_name"].value<std::string>()) c.appearance.activeThemeName = *s;
    if (auto s = (*ap)["color_theme_source"].value<std::string>()) c.appearance.activeThemeSource = *s;
    read_cf("color_theme_primary_r", c.appearance.customPrimaryR);
    read_cf("color_theme_primary_g", c.appearance.customPrimaryG);
    read_cf("color_theme_primary_b", c.appearance.customPrimaryB);
    read_cf("color_theme_on_primary_r", c.appearance.customOnPrimaryR);
    read_cf("color_theme_on_primary_g", c.appearance.customOnPrimaryG);
    read_cf("color_theme_on_primary_b", c.appearance.customOnPrimaryB);
    read_cf("color_theme_secondary_r", c.appearance.customSecondaryR);
    read_cf("color_theme_secondary_g", c.appearance.customSecondaryG);
    read_cf("color_theme_secondary_b", c.appearance.customSecondaryB);
    read_cf("color_theme_on_secondary_r", c.appearance.customOnSecondaryR);
    read_cf("color_theme_on_secondary_g", c.appearance.customOnSecondaryG);
    read_cf("color_theme_on_secondary_b", c.appearance.customOnSecondaryB);
    read_cf("color_theme_tertiary_r", c.appearance.customTertiaryR);
    read_cf("color_theme_tertiary_g", c.appearance.customTertiaryG);
    read_cf("color_theme_tertiary_b", c.appearance.customTertiaryB);
    read_cf("color_theme_on_tertiary_r", c.appearance.customOnTertiaryR);
    read_cf("color_theme_on_tertiary_g", c.appearance.customOnTertiaryG);
    read_cf("color_theme_on_tertiary_b", c.appearance.customOnTertiaryB);
    read_cf("color_theme_error_r", c.appearance.customErrorR);
    read_cf("color_theme_error_g", c.appearance.customErrorG);
    read_cf("color_theme_error_b", c.appearance.customErrorB);
    read_cf("color_theme_on_error_r", c.appearance.customOnErrorR);
    read_cf("color_theme_on_error_g", c.appearance.customOnErrorG);
    read_cf("color_theme_on_error_b", c.appearance.customOnErrorB);
    read_cf("color_theme_surface_r", c.appearance.customSurfaceR);
    read_cf("color_theme_surface_g", c.appearance.customSurfaceG);
    read_cf("color_theme_surface_b", c.appearance.customSurfaceB);
    read_cf("color_theme_on_surface_r", c.appearance.customOnSurfaceR);
    read_cf("color_theme_on_surface_g", c.appearance.customOnSurfaceG);
    read_cf("color_theme_on_surface_b", c.appearance.customOnSurfaceB);
    read_cf("color_theme_surface_variant_r", c.appearance.customSurfaceVariantR);
    read_cf("color_theme_surface_variant_g", c.appearance.customSurfaceVariantG);
    read_cf("color_theme_surface_variant_b", c.appearance.customSurfaceVariantB);
    read_cf("color_theme_on_surface_variant_r", c.appearance.customOnSurfaceVariantR);
    read_cf("color_theme_on_surface_variant_g", c.appearance.customOnSurfaceVariantG);
    read_cf("color_theme_on_surface_variant_b", c.appearance.customOnSurfaceVariantB);
    read_cf("color_theme_outline_r", c.appearance.customOutlineR);
    read_cf("color_theme_outline_g", c.appearance.customOutlineG);
    read_cf("color_theme_outline_b", c.appearance.customOutlineB);
    read_cf("color_theme_shadow_r", c.appearance.customShadowR);
    read_cf("color_theme_shadow_g", c.appearance.customShadowG);
    read_cf("color_theme_shadow_b", c.appearance.customShadowB);
    read_cf("color_theme_hover_r", c.appearance.customHoverR);
    read_cf("color_theme_hover_g", c.appearance.customHoverG);
    read_cf("color_theme_hover_b", c.appearance.customHoverB);
    read_cf("color_theme_on_hover_r", c.appearance.customOnHoverR);
    read_cf("color_theme_on_hover_g", c.appearance.customOnHoverG);
    read_cf("color_theme_on_hover_b", c.appearance.customOnHoverB);
  }

  if (const auto* pw = root.get_as<toml::table>("power")) {
    if (auto v = (*pw)["display_sleep"].value<bool>()) c.power.displaySleep = *v;
    if (auto v = (*pw)["display_sleep_timeout"].value<int64_t>())
      c.power.displaySleepTimeoutMin = static_cast<int>(std::clamp(*v, INT64_C(1), INT64_C(120)));
    if (auto v = (*pw)["idle_suspend"].value<bool>()) c.power.idleSuspend = *v;
    if (auto v = (*pw)["idle_suspend_timeout"].value<int64_t>())
      c.power.idleSuspendTimeoutMin = static_cast<int>(std::clamp(*v, INT64_C(5), INT64_C(240)));
    if (auto v = (*pw)["power_button_action"].value<int64_t>())
      c.power.powerButtonAction = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto v = (*pw)["lid_close_action"].value<int64_t>())
      c.power.lidCloseAction = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(2)));
    if (auto v = (*pw)["show_battery_percentage"].value<bool>()) c.power.showBatteryPercentage = *v;
    if (auto s = (*pw)["tuned_profile"].value<std::string>()) c.power.tunedProfile = *s;
    if (auto v = (*pw)["epp"].value<int64_t>()) c.power.epp = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(4)));
  }

  if (const auto* vb = root.get_as<toml::table>("vram_boost")) {
    if (auto v = (*vb)["enabled"].value<bool>()) c.vramBoost.enabled = *v;
    if (auto v = (*vb)["only_fullscreen"].value<bool>()) c.vramBoost.onlyFullscreen = *v;
  }

  if (const auto* nl = root.get_as<toml::table>("nightlight")) {
    if (auto v = (*nl)["enabled"].value<bool>()) c.nightLight.enabled = *v;
    if (auto v = (*nl)["day_temperature"].value<int64_t>())
      c.nightLight.dayTemperature = static_cast<int>(std::clamp(*v, INT64_C(4000), INT64_C(6500)));
    if (auto v = (*nl)["night_temperature"].value<int64_t>())
      c.nightLight.nightTemperature = static_cast<int>(std::clamp(*v, INT64_C(2500), INT64_C(4000)));
    if (auto v = (*nl)["schedule_mode"].value<int64_t>())
      c.nightLight.scheduleMode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(2)));
    if (auto v = (*nl)["schedule_start_min"].value<int64_t>())
      c.nightLight.scheduleStartMin = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1439)));
    if (auto v = (*nl)["schedule_end_min"].value<int64_t>())
      c.nightLight.scheduleEndMin = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1439)));
  }

  if (const auto* idleTbl = root.get_as<toml::table>("idle")) {
    IdleSettings is;
    if (const auto* bhv = (*idleTbl)["behavior"].as_table()) {
      for (const auto& [name, node] : *bhv) {
        const auto* bTbl = node.as_table();
        if (!bTbl) continue;
        IdleBehaviorConfig ib;
        ib.name = name.str();
        if (auto v = (*bTbl)["timeout"].value<int64_t>()) ib.timeout_sec = static_cast<int>(*v);
        if (auto s = (*bTbl)["command"].value<std::string>()) ib.command = *s;
        if (auto s = (*bTbl)["resume_command"].value<std::string>()) ib.resume_command = *s;
        if (auto v = (*bTbl)["enabled"].value<bool>()) ib.enabled = *v;
        if (ib.timeout_sec > 0) is.behaviors.push_back(std::move(ib));
      }
    }
    c.idle = std::move(is);
  }

  if (const auto* au = root.get_as<toml::table>("audio")) {
    if (auto s = (*au)["default_sink_name"].value<std::string>()) c.audio.default_sink_name = *s;
    if (auto s = (*au)["default_source_name"].value<std::string>()) c.audio.default_source_name = *s;
    if (auto v = (*au)["default_sink_volume_pct"].value<int64_t>()) c.audio.default_sink_volume_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*au)["default_sink_muted"].value<bool>()) c.audio.default_sink_muted = *v;
    if (auto v = (*au)["default_source_volume_pct"].value<int64_t>()) c.audio.default_source_volume_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = (*au)["default_source_muted"].value<bool>()) c.audio.default_source_muted = *v;
    if (auto v = (*au)["engine_clock_rate_hz"].value<int64_t>()) c.audio.engine_clock_rate_hz = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(384000)));
    if (auto v = (*au)["engine_force_rate_hz"].value<int64_t>()) c.audio.engine_force_rate_hz = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(384000)));
    if (const auto* a = (*au)["engine_allowed_rates_hz"].as_array()) {
      std::vector<int> rates;
      for (const auto& el : *a) {
        if (auto v = el.value<int64_t>()) rates.push_back(static_cast<int>(*v));
      }
      c.audio.engine_allowed_rates_hz = std::move(rates);
    }
    if (auto v = (*au)["compat_pcm_format"].value<int64_t>()) c.audio.compat_pcm_format = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
  }

  if (const auto* wtab = root.get_as<toml::table>("widget")) {
    for (const auto& [name, node] : *wtab) {
      const auto* entryTbl = node.as_table();
      if (entryTbl == nullptr) continue;
      WidgetInstanceConfig wi;
      if (auto tv = (*entryTbl)["type"].value<std::string>()) {
        wi.type = *tv;
      } else {
        wi.type = std::string(name.str());
      }
      for (const auto& [key, val] : *entryTbl) {
        if (key == "type") continue;
        const std::string ks(key.str());
        if (const auto* s = val.as_string()) {
          wi.settings[ks] = s->get();
        } else if (const auto* b = val.as_boolean()) {
          wi.settings[ks] = b->get() ? std::string("true") : std::string("false");
        } else if (const auto* i = val.as_integer()) {
          wi.settings[ks] = std::to_string(i->get());
        } else if (const auto* f = val.as_floating_point()) {
          wi.settings[ks] = std::to_string(f->get());
        } else if (const auto* arr = val.as_array()) {
          std::string joined;
          for (const auto& item : *arr) {
            if (auto sv = item.value<std::string>()) {
              if (!joined.empty()) joined += ',';
              joined += *sv;
            }
          }
          wi.settings[ks] = joined;
        }
      }
      c.widgets[std::string(name.str())] = std::move(wi);
    }
  }
}

// Component IDs and the umbrella section each lives under in settings.toml.
struct ComponentInfo { std::string_view name; std::string_view section; };
constexpr ComponentInfo kComponents[] = {
  {"general",       "general"},
  {"shell",         "shell"},
  {"desktop",       "desktop"},
  {"autostart",     "autostart"},
  {"dock",          "dock"},
  {"taskbar",       "taskbar"},
  {"appearance",    "appearance"},
  {"wallpaper",     "wallpaper"},
  {"notifications", "notifications"},
  {"keyboard",      "keyboard"},
  {"audio",         "audio"},
  {"power",         "power"},
  {"vram_boost",    "vram_boost"},
  {"nightlight",    "nightlight"},
  {"time",          "time"},
  {"default_apps",  "default_apps"},
  {"mpris",         "mpris"},
  {"widgets",       "widget"},    // config lives in widgets/widgets.toml but nests under [widget] here
  {"idle",          "idle"},
  {"file_browser",  "file_browser"},
  {"live_wallpaper","live_wallpaper"},
};

toml::table merge_declarative_and_state() {
   
  toml::table merged;
  const std::string cfgDir = declarative_config_dir();
  if (!cfgDir.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(cfgDir, ec)) {
      std::vector<std::filesystem::path> files;
      for (const auto& entry : std::filesystem::directory_iterator(cfgDir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".toml") files.push_back(entry.path());
      }
      std::sort(files.begin(), files.end());
      for (const auto& p : files) {
        try {
          toml::table tbl = toml::parse_file(p.string());
          deep_merge_toml(merged, tbl);
        } catch (const toml::parse_error&) {
        }
      }
    }
  }

  const std::string stPath = state_settings_toml_path();
  if (!stPath.empty() && std::filesystem::exists(stPath)) {
    try {
      toml::table st = toml::parse_file(stPath);
      deep_merge_toml(merged, st);
    } catch (const toml::parse_error&) {
    }
  }

  // Component-specific files win over the umbrella settings file.
  for (const auto& ci : kComponents) {
    const std::string cp = state_component_toml_path(ci.name);
    if (!cp.empty() && std::filesystem::exists(cp)) {
      try {
        toml::table comp = toml::parse_file(cp);
        if (ci.name == "live_wallpaper") {
          // Older live-wallpaper TOMLs wrote their wallpaper settings under a
          // "wallpaper" key. Drop that key here so it can't clobber the plain
          // wallpaper app's setup — the two programs are separate.
          comp.erase("wallpaper");
        }
        deep_merge_toml(merged, comp);
      } catch (const toml::parse_error&) {
      }
    }
  }

  return merged;
}

// Copies a freshly computed palette from `src` into `dst` when the generator
// actually produced colors. async_matugen_update falls back to this on a reload
// race so a config change landing mid-generation can't strand the UI on stock
// colors; mirrors the preserve-palette logic in shell_config_invalidate_light.
void adopt_palette_into_uncached(ShellAppearance& dst, const ShellAppearance& src) {
  if (src.matugenPaletteOk) {
    dst.matugenDockFillR = src.matugenDockFillR;
    dst.matugenDockFillG = src.matugenDockFillG;
    dst.matugenDockFillB = src.matugenDockFillB;
    dst.matugenPanelFillR = src.matugenPanelFillR;
    dst.matugenPanelFillG = src.matugenPanelFillG;
    dst.matugenPanelFillB = src.matugenPanelFillB;
    dst.matugenDrawerDimR = src.matugenDrawerDimR;
    dst.matugenDrawerDimG = src.matugenDrawerDimG;
    dst.matugenDrawerDimB = src.matugenDrawerDimB;
    dst.matugenOutlineR = src.matugenOutlineR;
    dst.matugenOutlineG = src.matugenOutlineG;
    dst.matugenOutlineB = src.matugenOutlineB;
    dst.matugenAccentR = src.matugenAccentR;
    dst.matugenAccentG = src.matugenAccentG;
    dst.matugenAccentB = src.matugenAccentB;
    dst.matugenTextR = src.matugenTextR;
    dst.matugenTextG = src.matugenTextG;
    dst.matugenTextB = src.matugenTextB;
    dst.matugenNotifCriticalBgR = src.matugenNotifCriticalBgR;
    dst.matugenNotifCriticalBgG = src.matugenNotifCriticalBgG;
    dst.matugenNotifCriticalBgB = src.matugenNotifCriticalBgB;
    dst.matugenNotifCriticalOutlineR = src.matugenNotifCriticalOutlineR;
    dst.matugenNotifCriticalOutlineG = src.matugenNotifCriticalOutlineG;
    dst.matugenNotifCriticalOutlineB = src.matugenNotifCriticalOutlineB;
    dst.matugenPaletteOk = true;
  }
  if (src.horizonColorsPaletteOk) {
    dst.hcDockFillR = src.hcDockFillR;
    dst.hcDockFillG = src.hcDockFillG;
    dst.hcDockFillB = src.hcDockFillB;
    dst.hcPanelFillR = src.hcPanelFillR;
    dst.hcPanelFillG = src.hcPanelFillG;
    dst.hcPanelFillB = src.hcPanelFillB;
    dst.hcDrawerDimR = src.hcDrawerDimR;
    dst.hcDrawerDimG = src.hcDrawerDimG;
    dst.hcDrawerDimB = src.hcDrawerDimB;
    dst.hcOutlineR = src.hcOutlineR;
    dst.hcOutlineG = src.hcOutlineG;
    dst.hcOutlineB = src.hcOutlineB;
    dst.hcAccentR = src.hcAccentR;
    dst.hcAccentG = src.hcAccentG;
    dst.hcAccentB = src.hcAccentB;
    dst.hcTextR = src.hcTextR;
    dst.hcTextG = src.hcTextG;
    dst.hcTextB = src.hcTextB;
    dst.hcNotifCriticalBgR = src.hcNotifCriticalBgR;
    dst.hcNotifCriticalBgG = src.hcNotifCriticalBgG;
    dst.hcNotifCriticalBgB = src.hcNotifCriticalBgB;
    dst.hcNotifCriticalOutlineR = src.hcNotifCriticalOutlineR;
    dst.hcNotifCriticalOutlineG = src.hcNotifCriticalOutlineG;
    dst.hcNotifCriticalOutlineB = src.hcNotifCriticalOutlineB;
    dst.horizonColorsPaletteOk = true;
  }
}

void async_matugen_update(const ShellConfig& base, std::uint64_t gen) {
    
  ShellConfig cfg = base;
  debug_log("config", "async_matugen_update: enter gen=%llu matugen=%d hc_native=%d matugen_ok=%d hc_ok=%d",
            static_cast<unsigned long long>(gen),
            cfg.appearance.matugenThemingEnabled ? 1 : 0,
            cfg.appearance.horizonColorsNative ? 1 : 0,
            cfg.appearance.matugenPaletteOk ? 1 : 0,
            cfg.appearance.horizonColorsPaletteOk ? 1 : 0);
  eh::matugen::refresh_wallpaper_derived_palette(cfg.appearance,
                                                    normalize_wallpaper_path_for_matugen(cfg.wallpaperImage));
  debug_log("config", "async_matugen_update: after refresh matugen_ok=%d hc_ok=%d",
            cfg.appearance.matugenPaletteOk ? 1 : 0,
            cfg.appearance.horizonColorsPaletteOk ? 1 : 0);
  eh::matugen::apply_external_matugen_templates(cfg);
  eh::horizon_colors::apply_native_templates(cfg);
  sync_hyprland_border_matugen(cfg.appearance);
  if (gen != g_load_gen.load(std::memory_order_acquire)) {
    // A reload landed while we were regenerating — the template files written
    // above can trip the config watcher and bump the load generation, so the
    // full commit below has to be skipped (the newer config wins wholesale).
    // The palette we just computed is still valid for this wallpaper though, so
    // keep those colors alive in the live cache rather than leaving the UI on
    // stock blue until a later reload regenerates the palette.
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_cache && g_cache->wallpaperImage == cfg.wallpaperImage) {
      adopt_palette_into_uncached(g_cache->appearance, cfg.appearance);
      if (g_cache_no_matugen) {
        adopt_palette_into_uncached(g_cache_no_matugen->appearance, cfg.appearance);
      }
    }
    return;
  }
  ShellConfigAppliedHookFn hook_fn = nullptr;
  void* hook_user = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (gen != g_load_gen.load(std::memory_order_relaxed)) return;
    g_cache = std::move(cfg);
    if (g_cache) g_cache_no_matugen = *g_cache;
    hook_fn = g_applied_hook;
    hook_user = g_applied_hook_user;
  }
  if (hook_fn) {
    DeferredCall::callLater([hook_fn, hook_user]() { hook_fn(hook_user); });
  }
}

ShellConfig load_uncached(bool skip_matugen = false) {
    
  debug_log("config", "load_uncached: BEGIN skip_matugen=%d", skip_matugen);
  const auto t0 = std::chrono::steady_clock::now();
  ShellConfig c;
  apply_dock_defaults_if_needed(c);
  apply_taskbar_defaults_if_needed(c);
  const auto t_after_defaults = std::chrono::steady_clock::now();

  toml::table merged = merge_declarative_and_state();
  const auto t_after_merge_files = std::chrono::steady_clock::now();
  if (!merged.empty()) {
    apply_toml_overlay(c, merged);
  }
  const auto t_after_overlay = std::chrono::steady_clock::now();

  apply_dock_defaults_if_needed(c);
  apply_taskbar_defaults_if_needed(c);
  normalize_legacy_tray_tokens(c);
  dock_migrate_legacy_autosep_slots(c);
  ensure_builtin_tray_widget(c);
  ensure_builtin_media_widget(c);
  ensure_builtin_spacer_widget(c);
  ensure_builtin_trash_widget(c);
  const auto t_after_widgets = std::chrono::steady_clock::now();

  if (const char* er = std::getenv("EH_SHELL_RENDERER")) {
    c.renderer = parse_shell_renderer_string(std::string_view(er));
  }
  const auto t_before_matugen = std::chrono::steady_clock::now();
  if (!skip_matugen) {
    eh::matugen::refresh_wallpaper_derived_palette(c.appearance, normalize_wallpaper_path_for_matugen(c.wallpaperImage));
    eh::matugen::apply_external_matugen_templates(c);
    eh::horizon_colors::apply_native_templates(c);
    sync_hyprland_border_matugen(c.appearance);
  }
  const auto t_end = std::chrono::steady_clock::now();

  if (eh::settings::trace::bench()) {
    const int64_t us_defaults = std::chrono::duration_cast<std::chrono::microseconds>(t_after_defaults - t0).count();
    const int64_t us_merge = std::chrono::duration_cast<std::chrono::microseconds>(t_after_merge_files - t_after_defaults).count();
    const int64_t us_overlay = std::chrono::duration_cast<std::chrono::microseconds>(t_after_overlay - t_after_merge_files).count();
    const int64_t us_wfix = std::chrono::duration_cast<std::chrono::microseconds>(t_after_widgets - t_after_overlay).count();
    const int64_t us_matugen = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_before_matugen).count();
    const int64_t us_total = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t0).count();
    std::cerr << "[settings-bench] load_uncached defaults=" << us_defaults << "us merge_declarative_state_toml=" << us_merge
              << "us apply_toml_overlay=" << us_overlay << "us widget_defaults_normalize=" << us_wfix << "us matugen_refresh=" << us_matugen
              << "us total=" << us_total << "us matugen_ok=" << (c.appearance.matugenPaletteOk ? 1 : 0)
              << " matugen_on=" << (c.appearance.matugenThemingEnabled ? 1 : 0) << " scheme=\"" << c.appearance.matugenScheme
              << "\" mode=\"" << c.appearance.matugenMode << "\"\n";
  }
  return c;
}

}

std::string declarative_config_dir() {
   
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
    if (xdg[0]) return std::string(xdg) + "/event-horizon";
  }
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.config/event-horizon";
  return {};
}

std::string state_event_horizon_dir() {
   
  if (const char* xdg = std::getenv("XDG_STATE_HOME")) {
    if (xdg[0]) return std::string(xdg) + "/event-horizon";
  }
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state/event-horizon";
  return "/tmp/event-horizon";
}

std::string state_settings_toml_path() { return state_event_horizon_dir() + "/settings.toml"; }
std::string state_file_browser_toml_path() { return state_component_toml_path("file_browser"); }

std::string state_component_toml_dir(std::string_view component) {
  return state_event_horizon_dir() + "/" + std::string(component);
}
std::string state_component_toml_path(std::string_view component) {
  return state_component_toml_dir(component) + "/" + std::string(component) + ".toml";
}
std::string state_general_toml_path()   { return state_component_toml_path("general"); }
std::string state_shell_toml_path()     { return state_component_toml_path("shell"); }
std::string state_desktop_toml_path()   { return state_component_toml_path("desktop"); }
std::string state_autostart_toml_path() { return state_component_toml_path("autostart"); }
std::string state_dock_toml_path()      { return state_component_toml_path("dock"); }
std::string state_taskbar_toml_path()   { return state_component_toml_path("taskbar"); }
std::string state_appearance_toml_path(){ return state_component_toml_path("appearance"); }
std::string state_wallpaper_toml_path() { return state_component_toml_path("wallpaper"); }
std::string state_notifications_toml_path() { return state_component_toml_path("notifications"); }
std::string state_keyboard_toml_path()  { return state_component_toml_path("keyboard"); }
std::string state_audio_toml_path()     { return state_component_toml_path("audio"); }
std::string state_power_toml_path()     { return state_component_toml_path("power"); }
std::string state_nightlight_toml_path(){ return state_component_toml_path("nightlight"); }
std::string state_time_toml_path()      { return state_component_toml_path("time"); }
std::string state_default_apps_toml_path() { return state_component_toml_path("default_apps"); }
std::string state_mpris_toml_path()     { return state_component_toml_path("mpris"); }
std::string state_widgets_toml_path()   { return state_component_toml_path("widgets"); }
std::string state_idle_toml_path()      { return state_component_toml_path("idle"); }

namespace {

// Writes a toml::table to disk atomically (temp file + rename).
bool write_toml_file(const toml::table& tbl, const std::string& path) {
   
  debug_log("settings", "write_toml_file: path=%s", path.c_str());
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  const std::string tmp = path + ".__ehtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Event Horizon component config — auto-generated.\n\n";
    out << tbl;
    if (!out.good()) { out.close(); (void)std::filesystem::remove(tmp, ec); return false; }
    out.flush();
    if (!out.good()) { out.close(); (void)std::filesystem::remove(tmp, ec); return false; }
    out.close();
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    (void)std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

std::optional<timespec> file_mtime(const std::string& p) {
   
  struct stat st {};
  if (stat(p.c_str(), &st) != 0) return std::nullopt;
#if defined(__APPLE__)
  return st.st_mtimespec;
#else
  return st.st_mtim;
#endif
}

void bump_mtime(std::optional<timespec>& best, const std::string& p) {
   
  if (auto t = file_mtime(p)) {
    if (!best || t->tv_sec > best->tv_sec || (t->tv_sec == best->tv_sec && t->tv_nsec > best->tv_nsec)) best = t;
  }
}

toml::array str_vec_to_array(const std::vector<std::string>& v) {
   
  toml::array a;
  for (const auto& s : v) a.push_back(s);
  return a;
}

}

std::optional<timespec> aggregate_config_source_mtime() {
   
  std::optional<timespec> best;
  for (const auto& ci : kComponents) {
    bump_mtime(best, state_component_toml_path(ci.name));
  }
  const std::string cfg = declarative_config_dir();
  if (!cfg.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(cfg, ec)) {
      for (const auto& entry : std::filesystem::directory_iterator(cfg, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".toml") continue;
        bump_mtime(best, entry.path().string());
      }
    }
  }
  return best;
}

std::string widget_implementation_type(std::string_view instance_id) {
   
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_cache) g_cache = load_uncached();
  const std::string id(instance_id);
  const auto it = g_cache->widgets.find(id);
  if (it != g_cache->widgets.end()) return it->second.type;
  return id;
}

bool widget_token_is_system_tray(std::string_view token) {
   
  if (token == "system_tray" || token == "tray") return true;
  return widget_implementation_type(token) == "system_tray";
}

bool widget_instance_enabled(const ShellConfig& sc, std::string_view instance_id) {
   
  const std::string id(instance_id);
  const auto it = sc.widgets.find(id);
  if (it == sc.widgets.end()) return true;
  const auto jt = it->second.settings.find("enabled");
  if (jt == it->second.settings.end()) return true;
  const std::string& v = jt->second;
  if (v.empty()) return true;
  const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(v.front())));
  if (c == '0' || c == 'f' || c == 'n') return false;
  return true;
}

std::string workspaces_widget_instance_id(const ShellConfig& sc) {
   
  auto scan = [&](const std::vector<std::string>& v) -> std::string {
    for (const auto& id : v)
      if (widget_implementation_type(id) == "workspaces") return id;
    return {};
  };
  if (auto w = scan(sc.dock.leftWidgets); !w.empty()) return w;
  if (auto w = scan(sc.dock.centerWidgets); !w.empty()) return w;
  if (auto w = scan(sc.dock.rightWidgets); !w.empty()) return w;
  return "workspaces";
}

void merge_widget_overrides_from_state_file(ShellConfig& merged) {
   
  const std::string finalPath = state_component_toml_path("widgets");
  if (finalPath.empty() || !std::filesystem::exists(finalPath)) return;
  try {
    toml::table old = toml::parse_file(finalPath);
    if (const auto* wold = old.get_as<toml::table>("widget")) {
      toml::table wrapper;
      wrapper.insert_or_assign("widget", *wold);
      ShellConfig extras{};
      apply_toml_overlay(extras, wrapper);
      for (const auto& [k, v] : extras.widgets) {
        if (merged.widgets.find(k) == merged.widgets.end()) merged.widgets[k] = v;
      }
    }
  } catch (const toml::parse_error&) {
  }
}

bool write_state_settings_toml(const ShellConfig& c) {
    
  debug_log("settings", "write_state_settings_toml: BEGIN");
  ShellConfig merged = c;
  merge_widget_overrides_from_state_file(merged);

  const std::string icon = merged.dock.iconTheme;

  toml::table root;
  {
    toml::table general;
    general.insert_or_assign("icon_theme", icon);
    if (!merged.avatarPath.empty())
      general.insert_or_assign("avatar_path", merged.avatarPath);
    root.insert_or_assign("general", std::move(general));
  }
  {
    toml::table shell;
    std::string rstr = "cairo";
    if (merged.renderer == ShellRendererBackend::Vulkan) rstr = "vulkan";
    shell.insert_or_assign("renderer", std::move(rstr));
    shell.insert_or_assign("ui_scale", merged.dock.shellUiScale);
    root.insert_or_assign("shell", std::move(shell));
  }
  {
    toml::table dock;
    dock.insert_or_assign("autohide", merged.dock.dockAutoHide);
    dock.insert_or_assign("show_dock", merged.dock.dockShowDock);
    dock.insert_or_assign("dock_widgets_enabled", merged.dock.dockWidgetsEnabled);
    dock.insert_or_assign("group_apps", merged.dock.dockGroupApps);
    dock.insert_or_assign("dock_tooltips_enabled", merged.dock.dockTooltipsEnabled);
    dock.insert_or_assign("dock_pinned_apps_tray_pill", merged.dock.dockPinnedAppsTrayPill);
    dock.insert_or_assign("dock_running_apps_tray_pill", merged.dock.dockRunningAppsTrayPill);
    dock.insert_or_assign("radius", static_cast<int64_t>(merged.dock.dockRadius));
    dock.insert_or_assign("icon_size", static_cast<int64_t>(merged.dock.dockIconSize));
    dock.insert_or_assign("icon_spacing", static_cast<int64_t>(merged.dock.dockIconSpacing));
    dock.insert_or_assign("bottom_gap", static_cast<int64_t>(merged.dock.dockBottomGap));
    dock.insert_or_assign("exclusive_zone_gap", static_cast<int64_t>(merged.dock.dockExclusiveZoneGap));
    dock.insert_or_assign("border_enabled", merged.dock.dockBorderEnabled);
    dock.insert_or_assign("border_size", static_cast<int64_t>(merged.dock.dockBorderSize));
    dock.insert_or_assign("border_hue", static_cast<int64_t>(merged.dock.dockBorderHue));
    dock.insert_or_assign("border_opacity", static_cast<int64_t>(merged.dock.dockBorderOpacity));
    dock.insert_or_assign("opacity", static_cast<int64_t>(merged.dock.dockOpacity));
    dock.insert_or_assign("slot_pill_opacity", static_cast<int64_t>(merged.dock.slotPillOpacity));
    dock.insert_or_assign("liquid_glass", merged.dock.dockLiquidGlass);
    dock.insert_or_assign("colored_glass", merged.dock.dockColoredGlass);
    dock.insert_or_assign("scale", merged.dock.dockScale);
    dock.insert_or_assign("bar_follows_icons", merged.dock.dockBarFollowsIcons);
    dock.insert_or_assign("manual_bar_height", static_cast<int64_t>(merged.dock.dockManualBarHeightPx));
    dock.insert_or_assign("pinned_apps", str_vec_to_array(merged.dock.pinnedApps));
    dock.insert_or_assign("start_menu_pinned_apps", str_vec_to_array(merged.dock.startMenuPinnedApps));
    dock.insert_or_assign("drawer_pinned_apps", str_vec_to_array(merged.dock.drawerPinnedApps));
    dock.insert_or_assign("left_widgets", str_vec_to_array(merged.dock.leftWidgets));
    dock.insert_or_assign("center_widgets", str_vec_to_array(merged.dock.centerWidgets));
    dock.insert_or_assign("right_widgets", str_vec_to_array(merged.dock.rightWidgets));
    dock.insert_or_assign("output", merged.dock.outputName.empty() ? std::string("") : merged.dock.outputName);
    root.insert_or_assign("dock", std::move(dock));
  }
  {
    toml::table tb;
    tb.insert_or_assign("enabled", merged.taskbar.enabled);
    tb.insert_or_assign("width_mode", static_cast<int64_t>(merged.taskbar.widthMode));
    tb.insert_or_assign("height", static_cast<int64_t>(merged.taskbar.height));
    tb.insert_or_assign("radius", static_cast<int64_t>(merged.taskbar.radius));
    tb.insert_or_assign("opacity", static_cast<int64_t>(merged.taskbar.opacity));
    tb.insert_or_assign("icon_size", static_cast<int64_t>(merged.taskbar.iconSize));
    tb.insert_or_assign("icon_spacing", static_cast<int64_t>(merged.taskbar.iconSpacing));
    tb.insert_or_assign("floating_amount", static_cast<int64_t>(merged.taskbar.floatingAmount));
    tb.insert_or_assign("edge_gap", static_cast<int64_t>(merged.taskbar.edgeGap));
    tb.insert_or_assign("exclusive_zone_gap", static_cast<int64_t>(merged.taskbar.exclusiveZoneGap));
    tb.insert_or_assign("scale", merged.taskbar.scale);
    tb.insert_or_assign("left_widgets", str_vec_to_array(merged.taskbar.leftWidgets));
    tb.insert_or_assign("center_widgets", str_vec_to_array(merged.taskbar.centerWidgets));
    tb.insert_or_assign("right_widgets", str_vec_to_array(merged.taskbar.rightWidgets));
    tb.insert_or_assign("pinned_apps", str_vec_to_array(merged.taskbar.pinnedApps));
    tb.insert_or_assign("group_apps", merged.taskbar.groupApps);
    tb.insert_or_assign("slot_pill_opacity", static_cast<int64_t>(merged.taskbar.slotPillOpacity));
    tb.insert_or_assign("auto_hide", merged.taskbar.autoHide);
    tb.insert_or_assign("tooltips", merged.taskbar.tooltipsEnabled);
    tb.insert_or_assign("pinned_apps_tray_pill", merged.taskbar.pinnedAppsTrayPill);
    tb.insert_or_assign("running_apps_tray_pill", merged.taskbar.runningAppsTrayPill);
    tb.insert_or_assign("widgets_enabled", merged.taskbar.widgetsEnabled);
    tb.insert_or_assign("border", merged.taskbar.border);
    tb.insert_or_assign("border_size", static_cast<int64_t>(merged.taskbar.borderSize));
    tb.insert_or_assign("icon_theme", merged.taskbar.iconTheme.empty() ? std::string("") : merged.taskbar.iconTheme);
    tb.insert_or_assign("position_top", merged.taskbar.positionTop);
    tb.insert_or_assign("output", merged.taskbar.outputName.empty() ? std::string("") : merged.taskbar.outputName);
    root.insert_or_assign("taskbar", std::move(tb));
  }
  {
    toml::table dt;
    dt.insert_or_assign("enabled", merged.desktopEnabled);
    dt.insert_or_assign("output", merged.desktopOutputName.empty() ? std::string("") : merged.desktopOutputName);
    root.insert_or_assign("desktop", std::move(dt));

    toml::table as;
    as.insert_or_assign("enabled", merged.autostartEnabled);
    root.insert_or_assign("autostart", std::move(as));
  }
  // The file browser moved to its own file_browser.toml, so there's no section here.
  {
    toml::table wp;
    wp.insert_or_assign("enabled", merged.wallpaperEnabled);
    wp.insert_or_assign("mode", static_cast<int64_t>(merged.wallpaperMode));
    wp.insert_or_assign("image", merged.wallpaperImage);
    wp.insert_or_assign("folder", merged.wallpaperFolder);
    wp.insert_or_assign("folder_picker_mode", static_cast<int64_t>(merged.wallpaperFolderPickerMode));
    wp.insert_or_assign("player_cmd", merged.wallpaperVideoPlayerCmd);
    wp.insert_or_assign("bing_enabled", merged.bingEnabled);
    wp.insert_or_assign("bing_daily_enabled", merged.bingDailyEnabled);
    wp.insert_or_assign("bing_download_path", merged.bingDownloadPath);
    wp.insert_or_assign("bing_filter", static_cast<int64_t>(merged.bingFilter));
    wp.insert_or_assign("bing_blocked_keywords", merged.bingBlockedKeywords);
    root.insert_or_assign("wallpaper", std::move(wp));
  }
  {
    toml::table pw;
    pw.insert_or_assign("display_sleep", merged.power.displaySleep);
    pw.insert_or_assign("display_sleep_timeout", static_cast<int64_t>(merged.power.displaySleepTimeoutMin));
    pw.insert_or_assign("idle_suspend", merged.power.idleSuspend);
    pw.insert_or_assign("idle_suspend_timeout", static_cast<int64_t>(merged.power.idleSuspendTimeoutMin));
    pw.insert_or_assign("power_button_action", static_cast<int64_t>(merged.power.powerButtonAction));
    pw.insert_or_assign("lid_close_action", static_cast<int64_t>(merged.power.lidCloseAction));
    pw.insert_or_assign("show_battery_percentage", merged.power.showBatteryPercentage);
    if (!merged.power.tunedProfile.empty())
      pw.insert_or_assign("tuned_profile", merged.power.tunedProfile);
    pw.insert_or_assign("epp", static_cast<int64_t>(merged.power.epp));
    root.insert_or_assign("power", std::move(pw));
  }
  {
    toml::table vb;
    vb.insert_or_assign("enabled", merged.vramBoost.enabled);
    vb.insert_or_assign("only_fullscreen", merged.vramBoost.onlyFullscreen);
    root.insert_or_assign("vram_boost", std::move(vb));
  }
  {
    toml::table nl;
    nl.insert_or_assign("enabled", merged.nightLight.enabled);
    nl.insert_or_assign("day_temperature", static_cast<int64_t>(merged.nightLight.dayTemperature));
    nl.insert_or_assign("night_temperature", static_cast<int64_t>(merged.nightLight.nightTemperature));
    nl.insert_or_assign("schedule_mode", static_cast<int64_t>(merged.nightLight.scheduleMode));
    nl.insert_or_assign("schedule_start_min", static_cast<int64_t>(merged.nightLight.scheduleStartMin));
    nl.insert_or_assign("schedule_end_min", static_cast<int64_t>(merged.nightLight.scheduleEndMin));
    root.insert_or_assign("nightlight", std::move(nl));
  }
  {
    toml::table mp;
    mp.insert_or_assign("blacklist", merged.mprisBlacklist);
    mp.insert_or_assign("preferred", merged.mprisPreferred);
    mp.insert_or_assign("now_playing_notify", merged.mprisNowPlayingNotify);
    root.insert_or_assign("mpris", std::move(mp));
  }
  {
    toml::table nt;
    nt.insert_or_assign("dbus", merged.notifications.dbusEnabled);
    nt.insert_or_assign("do_not_disturb", merged.notifications.doNotDisturb);
    nt.insert_or_assign("default_timeout_ms", static_cast<int64_t>(merged.notifications.defaultTimeoutMs));
    {
      toml::table toast;
      toast.insert_or_assign("layer_shell_enabled", merged.notifications.toast.layerShellEnabled);
      toast.insert_or_assign("margin_px", static_cast<int64_t>(merged.notifications.toast.marginPx));
      toast.insert_or_assign("corner_radius_px", static_cast<int64_t>(merged.notifications.toast.cornerRadiusPx));
      toast.insert_or_assign("max_width_px", static_cast<int64_t>(merged.notifications.toast.maxWidthPx));
      toast.insert_or_assign("scale_pct", static_cast<int64_t>(merged.notifications.toast.scalePct));
      toast.insert_or_assign("position", merged.notifications.toast.position);
      nt.insert_or_assign("toast", std::move(toast));
    }
    root.insert_or_assign("notifications", std::move(nt));
  }
  {
    const DefaultAppsSettings& d = merged.defaultApps;
    toml::table da;
    if (!d.web.empty()) da.insert_or_assign("web", d.web);
    if (!d.mail.empty()) da.insert_or_assign("mail", d.mail);
    if (!d.calendar.empty()) da.insert_or_assign("calendar", d.calendar);
    if (!d.fileManager.empty()) da.insert_or_assign("file_manager", d.fileManager);
    if (!d.terminal.empty()) da.insert_or_assign("terminal", d.terminal);
    if (!d.music.empty()) da.insert_or_assign("music", d.music);
    if (!d.video.empty()) da.insert_or_assign("video", d.video);
    if (!d.images.empty()) da.insert_or_assign("images", d.images);
    if (!d.pdf.empty()) da.insert_or_assign("pdf", d.pdf);
    if (!da.empty()) root.insert_or_assign("default_apps", std::move(da));
  }
  {
    toml::table tm;
    tm.insert_or_assign("use_24h", merged.time.use24h);
    tm.insert_or_assign("show_seconds", merged.time.showSeconds);
    tm.insert_or_assign("show_date", merged.time.showDate);
    tm.insert_or_assign("date_format", static_cast<int64_t>(merged.time.dateFormat));
    if (!merged.time.customFormat.empty())
      tm.insert_or_assign("custom_format", merged.time.customFormat);
    if (!merged.time.timezone.empty())
      tm.insert_or_assign("timezone", merged.time.timezone);
    root.insert_or_assign("time", std::move(tm));
  }
  {
    const KeyboardSettings& k = merged.keyboard;
    toml::table kb;
    kb.insert_or_assign("layout", k.layout);
    kb.insert_or_assign("layouts", str_vec_to_array(k.layouts));
    kb.insert_or_assign("switch_shortcut", static_cast<int64_t>(k.switchShortcut));
    kb.insert_or_assign("show_layout", k.showLayout);
    kb.insert_or_assign("numlock", k.numlock);
    kb.insert_or_assign("input_method", k.inputMethodEnabled);
    kb.insert_or_assign("caps_lock_behavior", static_cast<int64_t>(k.capsLockBehavior));
    kb.insert_or_assign("compose_key", static_cast<int64_t>(k.composeKey));
    kb.insert_or_assign("middle_click_paste", k.middleClickPaste);
    kb.insert_or_assign("repeat_rate", static_cast<int64_t>(k.repeatRate));
    kb.insert_or_assign("repeat_delay", static_cast<int64_t>(k.repeatDelay));
    root.insert_or_assign("keyboard", std::move(kb));
  }
  {
    const ShellAppearance& a = merged.appearance;
    toml::table ap;
    ap.insert_or_assign("overlay_opacity_advanced", a.overlayOpacityAdvanced);
    ap.insert_or_assign("overlay_opacity_master", static_cast<double>(a.overlayOpacityMaster));
    ap.insert_or_assign("overlay_opacity_control_center", static_cast<double>(a.overlayOpacityControlCenter));
    ap.insert_or_assign("overlay_opacity_control_center_inner", static_cast<double>(a.overlayOpacityControlCenterInner));
    ap.insert_or_assign("overlay_opacity_app_drawer", static_cast<double>(a.overlayOpacityAppDrawer));
    ap.insert_or_assign("overlay_opacity_launchpad", static_cast<double>(a.overlayOpacityLaunchpad));
    ap.insert_or_assign("overlay_opacity_settings", static_cast<double>(a.overlayOpacitySettings));
    ap.insert_or_assign("overlay_opacity_settings_sidebar", static_cast<double>(a.overlayOpacitySettingsSidebar));
    ap.insert_or_assign("overlay_opacity_dock_menu", static_cast<double>(a.overlayOpacityDockMenu));
    ap.insert_or_assign("overlay_opacity_desktop_menu", static_cast<double>(a.overlayOpacityDesktopMenu));
    ap.insert_or_assign("overlay_opacity_tray_menu", static_cast<double>(a.overlayOpacityTrayMenu));
    ap.insert_or_assign("overlay_opacity_calendar", static_cast<double>(a.overlayOpacityCalendar));
    ap.insert_or_assign("overlay_opacity_weather", static_cast<double>(a.overlayOpacityWeather));
    ap.insert_or_assign("overlay_opacity_tooltip", static_cast<double>(a.overlayOpacityTooltip));
    ap.insert_or_assign("overlay_opacity_widget_card", static_cast<double>(a.overlayOpacityWidgetCard));
    ap.insert_or_assign("overlay_opacity_notifications", static_cast<double>(a.overlayOpacityNotifications));
    ap.insert_or_assign("overlay_opacity_overview", static_cast<double>(a.overlayOpacityOverview));
    ap.insert_or_assign("launchpad_grid_columns", static_cast<int64_t>(a.launchpadGridColumns));
    ap.insert_or_assign("launchpad_grid_rows", static_cast<int64_t>(a.launchpadGridRows));
    ap.insert_or_assign("launchpad_cell_gap_px", static_cast<int64_t>(a.launchpadCellGapPx));
    ap.insert_or_assign("launchpad_icon_fill_pct", static_cast<int64_t>(a.launchpadIconFillPct));
    ap.insert_or_assign("launchpad_layout_scale_pct", static_cast<int64_t>(a.launchpadLayoutScalePct));
    ap.insert_or_assign("launchpad_dpi_scale_pct", static_cast<int64_t>(a.launchpadDpiScalePct));
    ap.insert_or_assign("launchpad_view_mode", static_cast<int64_t>(a.launchpadViewMode));
    ap.insert_or_assign("launchpad_folder_size_pct", static_cast<int64_t>(a.launchpadFolderSizePct));
    ap.insert_or_assign("launchpad_folder_gap_px", static_cast<int64_t>(a.launchpadFolderGapPx));
    ap.insert_or_assign("overview_axis", static_cast<int64_t>(a.overviewAxis));
    ap.insert_or_assign("overview_capture_mode", static_cast<int64_t>(a.overviewCaptureMode));
    ap.insert_or_assign("overview_live_updates", a.overviewLiveUpdates);
    ap.insert_or_assign("overview_multi_monitor", a.overviewMultiMonitor);
    ap.insert_or_assign("overview_card_scale_pct", static_cast<int64_t>(a.overviewCardScalePct));
    ap.insert_or_assign("overview_card_gap_px", static_cast<int64_t>(a.overviewCardGapPx));
    ap.insert_or_assign("overview_scroll_delay_ms", static_cast<int64_t>(a.overviewScrollDelayMs));
    ap.insert_or_assign("overview_close_btn_size_px", static_cast<int64_t>(a.overviewCloseBtnSizePx));
    ap.insert_or_assign("overview_search_width_px", static_cast<int64_t>(a.overviewSearchWidthPx));
    ap.insert_or_assign("matugen_theming", a.matugenThemingEnabled);
    ap.insert_or_assign("horizon_colors_native", a.horizonColorsNative);
    ap.insert_or_assign("matugen_scheme", a.matugenScheme.empty() ? std::string("scheme-content") : a.matugenScheme);
    ap.insert_or_assign("matugen_mode", a.matugenMode.empty() ? std::string("dark") : a.matugenMode);
    {
      const MatugenExternalTemplateToggles& m = a.matugenOutputs;
      toml::table mt;
      mt.insert_or_assign("run_bundled", m.runBundledToml);
      mt.insert_or_assign("niri", m.niri);
      mt.insert_or_assign("hyprland", m.hyprland);
      mt.insert_or_assign("mango", m.mango);
      mt.insert_or_assign("gtk_shell_css", m.gtkShellCss);
      mt.insert_or_assign("gtk_event_colors_light", m.gtkEventColorsLight);
      mt.insert_or_assign("gtk_event_colors_dark", m.gtkEventColorsDark);
      mt.insert_or_assign("kcolorscheme", m.kcolorscheme);
      mt.insert_or_assign("qt5ct", m.qt5ct);
      mt.insert_or_assign("qt6ct", m.qt6ct);
      mt.insert_or_assign("kitty_theme", m.kittyTheme);
      mt.insert_or_assign("kitty_tabs", m.kittyTabs);
      mt.insert_or_assign("ghostty", m.ghostty);
      mt.insert_or_assign("wezterm", m.wezterm);
      mt.insert_or_assign("alacritty", m.alacritty);
      mt.insert_or_assign("foot", m.foot);
      mt.insert_or_assign("otter_term", m.otterTerm);
      mt.insert_or_assign("btop", m.btop);
      mt.insert_or_assign("neovim", m.neovim);
      mt.insert_or_assign("vscode_material", m.vscodeMaterial);
      mt.insert_or_assign("vscode_color_themes", m.vscodeColorThemes);
      mt.insert_or_assign("firefox", m.firefox);
      mt.insert_or_assign("zenbrowser", m.zenbrowser);
      mt.insert_or_assign("vesktop", m.vesktop);
      mt.insert_or_assign("equibop", m.equibop);
      mt.insert_or_assign("pywalfox", m.pywalfox);
      mt.insert_or_assign("steam", m.steam);
      mt.insert_or_assign("dgop", m.dgop);
      mt.insert_or_assign("emacs", m.emacs);
      mt.insert_or_assign("zed", m.zed);
      mt.insert_or_assign("ptyxis", m.ptyxis);
      mt.insert_or_assign("horizon_files", m.horizonFiles);
      mt.insert_or_assign("horizon_photo", m.horizonPhoto);
      mt.insert_or_assign("horizon_calendar", m.horizonCalendar);
      mt.insert_or_assign("obs", m.obs);
      mt.insert_or_assign("heroic", m.heroic);
      mt.insert_or_assign("fluxer", m.fluxer);
      ap.insert_or_assign("matugen_templates", std::move(mt));
    }
    ap.insert_or_assign("color_theme_enabled", a.customThemeEnabled);
    ap.insert_or_assign("color_theme_name", a.activeThemeName);
    ap.insert_or_assign("color_theme_source", a.activeThemeSource);
    auto write_cf = [](float v) -> double { return static_cast<double>(v); };
    ap.insert_or_assign("color_theme_primary_r", write_cf(a.customPrimaryR));
    ap.insert_or_assign("color_theme_primary_g", write_cf(a.customPrimaryG));
    ap.insert_or_assign("color_theme_primary_b", write_cf(a.customPrimaryB));
    ap.insert_or_assign("color_theme_on_primary_r", write_cf(a.customOnPrimaryR));
    ap.insert_or_assign("color_theme_on_primary_g", write_cf(a.customOnPrimaryG));
    ap.insert_or_assign("color_theme_on_primary_b", write_cf(a.customOnPrimaryB));
    ap.insert_or_assign("color_theme_secondary_r", write_cf(a.customSecondaryR));
    ap.insert_or_assign("color_theme_secondary_g", write_cf(a.customSecondaryG));
    ap.insert_or_assign("color_theme_secondary_b", write_cf(a.customSecondaryB));
    ap.insert_or_assign("color_theme_on_secondary_r", write_cf(a.customOnSecondaryR));
    ap.insert_or_assign("color_theme_on_secondary_g", write_cf(a.customOnSecondaryG));
    ap.insert_or_assign("color_theme_on_secondary_b", write_cf(a.customOnSecondaryB));
    ap.insert_or_assign("color_theme_tertiary_r", write_cf(a.customTertiaryR));
    ap.insert_or_assign("color_theme_tertiary_g", write_cf(a.customTertiaryG));
    ap.insert_or_assign("color_theme_tertiary_b", write_cf(a.customTertiaryB));
    ap.insert_or_assign("color_theme_on_tertiary_r", write_cf(a.customOnTertiaryR));
    ap.insert_or_assign("color_theme_on_tertiary_g", write_cf(a.customOnTertiaryG));
    ap.insert_or_assign("color_theme_on_tertiary_b", write_cf(a.customOnTertiaryB));
    ap.insert_or_assign("color_theme_error_r", write_cf(a.customErrorR));
    ap.insert_or_assign("color_theme_error_g", write_cf(a.customErrorG));
    ap.insert_or_assign("color_theme_error_b", write_cf(a.customErrorB));
    ap.insert_or_assign("color_theme_on_error_r", write_cf(a.customOnErrorR));
    ap.insert_or_assign("color_theme_on_error_g", write_cf(a.customOnErrorG));
    ap.insert_or_assign("color_theme_on_error_b", write_cf(a.customOnErrorB));
    ap.insert_or_assign("color_theme_surface_r", write_cf(a.customSurfaceR));
    ap.insert_or_assign("color_theme_surface_g", write_cf(a.customSurfaceG));
    ap.insert_or_assign("color_theme_surface_b", write_cf(a.customSurfaceB));
    ap.insert_or_assign("color_theme_on_surface_r", write_cf(a.customOnSurfaceR));
    ap.insert_or_assign("color_theme_on_surface_g", write_cf(a.customOnSurfaceG));
    ap.insert_or_assign("color_theme_on_surface_b", write_cf(a.customOnSurfaceB));
    ap.insert_or_assign("color_theme_surface_variant_r", write_cf(a.customSurfaceVariantR));
    ap.insert_or_assign("color_theme_surface_variant_g", write_cf(a.customSurfaceVariantG));
    ap.insert_or_assign("color_theme_surface_variant_b", write_cf(a.customSurfaceVariantB));
    ap.insert_or_assign("color_theme_on_surface_variant_r", write_cf(a.customOnSurfaceVariantR));
    ap.insert_or_assign("color_theme_on_surface_variant_g", write_cf(a.customOnSurfaceVariantG));
    ap.insert_or_assign("color_theme_on_surface_variant_b", write_cf(a.customOnSurfaceVariantB));
    ap.insert_or_assign("color_theme_outline_r", write_cf(a.customOutlineR));
    ap.insert_or_assign("color_theme_outline_g", write_cf(a.customOutlineG));
    ap.insert_or_assign("color_theme_outline_b", write_cf(a.customOutlineB));
    ap.insert_or_assign("color_theme_shadow_r", write_cf(a.customShadowR));
    ap.insert_or_assign("color_theme_shadow_g", write_cf(a.customShadowG));
    ap.insert_or_assign("color_theme_shadow_b", write_cf(a.customShadowB));
    ap.insert_or_assign("color_theme_hover_r", write_cf(a.customHoverR));
    ap.insert_or_assign("color_theme_hover_g", write_cf(a.customHoverG));
    ap.insert_or_assign("color_theme_hover_b", write_cf(a.customHoverB));
    ap.insert_or_assign("color_theme_on_hover_r", write_cf(a.customOnHoverR));
    ap.insert_or_assign("color_theme_on_hover_g", write_cf(a.customOnHoverG));
    ap.insert_or_assign("color_theme_on_hover_b", write_cf(a.customOnHoverB));
    root.insert_or_assign("appearance", std::move(ap));
  }

  {
    toml::table au;
    if (!merged.audio.default_sink_name.empty())
      au.insert_or_assign("default_sink_name", merged.audio.default_sink_name);
    if (!merged.audio.default_source_name.empty())
      au.insert_or_assign("default_source_name", merged.audio.default_source_name);
    au.insert_or_assign("default_sink_volume_pct", static_cast<int64_t>(merged.audio.default_sink_volume_pct));
    au.insert_or_assign("default_sink_muted", merged.audio.default_sink_muted);
    au.insert_or_assign("default_source_volume_pct", static_cast<int64_t>(merged.audio.default_source_volume_pct));
    au.insert_or_assign("default_source_muted", merged.audio.default_source_muted);
    au.insert_or_assign("engine_clock_rate_hz", static_cast<int64_t>(merged.audio.engine_clock_rate_hz));
    au.insert_or_assign("engine_force_rate_hz", static_cast<int64_t>(merged.audio.engine_force_rate_hz));
    if (!merged.audio.engine_allowed_rates_hz.empty()) {
      toml::array rates;
      for (int r : merged.audio.engine_allowed_rates_hz) rates.push_back(static_cast<int64_t>(r));
      au.insert_or_assign("engine_allowed_rates_hz", std::move(rates));
    }
    au.insert_or_assign("compat_pcm_format", static_cast<int64_t>(merged.audio.compat_pcm_format));
    root.insert_or_assign("audio", std::move(au));
  }

  if (!merged.widgets.empty()) {
    toml::table wtab;
    for (const auto& [name, wc] : merged.widgets) {
      toml::table one;
      one.insert_or_assign("type", wc.type);
      for (const auto& [k, v] : wc.settings) one.insert_or_assign(k, v);
      wtab.insert_or_assign(name, std::move(one));
    }
    root.insert_or_assign("widget", std::move(wtab));
  }

  // Then persist the merged result as one TOML per component.
  debug_log("config", "write_state_settings_toml: writing component files");
  for (const auto& ci : kComponents) {
    if (auto* sec = root.get_as<toml::table>(ci.section)) {
      toml::table comp_root;
      comp_root.insert_or_assign(ci.section, *sec);
      const std::string cp = state_component_toml_path(ci.name);
      debug_log("config", "write_state_settings_toml:  component=%s path=%s", ci.name.data(), cp.c_str());
      const bool wrote = write_toml_file(comp_root, cp);
      debug_log("config", "write_state_settings_toml:  component=%s wrote=%d", ci.name.data(), (int)wrote);
    } else {
      debug_log("config", "write_state_settings_toml:  component=%s SKIP (no section in root)", ci.name.data());
    }
  }

  debug_log("config", "write_state_settings_toml: OK");
  return true;
}

// File-browser specific settings live in their own file (file_browser.toml).

FileBrowserSettings read_file_browser_toml() {
  FileBrowserSettings fb;
  const std::string path = state_file_browser_toml_path();
  if (!std::filesystem::exists(path)) return fb;
  try {
    toml::table tbl = toml::parse_file(path);
    if (auto d = tbl["zoom_pct"].value<double>()) {
      if (std::isfinite(*d)) fb.zoom_pct = std::clamp(*d, 50.0, 200.0);
    } else if (auto i = tbl["zoom_pct"].value<int64_t>()) {
      fb.zoom_pct = std::clamp(static_cast<double>(*i), 50.0, 200.0);
    }
    if (auto v = tbl["folders_before_files"].value<bool>()) fb.folders_before_files = *v;
    if (auto v = tbl["surface_opacity_pct"].value<int64_t>())
      fb.surface_opacity_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = tbl["sidebar_opacity_pct"].value<int64_t>())
      fb.sidebar_opacity_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = tbl["topbar_opacity_pct"].value<int64_t>())
      fb.topbar_opacity_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = tbl["statusbar_opacity_pct"].value<int64_t>())
      fb.statusbar_opacity_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto v = tbl["preview_opacity_pct"].value<int64_t>())
      fb.preview_opacity_pct = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(100)));
    if (auto s = tbl["default_terminal"].value<std::string>()) fb.default_terminal = *s;
    if (auto v = tbl["view_mode"].value<int64_t>()) fb.view_mode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1)));
    if (auto v = tbl["sort_field"].value<int64_t>()) fb.sort_field = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
    if (auto v = tbl["sort_descending"].value<bool>()) fb.sort_descending = *v;
    if (auto v = tbl["show_hidden"].value<bool>()) fb.show_hidden = *v;
    if (auto* pf = tbl.get_as<toml::table>("per_folder")) {
      for (auto& [key, val] : *pf) {
        if (auto* ent = val.as_table()) {
          FileBrowserSettings::PerFolder p;
          if (auto v = ent->get("view_mode")->value<int64_t>())
            p.view_mode = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(1)));
          if (auto v = ent->get("sort_field")->value<int64_t>())
            p.sort_field = static_cast<int>(std::clamp(*v, INT64_C(0), INT64_C(3)));
          if (auto v = ent->get("sort_descending")->value<bool>())
            p.sort_descending = *v;
          fb.per_folder[std::string(key.str())] = p;
        }
      }
    }
    if (auto* fav = tbl.get_as<toml::array>("favorites")) {
      for (auto& el : *fav) {
        if (auto s = el.value<std::string>())
          fb.favorites.push_back(*s);
      }
    }
    if (auto v = tbl["window_controls_left"].value<bool>()) fb.window_controls_left = *v;
  } catch (const toml::parse_error&) {}
  return fb;
}

std::string read_dock_icon_theme_from_disk() {
  // The icon theme lives in dock/dock.toml, with a fallback to general/general.toml.
  const std::string dockPath = state_component_toml_path("dock");
  if (!dockPath.empty() && std::filesystem::exists(dockPath)) {
    try {
      toml::table tbl = toml::parse_file(dockPath);
      if (auto* dock = tbl.get_as<toml::table>("dock")) {
        if (auto s = (*dock)["icon_theme"].value<std::string>())
          if (!s->empty()) return *s;
      }
    } catch (const toml::parse_error&) {}
  }
  const std::string genPath = state_component_toml_path("general");
  if (!genPath.empty() && std::filesystem::exists(genPath)) {
    try {
      toml::table tbl = toml::parse_file(genPath);
      if (auto* gen = tbl.get_as<toml::table>("general")) {
        if (auto s = (*gen)["icon_theme"].value<std::string>())
          if (!s->empty()) return *s;
      }
    } catch (const toml::parse_error&) {}
  }
  return {};
}

bool write_file_browser_toml(const FileBrowserSettings& fb) {
  const std::string finalPath = state_file_browser_toml_path();
  if (finalPath.empty()) return false;
  const std::string tmp = finalPath + ".__ehtmp";

  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(finalPath).parent_path(), ec);

  toml::table root;
  root.insert_or_assign("zoom_pct", fb.zoom_pct);
  root.insert_or_assign("folders_before_files", fb.folders_before_files);
  root.insert_or_assign("surface_opacity_pct", static_cast<int64_t>(fb.surface_opacity_pct));
  root.insert_or_assign("sidebar_opacity_pct", static_cast<int64_t>(fb.sidebar_opacity_pct));
  root.insert_or_assign("topbar_opacity_pct", static_cast<int64_t>(fb.topbar_opacity_pct));
  root.insert_or_assign("statusbar_opacity_pct", static_cast<int64_t>(fb.statusbar_opacity_pct));
  root.insert_or_assign("preview_opacity_pct", static_cast<int64_t>(fb.preview_opacity_pct));
  root.insert_or_assign("default_terminal", fb.default_terminal);
  root.insert_or_assign("view_mode", static_cast<int64_t>(fb.view_mode));
  root.insert_or_assign("sort_field", static_cast<int64_t>(fb.sort_field));
  root.insert_or_assign("sort_descending", fb.sort_descending);
  root.insert_or_assign("show_hidden", fb.show_hidden);

  // Per-folder overrides.
  toml::table pf_tbl;
  for (const auto& [path, pf] : fb.per_folder) {
    toml::table ent;
    ent.insert_or_assign("view_mode", static_cast<int64_t>(pf.view_mode));
    ent.insert_or_assign("sort_field", static_cast<int64_t>(pf.sort_field));
    ent.insert_or_assign("sort_descending", pf.sort_descending);
    pf_tbl.insert_or_assign(path, std::move(ent));
  }
  root.insert_or_assign("per_folder", std::move(pf_tbl));

  // Sidebar favorites (bookmarked folders).
  toml::array fav_arr;
  for (const auto& f : fb.favorites)
    fav_arr.push_back(f);
  root.insert_or_assign("favorites", std::move(fav_arr));
  root.insert_or_assign("window_controls_left", fb.window_controls_left);

  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Event Horizon File Browser settings.\n\n";
    out << root;
    if (!out.good()) { out.close(); (void)std::filesystem::remove(tmp, ec); return false; }
    out.flush();
    if (!out.good()) { out.close(); (void)std::filesystem::remove(tmp, ec); return false; }
    out.close();
  }

  if (std::rename(tmp.c_str(), finalPath.c_str()) != 0) {
    (void)std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

float control_center_inner_glass_alpha_scale(const ShellConfig& sc) {
   
  const ShellAppearance& a = sc.appearance;
  if (!a.overlayOpacityAdvanced) return std::clamp(a.overlayOpacityMaster, 0.f, 1.f);
  return std::clamp(a.overlayOpacityControlCenterInner, 0.f, 1.f);
}

ChromePaintColors derived_chrome_colors(const ShellAppearance& a) {
   
  ChromePaintColors c{};
  if (a.customThemeEnabled) {
    c.dockFillR = static_cast<double>(a.customSurfaceR);
    c.dockFillG = static_cast<double>(a.customSurfaceG);
    c.dockFillB = static_cast<double>(a.customSurfaceB);
    c.panelFillR = static_cast<double>(a.customSurfaceVariantR);
    c.panelFillG = static_cast<double>(a.customSurfaceVariantG);
    c.panelFillB = static_cast<double>(a.customSurfaceVariantB);
    c.drawerDimR = static_cast<double>(a.customSurfaceR) * 0.85;
    c.drawerDimG = static_cast<double>(a.customSurfaceG) * 0.85;
    c.drawerDimB = static_cast<double>(a.customSurfaceB) * 0.85;
    c.outlineR = static_cast<double>(a.customOutlineR);
    c.outlineG = static_cast<double>(a.customOutlineG);
    c.outlineB = static_cast<double>(a.customOutlineB);
    c.accentR = static_cast<double>(a.customPrimaryR);
    c.accentG = static_cast<double>(a.customPrimaryG);
    c.accentB = static_cast<double>(a.customPrimaryB);
    c.textR = static_cast<double>(a.customOnSurfaceR);
    c.textG = static_cast<double>(a.customOnSurfaceG);
    c.textB = static_cast<double>(a.customOnSurfaceB);
    c.notifCriticalBgR = static_cast<double>(a.customErrorR);
    c.notifCriticalBgG = static_cast<double>(a.customErrorG);
    c.notifCriticalBgB = static_cast<double>(a.customErrorB);
    c.notifCriticalOutlineR = static_cast<double>(a.customOutlineR);
    c.notifCriticalOutlineG = static_cast<double>(a.customOutlineG);
    c.notifCriticalOutlineB = static_cast<double>(a.customOutlineB);
    apply_color_adjustment(c.dockFillR, c.dockFillG, c.dockFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.panelFillR, c.panelFillG, c.panelFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.drawerDimR, c.drawerDimG, c.drawerDimB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.outlineR, c.outlineG, c.outlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.accentR, c.accentG, c.accentB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalBgR, c.notifCriticalBgG, c.notifCriticalBgB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalOutlineR, c.notifCriticalOutlineG, c.notifCriticalOutlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    return c;
  }
  if (a.horizonColorsNative && a.horizonColorsPaletteOk) {
    c.dockFillR = static_cast<double>(a.hcDockFillR);
    c.dockFillG = static_cast<double>(a.hcDockFillG);
    c.dockFillB = static_cast<double>(a.hcDockFillB);
    c.panelFillR = static_cast<double>(a.hcPanelFillR);
    c.panelFillG = static_cast<double>(a.hcPanelFillG);
    c.panelFillB = static_cast<double>(a.hcPanelFillB);
    c.drawerDimR = static_cast<double>(a.hcDrawerDimR);
    c.drawerDimG = static_cast<double>(a.hcDrawerDimG);
    c.drawerDimB = static_cast<double>(a.hcDrawerDimB);
    c.outlineR = static_cast<double>(a.hcOutlineR);
    c.outlineG = static_cast<double>(a.hcOutlineG);
    c.outlineB = static_cast<double>(a.hcOutlineB);
    c.accentR = static_cast<double>(a.hcAccentR);
    c.accentG = static_cast<double>(a.hcAccentG);
    c.accentB = static_cast<double>(a.hcAccentB);
    c.textR   = static_cast<double>(a.hcTextR);
    c.textG   = static_cast<double>(a.hcTextG);
    c.textB   = static_cast<double>(a.hcTextB);
    c.notifCriticalBgR = static_cast<double>(a.hcNotifCriticalBgR);
    c.notifCriticalBgG = static_cast<double>(a.hcNotifCriticalBgG);
    c.notifCriticalBgB = static_cast<double>(a.hcNotifCriticalBgB);
    c.notifCriticalOutlineR = static_cast<double>(a.hcNotifCriticalOutlineR);
    c.notifCriticalOutlineG = static_cast<double>(a.hcNotifCriticalOutlineG);
    c.notifCriticalOutlineB = static_cast<double>(a.hcNotifCriticalOutlineB);
    apply_color_adjustment(c.dockFillR, c.dockFillG, c.dockFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.panelFillR, c.panelFillG, c.panelFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.drawerDimR, c.drawerDimG, c.drawerDimB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.outlineR, c.outlineG, c.outlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.accentR, c.accentG, c.accentB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalBgR, c.notifCriticalBgG, c.notifCriticalBgB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalOutlineR, c.notifCriticalOutlineG, c.notifCriticalOutlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    return c;
  }
  if (a.matugenThemingEnabled && a.matugenPaletteOk) {
    c.dockFillR = static_cast<double>(a.matugenDockFillR);
    c.dockFillG = static_cast<double>(a.matugenDockFillG);
    c.dockFillB = static_cast<double>(a.matugenDockFillB);
    c.panelFillR = static_cast<double>(a.matugenPanelFillR);
    c.panelFillG = static_cast<double>(a.matugenPanelFillG);
    c.panelFillB = static_cast<double>(a.matugenPanelFillB);
    c.drawerDimR = static_cast<double>(a.matugenDrawerDimR);
    c.drawerDimG = static_cast<double>(a.matugenDrawerDimG);
    c.drawerDimB = static_cast<double>(a.matugenDrawerDimB);
    c.outlineR = static_cast<double>(a.matugenOutlineR);
    c.outlineG = static_cast<double>(a.matugenOutlineG);
    c.outlineB = static_cast<double>(a.matugenOutlineB);
    c.accentR = static_cast<double>(a.matugenAccentR);
    c.accentG = static_cast<double>(a.matugenAccentG);
    c.accentB = static_cast<double>(a.matugenAccentB);
    c.textR   = static_cast<double>(a.matugenTextR);
    c.textG   = static_cast<double>(a.matugenTextG);
    c.textB   = static_cast<double>(a.matugenTextB);
    c.notifCriticalBgR = static_cast<double>(a.matugenNotifCriticalBgR);
    c.notifCriticalBgG = static_cast<double>(a.matugenNotifCriticalBgG);
    c.notifCriticalBgB = static_cast<double>(a.matugenNotifCriticalBgB);
    c.notifCriticalOutlineR = static_cast<double>(a.matugenNotifCriticalOutlineR);
    c.notifCriticalOutlineG = static_cast<double>(a.matugenNotifCriticalOutlineG);
    c.notifCriticalOutlineB = static_cast<double>(a.matugenNotifCriticalOutlineB);
    apply_color_adjustment(c.dockFillR, c.dockFillG, c.dockFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.panelFillR, c.panelFillG, c.panelFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.drawerDimR, c.drawerDimG, c.drawerDimB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.outlineR, c.outlineG, c.outlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.accentR, c.accentG, c.accentB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalBgR, c.notifCriticalBgG, c.notifCriticalBgB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    apply_color_adjustment(c.notifCriticalOutlineR, c.notifCriticalOutlineG, c.notifCriticalOutlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
    return c;
  }
  c.dockFillR = 0.12;
  c.dockFillG = 0.12;
  c.dockFillB = 0.14;
  c.panelFillR = 0.08;
  c.panelFillG = 0.10;
  c.panelFillB = 0.13;
  c.drawerDimR = 0.11;
  c.drawerDimG = 0.11;
  c.drawerDimB = 0.13;
  c.outlineR = 0.55;
  c.outlineG = 0.60;
  c.outlineB = 0.62;
  c.accentR = 0.90;
  c.accentG = 0.90;
  c.accentB = 0.90;
  c.textR   = 0.92;
  c.textG   = 0.92;
  c.textB   = 0.95;
  c.notifCriticalBgR = 0.20;
  c.notifCriticalBgG = 0.08;
  c.notifCriticalBgB = 0.08;
  c.notifCriticalOutlineR = 0.75;
  c.notifCriticalOutlineG = 0.15;
  c.notifCriticalOutlineB = 0.15;
  apply_color_adjustment(c.dockFillR, c.dockFillG, c.dockFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.panelFillR, c.panelFillG, c.panelFillB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.drawerDimR, c.drawerDimG, c.drawerDimB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.outlineR, c.outlineG, c.outlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.accentR, c.accentG, c.accentB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.notifCriticalBgR, c.notifCriticalBgG, c.notifCriticalBgB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  apply_color_adjustment(c.notifCriticalOutlineR, c.notifCriticalOutlineG, c.notifCriticalOutlineB, a.colorBrightness, a.colorContrast, a.colorVibrance, a.colorGamma);
  return c;
}

float overlay_surface_alpha_scale(const ShellConfig& sc, OverlaySurfaceAlphaKind kind) {
   
  const ShellAppearance& a = sc.appearance;
  if (!a.overlayOpacityAdvanced) {

    if (kind == OverlaySurfaceAlphaKind::Launchpad)
      return std::clamp(a.overlayOpacityLaunchpad, 0.f, 1.f);
    return std::clamp(a.overlayOpacityMaster, 0.f, 1.f);
  }
  switch (kind) {
    case OverlaySurfaceAlphaKind::ControlCenter:
      return std::clamp(a.overlayOpacityControlCenter, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::AppDrawer:
      return std::clamp(a.overlayOpacityAppDrawer, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Launchpad:
      return std::clamp(a.overlayOpacityLaunchpad, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Settings:
      return std::clamp(a.overlayOpacitySettings, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::DockContextMenu:
      return std::clamp(a.overlayOpacityDockMenu, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::DesktopContextMenu:
      return std::clamp(a.overlayOpacityDesktopMenu, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::TrayMenu:
      return std::clamp(a.overlayOpacityTrayMenu, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Calendar:
      return std::clamp(a.overlayOpacityCalendar, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Weather:
      return std::clamp(a.overlayOpacityWeather, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Tooltip:
      return std::clamp(a.overlayOpacityTooltip, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Overview:
      return std::clamp(a.overlayOpacityOverview, 0.f, 1.f);
    case OverlaySurfaceAlphaKind::Notifications:
      return std::clamp(a.overlayOpacityNotifications, 0.f, 1.f);
    default:
      return 1.f;
  }
}

void shell_config_invalidate() {
   
  ++g_load_gen;
  std::lock_guard<std::mutex> lock(g_mu);
  g_cache.reset();
  g_cache_no_matugen.reset();
  g_drag_preview_active = false;
}

void shell_config_invalidate_light() {
    
  debug_log("config", "shell_config_invalidate_light");
  ++g_load_gen;
  std::lock_guard<std::mutex> lock(g_mu);

  ShellAppearance saved_matugen;
  bool has_matugen = false;
  ShellAppearance saved_hc;
  bool has_hc = false;
  if (g_cache) {
    saved_matugen = g_cache->appearance;
    has_matugen = saved_matugen.matugenPaletteOk;
    saved_hc = g_cache->appearance;
    has_hc = saved_hc.horizonColorsPaletteOk;
  }

  g_cache.reset();
  g_cache_no_matugen = load_uncached(true);

  if (has_matugen) {
    auto& a = g_cache_no_matugen->appearance;
    a.matugenDockFillR = saved_matugen.matugenDockFillR;
    a.matugenDockFillG = saved_matugen.matugenDockFillG;
    a.matugenDockFillB = saved_matugen.matugenDockFillB;
    a.matugenPanelFillR = saved_matugen.matugenPanelFillR;
    a.matugenPanelFillG = saved_matugen.matugenPanelFillG;
    a.matugenPanelFillB = saved_matugen.matugenPanelFillB;
    a.matugenDrawerDimR = saved_matugen.matugenDrawerDimR;
    a.matugenDrawerDimG = saved_matugen.matugenDrawerDimG;
    a.matugenDrawerDimB = saved_matugen.matugenDrawerDimB;
    a.matugenOutlineR = saved_matugen.matugenOutlineR;
    a.matugenOutlineG = saved_matugen.matugenOutlineG;
    a.matugenOutlineB = saved_matugen.matugenOutlineB;
    a.matugenAccentR = saved_matugen.matugenAccentR;
    a.matugenAccentG = saved_matugen.matugenAccentG;
    a.matugenAccentB = saved_matugen.matugenAccentB;
    a.matugenTextR = saved_matugen.matugenTextR;
    a.matugenTextG = saved_matugen.matugenTextG;
    a.matugenTextB = saved_matugen.matugenTextB;
    a.matugenNotifCriticalBgR = saved_matugen.matugenNotifCriticalBgR;
    a.matugenNotifCriticalBgG = saved_matugen.matugenNotifCriticalBgG;
    a.matugenNotifCriticalBgB = saved_matugen.matugenNotifCriticalBgB;
    a.matugenNotifCriticalOutlineR = saved_matugen.matugenNotifCriticalOutlineR;
    a.matugenNotifCriticalOutlineG = saved_matugen.matugenNotifCriticalOutlineG;
    a.matugenNotifCriticalOutlineB = saved_matugen.matugenNotifCriticalOutlineB;
    a.matugenPaletteOk = saved_matugen.matugenPaletteOk;
  }

  if (has_hc) {
    auto& a = g_cache_no_matugen->appearance;
    a.hcDockFillR = saved_hc.hcDockFillR;
    a.hcDockFillG = saved_hc.hcDockFillG;
    a.hcDockFillB = saved_hc.hcDockFillB;
    a.hcPanelFillR = saved_hc.hcPanelFillR;
    a.hcPanelFillG = saved_hc.hcPanelFillG;
    a.hcPanelFillB = saved_hc.hcPanelFillB;
    a.hcDrawerDimR = saved_hc.hcDrawerDimR;
    a.hcDrawerDimG = saved_hc.hcDrawerDimG;
    a.hcDrawerDimB = saved_hc.hcDrawerDimB;
    a.hcOutlineR = saved_hc.hcOutlineR;
    a.hcOutlineG = saved_hc.hcOutlineG;
    a.hcOutlineB = saved_hc.hcOutlineB;
    a.hcAccentR = saved_hc.hcAccentR;
    a.hcAccentG = saved_hc.hcAccentG;
    a.hcAccentB = saved_hc.hcAccentB;
    a.hcTextR = saved_hc.hcTextR;
    a.hcTextG = saved_hc.hcTextG;
    a.hcTextB = saved_hc.hcTextB;
    a.hcNotifCriticalBgR = saved_hc.hcNotifCriticalBgR;
    a.hcNotifCriticalBgG = saved_hc.hcNotifCriticalBgG;
    a.hcNotifCriticalBgB = saved_hc.hcNotifCriticalBgB;
    a.hcNotifCriticalOutlineR = saved_hc.hcNotifCriticalOutlineR;
    a.hcNotifCriticalOutlineG = saved_hc.hcNotifCriticalOutlineG;
    a.hcNotifCriticalOutlineB = saved_hc.hcNotifCriticalOutlineB;
    a.horizonColorsPaletteOk = saved_hc.horizonColorsPaletteOk;
  }

  g_cache = *g_cache_no_matugen;
  g_drag_preview_active = false;
  debug_log("config", "shell_config_invalidate_light: done load_gen=%d", g_load_gen.load());
}

void shell_config_apply_from_memory(ShellConfig sc) {
    
  debug_log("config", "shell_config_apply_from_memory: BEGIN load_gen=%d", g_load_gen.load() + 1);
  ++g_load_gen;
  ShellConfigAppliedHookFn hook_fn = nullptr;
  void* hook_user = nullptr;
  bool need_matugen_retrigger = false;
  bool need_hc_retrigger = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);

    if (g_cache && g_cache->appearance.matugenPaletteOk) {
      auto& a = sc.appearance;
      const auto& src = g_cache->appearance;
      a.matugenDockFillR = src.matugenDockFillR;
      a.matugenDockFillG = src.matugenDockFillG;
      a.matugenDockFillB = src.matugenDockFillB;
      a.matugenPanelFillR = src.matugenPanelFillR;
      a.matugenPanelFillG = src.matugenPanelFillG;
      a.matugenPanelFillB = src.matugenPanelFillB;
      a.matugenDrawerDimR = src.matugenDrawerDimR;
      a.matugenDrawerDimG = src.matugenDrawerDimG;
      a.matugenDrawerDimB = src.matugenDrawerDimB;
      a.matugenOutlineR = src.matugenOutlineR;
      a.matugenOutlineG = src.matugenOutlineG;
      a.matugenOutlineB = src.matugenOutlineB;
      a.matugenAccentR = src.matugenAccentR;
      a.matugenAccentG = src.matugenAccentG;
      a.matugenAccentB = src.matugenAccentB;
      a.matugenTextR = src.matugenTextR;
      a.matugenTextG = src.matugenTextG;
      a.matugenTextB = src.matugenTextB;
      a.matugenNotifCriticalBgR = src.matugenNotifCriticalBgR;
      a.matugenNotifCriticalBgG = src.matugenNotifCriticalBgG;
      a.matugenNotifCriticalBgB = src.matugenNotifCriticalBgB;
      a.matugenNotifCriticalOutlineR = src.matugenNotifCriticalOutlineR;
      a.matugenNotifCriticalOutlineG = src.matugenNotifCriticalOutlineG;
      a.matugenNotifCriticalOutlineB = src.matugenNotifCriticalOutlineB;
      a.matugenPaletteOk = src.matugenPaletteOk;
    } else if (sc.appearance.matugenThemingEnabled) {
      need_matugen_retrigger = true;
    }

    if (g_cache && g_cache->appearance.horizonColorsPaletteOk) {
      auto& a = sc.appearance;
      const auto& src = g_cache->appearance;
      a.hcDockFillR = src.hcDockFillR;
      a.hcDockFillG = src.hcDockFillG;
      a.hcDockFillB = src.hcDockFillB;
      a.hcPanelFillR = src.hcPanelFillR;
      a.hcPanelFillG = src.hcPanelFillG;
      a.hcPanelFillB = src.hcPanelFillB;
      a.hcDrawerDimR = src.hcDrawerDimR;
      a.hcDrawerDimG = src.hcDrawerDimG;
      a.hcDrawerDimB = src.hcDrawerDimB;
      a.hcOutlineR = src.hcOutlineR;
      a.hcOutlineG = src.hcOutlineG;
      a.hcOutlineB = src.hcOutlineB;
      a.hcAccentR = src.hcAccentR;
      a.hcAccentG = src.hcAccentG;
      a.hcAccentB = src.hcAccentB;
      a.hcTextR = src.hcTextR;
      a.hcTextG = src.hcTextG;
      a.hcTextB = src.hcTextB;
      a.hcNotifCriticalBgR = src.hcNotifCriticalBgR;
      a.hcNotifCriticalBgG = src.hcNotifCriticalBgG;
      a.hcNotifCriticalBgB = src.hcNotifCriticalBgB;
      a.hcNotifCriticalOutlineR = src.hcNotifCriticalOutlineR;
      a.hcNotifCriticalOutlineG = src.hcNotifCriticalOutlineG;
      a.hcNotifCriticalOutlineB = src.hcNotifCriticalOutlineB;
      a.horizonColorsPaletteOk = src.horizonColorsPaletteOk;
    } else if (sc.appearance.horizonColorsNative) {
      need_hc_retrigger = true;
    }

    g_cache = std::move(sc);
    if (g_cache) g_cache_no_matugen = *g_cache;
    else g_cache_no_matugen.reset();
    g_drag_preview_active = false;

    hook_fn = g_applied_hook;
    hook_user = g_applied_hook_user;
  }
  if (hook_fn) hook_fn(hook_user);
  if (need_matugen_retrigger) {
    shell_config_trigger_async_matugen();
  }
  if (need_hc_retrigger) {
    shell_config_trigger_async_matugen();
  }
  debug_log("config", "shell_config_apply_from_memory: OK load_gen=%d need_matugen=%d", g_load_gen.load(), need_matugen_retrigger);
}

void shell_config_reload_from_disk_now(bool skip_matugen) {
    
  debug_log("config", "shell_config_reload_from_disk_now skip_matugen=%d", skip_matugen);
  ++g_load_gen;
  std::lock_guard<std::mutex> lock(g_mu);
  g_drag_preview_active = false;
  g_cache = load_uncached(skip_matugen);
  g_cache_no_matugen.reset();
  // A skip-matugen reload leaves a palette-less cache. shell_config_snapshot
  // queues an async regeneration when it builds g_cache off the no-matugen
  // cache, but this path would otherwise strand colors on stock blue until the
  // next full palette reload — so re-kick the async palette update here.
  if (skip_matugen && g_cache &&
      (g_cache->appearance.matugenThemingEnabled || g_cache->appearance.horizonColorsNative)) {
    const std::uint64_t gen = g_load_gen.load(std::memory_order_relaxed);
    ThreadPool::instance().enqueue([cfg = *g_cache, gen]() mutable {
      async_matugen_update(cfg, gen);
    });
  }
}

void shell_config_restore_matugen_palette(const ShellAppearance& ap) {
   
  std::lock_guard<std::mutex> lock(g_mu);
  ShellConfig tmp;
  ShellConfig* target = g_cache ? &*g_cache : nullptr;
  if (!target) {
    if (!g_cache_no_matugen) return;
    tmp = *g_cache_no_matugen;
    target = &tmp;
  }
  auto& a = target->appearance;
  a.matugenDockFillR = ap.matugenDockFillR;
  a.matugenDockFillG = ap.matugenDockFillG;
  a.matugenDockFillB = ap.matugenDockFillB;
  a.matugenPanelFillR = ap.matugenPanelFillR;
  a.matugenPanelFillG = ap.matugenPanelFillG;
  a.matugenPanelFillB = ap.matugenPanelFillB;
  a.matugenDrawerDimR = ap.matugenDrawerDimR;
  a.matugenDrawerDimG = ap.matugenDrawerDimG;
  a.matugenDrawerDimB = ap.matugenDrawerDimB;
  a.matugenOutlineR = ap.matugenOutlineR;
  a.matugenOutlineG = ap.matugenOutlineG;
  a.matugenOutlineB = ap.matugenOutlineB;
  a.matugenAccentR = ap.matugenAccentR;
  a.matugenAccentG = ap.matugenAccentG;
  a.matugenAccentB = ap.matugenAccentB;
  a.matugenTextR = ap.matugenTextR;
  a.matugenTextG = ap.matugenTextG;
  a.matugenTextB = ap.matugenTextB;
  a.matugenNotifCriticalBgR = ap.matugenNotifCriticalBgR;
  a.matugenNotifCriticalBgG = ap.matugenNotifCriticalBgG;
  a.matugenNotifCriticalBgB = ap.matugenNotifCriticalBgB;
  a.matugenNotifCriticalOutlineR = ap.matugenNotifCriticalOutlineR;
  a.matugenNotifCriticalOutlineG = ap.matugenNotifCriticalOutlineG;
  a.matugenNotifCriticalOutlineB = ap.matugenNotifCriticalOutlineB;
  a.matugenPaletteOk = ap.matugenPaletteOk;
  if (!g_cache && target == &tmp) {
    g_cache = std::move(tmp);
  }
}

const ShellConfig& shell_config_snapshot() {
    
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_cache) {
    if (g_drag_preview_active) {
      return g_drag_preview_merge;
    }
    return *g_cache;
  }
  if (g_cache_no_matugen) {
    g_cache = *g_cache_no_matugen;
    if (g_cache && (g_cache->appearance.matugenThemingEnabled || g_cache->appearance.horizonColorsNative)) {
      const std::uint64_t gen = ++g_load_gen;
      ThreadPool::instance().enqueue([cfg = *g_cache_no_matugen, gen]() mutable {
        async_matugen_update(cfg, gen);
      });
    }
    return *g_cache;
  }
  g_cache_no_matugen = load_uncached(true);
  g_cache = *g_cache_no_matugen;
  const std::uint64_t gen = ++g_load_gen;
  ThreadPool::instance().enqueue([cfg = *g_cache_no_matugen, gen]() mutable {
    async_matugen_update(cfg, gen);
  });
  return *g_cache;
}

const ShellConfig& shell_config_snapshot_skip_matugen() {
    
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_cache_no_matugen) {
    g_cache_no_matugen = load_uncached(true);
  }
  return *g_cache_no_matugen;
}

void shell_config_trigger_async_matugen() {
   
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_cache_no_matugen) return;
  const std::uint64_t gen = ++g_load_gen;
  ThreadPool::instance().enqueue([cfg = *g_cache_no_matugen, gen]() mutable {
    async_matugen_update(cfg, gen);
  });
}

void shell_config_set_settings_drag_preview(const ShellConfig& ui_overlay, ShellConfigDragPreviewPatchFn patch,
                                            void* patch_user) {
   
  ShellConfig merged_work;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_cache) g_cache = load_uncached();
    merged_work = *g_cache;
    merge_settings_drag_fields_onto(merged_work, ui_overlay);
  }
  if (patch) patch(merged_work, patch_user);
  ShellConfigDragPreviewPaintTickFn tick_copy = nullptr;
  void* tick_user_copy = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_drag_preview_merge = std::move(merged_work);
    g_drag_preview_active = true;
    tick_copy = g_drag_preview_paint_tick;
    tick_user_copy = g_drag_preview_paint_tick_user;
  }
  if (tick_copy) tick_copy(tick_user_copy);
}

void shell_config_clear_settings_drag_preview() {
    
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_drag_preview_active && g_cache) {
    merge_settings_drag_fields_onto(*g_cache, g_drag_preview_merge);
  }
  g_drag_preview_active = false;
}

void shell_config_set_drag_preview_paint_tick(ShellConfigDragPreviewPaintTickFn fn, void* user) {
   
  std::lock_guard<std::mutex> lock(g_mu);
  g_drag_preview_paint_tick = fn;
  g_drag_preview_paint_tick_user = user;
}

void shell_config_clear_drag_preview_paint_tick() {
   
  std::lock_guard<std::mutex> lock(g_mu);
  g_drag_preview_paint_tick = nullptr;
  g_drag_preview_paint_tick_user = nullptr;
}

void shell_config_set_applied_hook(ShellConfigAppliedHookFn fn, void* user) {
   
  std::lock_guard<std::mutex> lock(g_mu);
  g_applied_hook = fn;
  g_applied_hook_user = user;
}

void shell_config_clear_applied_hook() {
   
  std::lock_guard<std::mutex> lock(g_mu);
  g_applied_hook = nullptr;
  g_applied_hook_user = nullptr;
}

}
