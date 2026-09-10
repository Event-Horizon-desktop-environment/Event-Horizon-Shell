#include <algorithm>
#include <functional>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <string_view>
#include <map>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "bootstrap/thread/thread_dispatch.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wallpaper/wallpaper_log.hpp"
#include "ux/settings/data/default_apps/settings_default_apps_apply.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "desktop_shell/common/log/debug_log.hpp"

#include <toml++/toml.hpp>

namespace {

static std::string widget_setting_simple(const eh::config::ShellConfig& sc, const std::string& instance_id,
                                         std::string_view key) {
  auto it = sc.widgets.find(instance_id);
  if (it == sc.widgets.end()) return {};
  auto jt = it->second.settings.find(std::string(key));
  if (jt == it->second.settings.end()) return {};
  return jt->second;
}

// Launchpad TOML state helpers.

static std::string launchpad_state_path() {
  return eh::config::state_component_toml_path("launchpad");
}

static void save_launchpad_settings_to_file(const eh::config::ShellConfig& sc) {
  const auto& a = sc.appearance;
  toml::table root;
  toml::table ap;
  ap.insert_or_assign("launchpad_grid_columns", static_cast<int64_t>(a.launchpadGridColumns));
  ap.insert_or_assign("launchpad_grid_rows", static_cast<int64_t>(a.launchpadGridRows));
  ap.insert_or_assign("launchpad_cell_gap_px", static_cast<int64_t>(a.launchpadCellGapPx));
  ap.insert_or_assign("launchpad_icon_fill_pct", static_cast<int64_t>(a.launchpadIconFillPct));
  ap.insert_or_assign("launchpad_layout_scale_pct", static_cast<int64_t>(a.launchpadLayoutScalePct));
  ap.insert_or_assign("launchpad_dpi_scale_pct", static_cast<int64_t>(a.launchpadDpiScalePct));
  ap.insert_or_assign("launchpad_view_mode", static_cast<int64_t>(a.launchpadViewMode));
  ap.insert_or_assign("launchpad_folder_size_pct", static_cast<int64_t>(a.launchpadFolderSizePct));
  ap.insert_or_assign("launchpad_folder_gap_px", static_cast<int64_t>(a.launchpadFolderGapPx));
  ap.insert_or_assign("overlay_opacity_launchpad", static_cast<double>(a.overlayOpacityLaunchpad));
  root.insert_or_assign("appearance", std::move(ap));
  // Preserve existing folder entries by re-reading the file
  try {
    toml::table existing = toml::parse_file(launchpad_state_path());
    if (const auto* folders = existing["folder"].as_array()) {
      root.insert_or_assign("folder", *folders);
    }
  } catch (const toml::parse_error&) {
  }
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(launchpad_state_path()).parent_path(), ec);
  std::ofstream ofs(launchpad_state_path());
  if (ofs) ofs << root << "\n";
}

static void load_launchpad_appearance_from_file(Settings& s) {
  try {
    toml::table tbl = toml::parse_file(launchpad_state_path());
    if (const auto* ap = tbl["appearance"].as_table()) {
      auto assign_int = [&](const char* key, int& out, int lo, int hi) {
        if (auto v = (*ap)[key].value<int64_t>())
          out = static_cast<int>(std::clamp(*v, static_cast<int64_t>(lo), static_cast<int64_t>(hi)));
      };
      assign_int("launchpad_grid_columns", s.launchpadGridColumns, 4, 12);
      assign_int("launchpad_grid_rows", s.launchpadGridRows, 3, 10);
      assign_int("launchpad_cell_gap_px", s.launchpadCellGapPx, 0, 24);
      assign_int("launchpad_icon_fill_pct", s.launchpadIconFillPct, 30, 95);
      assign_int("launchpad_layout_scale_pct", s.launchpadLayoutScalePct, 70, 150);
      assign_int("launchpad_dpi_scale_pct", s.launchpadDpiScalePct, 50, 300);
      assign_int("launchpad_view_mode", s.launchpadViewMode, 0, 1);
      assign_int("launchpad_folder_size_pct", s.launchpadFolderSizePct, 50, 200);
      assign_int("launchpad_folder_gap_px", s.launchpadFolderGapPx, 4, 48);
      if (auto d = (*ap)["overlay_opacity_launchpad"].value<double>()) {
        s.overlayOpacityLaunchpadPct = static_cast<int>(std::lround(std::clamp(*d, 0.0, 1.0) * 100.0));
      }
    }
  } catch (const toml::parse_error&) {
  }
}

static std::string desktop_widgets_state_path() {
  return eh::config::state_event_horizon_dir() + "/desktop_widgets.toml";
}

static void save_desktop_widgets_to_file(const Settings& s) {
  debug_log("settings", "save_desktop_widgets: count=%zu", s.desktopWidgets.size());
  toml::array arr;
  for (const auto& wc : s.desktopWidgets) {
    toml::table wt;
    wt.insert_or_assign("type", static_cast<int64_t>(static_cast<int>(wc.type)));
    wt.insert_or_assign("id", wc.id);
    if (!wc.widgetId.empty()) wt.insert_or_assign("widgetId", wc.widgetId);
    wt.insert_or_assign("posX", static_cast<int64_t>(wc.posX));
    wt.insert_or_assign("posY", static_cast<int64_t>(wc.posY));
    wt.insert_or_assign("scale", wc.scale);
    if (!wc.timeFormat.empty()) wt.insert_or_assign("timeFormat", wc.timeFormat);
    wt.insert_or_assign("showSeconds", wc.showSeconds);
    wt.insert_or_assign("showDate", wc.showDate);
    wt.insert_or_assign("fontSize", static_cast<int64_t>(wc.fontSize));
    if (!wc.outputName.empty()) wt.insert_or_assign("output", wc.outputName);
    arr.push_back(std::move(wt));
  }
  toml::table root;
  root.insert_or_assign("widget", std::move(arr));
  toml::array dslots;
  for (const auto& id : s.desktopWidgetSlotsDisabled) dslots.push_back(id);
  root.insert_or_assign("disabled_slots", std::move(dslots));
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(desktop_widgets_state_path()).parent_path(), ec);
  std::ofstream ofs(desktop_widgets_state_path());
  if (ofs) ofs << root << "\n";
}

static void load_desktop_widgets_from_file(Settings& s) {
  debug_log("settings", "load_desktop_widgets: BEGIN path=%s", desktop_widgets_state_path().c_str());
  try {
    toml::table tbl = toml::parse_file(desktop_widgets_state_path());
    if (const auto* arr = tbl["widget"].as_array()) {
      s.desktopWidgets.clear();
      for (const auto& el : *arr) {
        const auto* wt = el.as_table();
        if (!wt) continue;
        DesktopWidgetConfig wc;
        if (auto v = (*wt)["type"].value<int64_t>())
          wc.type = static_cast<DesktopWidgetType>(static_cast<int>(*v));
        if (auto s = (*wt)["id"].value<std::string>()) wc.id = *s;
        if (auto s = (*wt)["widgetId"].value<std::string>()) wc.widgetId = *s;
        if (auto v = (*wt)["posX"].value<int64_t>()) wc.posX = static_cast<int>(*v);
        if (auto v = (*wt)["posY"].value<int64_t>()) wc.posY = static_cast<int>(*v);
        if (auto d = (*wt)["scale"].value<double>()) wc.scale = *d;
        if (auto s = (*wt)["timeFormat"].value<std::string>()) wc.timeFormat = *s;
        if (auto v = (*wt)["showSeconds"].value<bool>()) wc.showSeconds = *v;
        if (auto v = (*wt)["showDate"].value<bool>()) wc.showDate = *v;
        if (auto v = (*wt)["fontSize"].value<int64_t>()) wc.fontSize = static_cast<int>(*v);
        if (auto s = (*wt)["output"].value<std::string>()) wc.outputName = *s;
        s.desktopWidgets.push_back(std::move(wc));
      }
    }
    s.desktopWidgetSlotsDisabled.clear();
    if (const auto* arr = tbl["disabled_slots"].as_array()) {
      for (const auto& el : *arr)
        if (auto v = el.value<std::string>()) s.desktopWidgetSlotsDisabled.insert(*v);
    }
    debug_log("settings", "load_desktop_widgets: loaded %zu widgets", s.desktopWidgets.size());
  } catch (const toml::parse_error& e) {
    debug_log("settings", "load_desktop_widgets: parse_error (no file or bad format)");
  }
}

} // namespace

// Lightweight reader for the horizon-desktop child: reads just the toggled-off
// desktop widget slot ids from desktop_widgets.toml without a full load_settings()
// (which re-parses every component and pulls the shell config snapshot).
std::unordered_set<std::string> load_desktop_widget_disabled_slots() {
  std::unordered_set<std::string> out;
  try {
    toml::table tbl = toml::parse_file(desktop_widgets_state_path());
    if (const auto* arr = tbl["disabled_slots"].as_array()) {
      for (const auto& el : *arr)
        if (auto v = el.value<std::string>()) out.insert(*v);
    }
  } catch (const toml::parse_error&) {
  }
  return out;
}

std::string join_list(const std::vector<std::string>& v) {
   
  std::string out;
  for (size_t i = 0; i < v.size(); i++) {
    if (i) out += ';';
    out += v[i];
  }
  return out;
}

std::string join_set(const std::unordered_set<std::string>& s) {
   
  std::string out;
  for (const auto& id : s) {
    if (!out.empty()) out += ';';
    out += id;
  }
  return out;
}

std::unordered_set<std::string> parse_semicolon_set(const std::string& s) {
   
  std::unordered_set<std::string> out;
  size_t pos = 0;
  while (pos < s.size()) {
    const size_t end = s.find(';', pos);
    const std::string id = s.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (!id.empty()) out.insert(id);
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return out;
}

std::string get_state_dir() {
  if (const char* d = std::getenv("XDG_STATE_HOME")) return d;
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

void patch_workspaces_widget_in_shell_config(eh::config::ShellConfig& sc, const Settings& s) {
   
  const std::string wid = eh::config::workspaces_widget_instance_id(sc);
  auto& w = sc.widgets[wid];
  if (w.type.empty()) w.type = "workspaces";
  w.settings["max_workspaces"] = std::to_string(s.workspacesMaxSlots);
  w.settings["show_apps"] = s.workspacesShowApps ? "true" : "false";
  w.settings["max_workspace_icons"] = std::to_string(s.workspacesMaxIcons);
}

bool widget_slot_enabled_for_settings(const std::unordered_set<std::string>& disabled, const std::string& id) {
   
  return disabled.find(id) == disabled.end();
}

void patch_widget_slot_enabled_into_shell_config(eh::config::ShellConfig& sc, const Settings& s) {

  std::unordered_set<std::string> merged = s.widgetSlotsDisabled;
  merged.insert(s.dockWidgetSlotsDisabled.begin(), s.dockWidgetSlotsDisabled.end());
  merged.insert(s.panelWidgetSlotsDisabled.begin(), s.panelWidgetSlotsDisabled.end());
  merged.insert(s.taskbarWidgetSlotsDisabled.begin(), s.taskbarWidgetSlotsDisabled.end());
  merged.insert(s.desktopWidgetSlotsDisabled.begin(), s.desktopWidgetSlotsDisabled.end());
  std::map<std::string, bool> logged;
  auto touch = [&](const std::string& id) {
    const bool off = merged.find(id) != merged.end();
    if (!logged.count(id)) {
      logged[id] = off;
      debug_log("settings",
                "patch_slot id=%s off=%d glob=%d dock=%d taskbar=%d desktop=%d panel=%d", id.c_str(),
                off ? 1 : 0, s.widgetSlotsDisabled.count(id) ? 1 : 0,
                s.dockWidgetSlotsDisabled.count(id) ? 1 : 0,
                s.taskbarWidgetSlotsDisabled.count(id) ? 1 : 0,
                s.desktopWidgetSlotsDisabled.count(id) ? 1 : 0,
                s.panelWidgetSlotsDisabled.count(id) ? 1 : 0);
    }
    if (off) {
      auto& wc = sc.widgets[id];
      if (wc.type.empty()) wc.type = eh::config::widget_implementation_type(id);
      wc.settings["enabled"] = "false";
    } else {
      const auto it = sc.widgets.find(id);
      if (it != sc.widgets.end()) it->second.settings.erase("enabled");
    }
  };
  for (const auto& id : s.leftWidgets) touch(id);
  for (const auto& id : s.centerWidgets) touch(id);
  for (const auto& id : s.rightWidgets) touch(id);
  for (const auto& id : s.taskbarLeftWidgets) touch(id);
  for (const auto& id : s.taskbarCenterWidgets) touch(id);
  for (const auto& id : s.taskbarRightWidgets) touch(id);
}

Settings load_settings() {
   
  debug_log("settings", "load_settings: BEGIN");
  const std::chrono::steady_clock::time_point t_ls0 = std::chrono::steady_clock::now();
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const std::chrono::steady_clock::time_point t_ls1 = std::chrono::steady_clock::now();
  Settings s;
  s.renderer = (sc.renderer == eh::config::ShellRendererBackend::Vulkan) ? "vulkan" : "cairo";
  s.dockShowDock = sc.dock.dockShowDock;
  s.dockAutoHide = sc.dock.dockAutoHide;
  s.dockWidgetsEnabled = sc.dock.dockWidgetsEnabled;
  s.dockGroupApps = sc.dock.dockGroupApps;
  s.dockTooltipsEnabled = sc.dock.dockTooltipsEnabled;
  s.dockPinnedAppsTrayPill = sc.dock.dockPinnedAppsTrayPill;
  s.dockRunningAppsTrayPill = sc.dock.dockRunningAppsTrayPill;
  s.dockScale = sc.dock.dockScale;
  s.uiScale = sc.dock.shellUiScale;
  s.dockBarFollowsIcons = sc.dock.dockBarFollowsIcons;
  s.dockManualBarHeightPx = sc.dock.dockManualBarHeightPx;
  s.dockRadius = sc.dock.dockRadius;
  s.dockIconSize = sc.dock.dockIconSize;
  s.dockIconSpacing = sc.dock.dockIconSpacing;
  s.dockBottomGap = sc.dock.dockBottomGap;
  s.dockExclusiveZoneGap = sc.dock.dockExclusiveZoneGap;
  s.dockBorderEnabled = sc.dock.dockBorderEnabled;
  s.dockBorderSize = sc.dock.dockBorderSize;
  s.dockBorderHue = sc.dock.dockBorderHue;
  s.dockBorderOpacity = sc.dock.dockBorderOpacity;
  s.dockOpacity = sc.dock.dockOpacity;
  s.slotPillOpacity = sc.dock.slotPillOpacity;
  s.dockLiquidGlass = sc.dock.dockLiquidGlass;
  s.dockColoredGlass = sc.dock.dockColoredGlass;
  s.iconTheme = sc.dock.iconTheme;
  s.pinnedApps = sc.dock.pinnedApps;
  s.drawerPinnedApps = sc.dock.drawerPinnedApps;
  s.startMenuPinnedApps = sc.dock.startMenuPinnedApps;
  s.leftWidgets = sc.dock.leftWidgets;
  s.centerWidgets = sc.dock.centerWidgets;
  s.rightWidgets = sc.dock.rightWidgets;
  s.taskbarEnabled = sc.taskbar.enabled;
  s.taskbarWidthMode = sc.taskbar.widthMode;
  s.taskbarHeight = sc.taskbar.height;
  s.taskbarRadius = sc.taskbar.radius;
  s.taskbarOpacity = sc.taskbar.opacity;
  s.taskbarIconSize = sc.taskbar.iconSize;
  s.taskbarIconSpacing = sc.taskbar.iconSpacing;
  s.taskbarFloatingAmount = sc.taskbar.floatingAmount;
  s.taskbarEdgeGap = sc.taskbar.edgeGap;
  s.taskbarExclusiveZoneGap = sc.taskbar.exclusiveZoneGap;
  s.taskbarScale = sc.taskbar.scale;
  s.taskbarPositionTop = sc.taskbar.positionTop;
  s.taskbarGroupApps = sc.taskbar.groupApps;
  s.taskbarSlotPillOpacity = sc.taskbar.slotPillOpacity;
  s.taskbarAutoHide = sc.taskbar.autoHide;
  s.taskbarTooltipsEnabled = sc.taskbar.tooltipsEnabled;
  s.taskbarPinnedAppsTrayPill = sc.taskbar.pinnedAppsTrayPill;
  s.taskbarRunningAppsTrayPill = sc.taskbar.runningAppsTrayPill;
  s.taskbarWidgetsEnabled = sc.taskbar.widgetsEnabled;
  s.taskbarBorder = sc.taskbar.border;
  s.taskbarBorderSize = sc.taskbar.borderSize;
  s.taskbarIconTheme = sc.taskbar.iconTheme;
  s.taskbarLeftWidgets = sc.taskbar.leftWidgets;
  s.taskbarCenterWidgets = sc.taskbar.centerWidgets;
  s.taskbarRightWidgets = sc.taskbar.rightWidgets;
  s.taskbarPinnedApps = sc.taskbar.pinnedApps;
  s.dockOutputName = sc.dock.outputName;
  s.taskbarOutputName = sc.taskbar.outputName;
  s.desktopWidgetsOutputName = sc.desktopWidgetsOutputName;
  s.plasmaTheme = sc.plasmaTheme;
  s.wallpaperEnabled = sc.wallpaperEnabled;
  s.wallpaperMode = sc.wallpaperMode;
  s.wallpaperImage = eh::wallpaper::normalize_wallpaper_path(sc.wallpaperImage);
  s.wallpaperFolder = eh::wallpaper::normalize_wallpaper_path(sc.wallpaperFolder);
  s.wallpaperFolderPickerMode = std::clamp(sc.wallpaperFolderPickerMode, 0, 3);
  s.wallpaperVideoPlayerCmd = sc.wallpaperVideoPlayerCmd;
  s.bingEnabled = sc.bingEnabled;
  s.bingDailyEnabled = sc.bingDailyEnabled;
  s.bingDownloadPath = sc.bingDownloadPath.empty() ? (std::string(getenv("HOME")) + "/Pictures/BingWallpaper") : sc.bingDownloadPath;
  s.bingFilter = sc.bingFilter;
  s.bingBlockedKeywords = sc.bingBlockedKeywords;

  {
    const std::string wid = eh::config::workspaces_widget_instance_id(sc);
    const std::string mw = widget_setting_simple(sc, wid, "max_workspaces");
    if (!mw.empty()) {
      char* end = nullptr;
      const long v = std::strtol(mw.c_str(), &end, 10);
      if (end != mw.c_str()) s.workspacesMaxSlots = static_cast<int>(std::clamp(v, 0L, 16L));
    }
    const std::string sa = widget_setting_simple(sc, wid, "show_apps");
    if (!sa.empty()) {
      if (sa == "1" || sa == "true" || sa == "True" || sa == "yes") s.workspacesShowApps = true;
      else if (sa == "0" || sa == "false" || sa == "False" || sa == "no") s.workspacesShowApps = false;
    }
    const std::string mi = widget_setting_simple(sc, wid, "max_workspace_icons");
    if (!mi.empty()) {
      char* end = nullptr;
      const long v = std::strtol(mi.c_str(), &end, 10);
      if (end != mi.c_str()) s.workspacesMaxIcons = static_cast<int>(std::clamp(v, 1L, 8L));
    }
  }
  {
    const eh::config::ShellAppearance& ap = sc.appearance;
    s.matugenThemingEnabled = ap.matugenThemingEnabled;
    s.horizonColorsNative = ap.horizonColorsNative;
    s.horizonColorsPaletteOk = ap.horizonColorsPaletteOk;
    s.hcAccentR = ap.hcAccentR;
    s.hcAccentG = ap.hcAccentG;
    s.hcAccentB = ap.hcAccentB;
    s.hcTextR = ap.hcTextR;
    s.hcTextG = ap.hcTextG;
    s.hcTextB = ap.hcTextB;
    s.hcSurfaceR = ap.hcDockFillR;
    s.hcSurfaceG = ap.hcDockFillG;
    s.hcSurfaceB = ap.hcDockFillB;
    s.hcOutlineR = ap.hcOutlineR;
    s.hcOutlineG = ap.hcOutlineG;
    s.hcOutlineB = ap.hcOutlineB;
    s.matugenScheme = eh::matugen::normalize_matugen_scheme(ap.matugenScheme);
    s.matugenMode = eh::matugen::normalize_matugen_mode(ap.matugenMode);
    s.matugenOutputs = ap.matugenOutputs;
    s.matugenPaletteOk = ap.matugenPaletteOk;
    s.matugenAccentR = ap.matugenAccentR;
    s.matugenAccentG = ap.matugenAccentG;
    s.matugenAccentB = ap.matugenAccentB;
    s.matugenTextR = ap.matugenTextR;
    s.matugenTextG = ap.matugenTextG;
    s.matugenTextB = ap.matugenTextB;
    s.matugenSurfaceR = ap.matugenDockFillR;
    s.matugenSurfaceG = ap.matugenDockFillG;
    s.matugenSurfaceB = ap.matugenDockFillB;
    s.matugenOutlineR = ap.matugenOutlineR;
    s.matugenOutlineG = ap.matugenOutlineG;
    s.matugenOutlineB = ap.matugenOutlineB;
    s.colorBrightnessPct = static_cast<int>(std::lround(ap.colorBrightness * 100.0));
    s.colorContrastPct = static_cast<int>(std::lround(ap.colorContrast * 100.0));
    s.colorVibrancePct = static_cast<int>(std::lround(ap.colorVibrance * 100.0));
    s.colorGammaPct = static_cast<int>(std::lround(ap.colorGamma * 100.0));
    s.colorThemeEnabled = ap.customThemeEnabled;
    s.colorThemeName = ap.activeThemeName;
    s.colorThemeSource = ap.activeThemeSource;
    s.themePrimaryR = ap.customPrimaryR;
    s.themePrimaryG = ap.customPrimaryG;
    s.themePrimaryB = ap.customPrimaryB;
    s.themeOnPrimaryR = ap.customOnPrimaryR;
    s.themeOnPrimaryG = ap.customOnPrimaryG;
    s.themeOnPrimaryB = ap.customOnPrimaryB;
    s.themeSecondaryR = ap.customSecondaryR;
    s.themeSecondaryG = ap.customSecondaryG;
    s.themeSecondaryB = ap.customSecondaryB;
    s.themeOnSecondaryR = ap.customOnSecondaryR;
    s.themeOnSecondaryG = ap.customOnSecondaryG;
    s.themeOnSecondaryB = ap.customOnSecondaryB;
    s.themeTertiaryR = ap.customTertiaryR;
    s.themeTertiaryG = ap.customTertiaryG;
    s.themeTertiaryB = ap.customTertiaryB;
    s.themeOnTertiaryR = ap.customOnTertiaryR;
    s.themeOnTertiaryG = ap.customOnTertiaryG;
    s.themeOnTertiaryB = ap.customOnTertiaryB;
    s.themeErrorR = ap.customErrorR;
    s.themeErrorG = ap.customErrorG;
    s.themeErrorB = ap.customErrorB;
    s.themeOnErrorR = ap.customOnErrorR;
    s.themeOnErrorG = ap.customOnErrorG;
    s.themeOnErrorB = ap.customOnErrorB;
    s.themeSurfaceR = ap.customSurfaceR;
    s.themeSurfaceG = ap.customSurfaceG;
    s.themeSurfaceB = ap.customSurfaceB;
    s.themeOnSurfaceR = ap.customOnSurfaceR;
    s.themeOnSurfaceG = ap.customOnSurfaceG;
    s.themeOnSurfaceB = ap.customOnSurfaceB;
    s.themeSurfaceVariantR = ap.customSurfaceVariantR;
    s.themeSurfaceVariantG = ap.customSurfaceVariantG;
    s.themeSurfaceVariantB = ap.customSurfaceVariantB;
    s.themeOnSurfaceVariantR = ap.customOnSurfaceVariantR;
    s.themeOnSurfaceVariantG = ap.customOnSurfaceVariantG;
    s.themeOnSurfaceVariantB = ap.customOnSurfaceVariantB;
    s.themeOutlineR = ap.customOutlineR;
    s.themeOutlineG = ap.customOutlineG;
    s.themeOutlineB = ap.customOutlineB;
    s.themeShadowR = ap.customShadowR;
    s.themeShadowG = ap.customShadowG;
    s.themeShadowB = ap.customShadowB;
    s.themeHoverR = ap.customHoverR;
    s.themeHoverG = ap.customHoverG;
    s.themeHoverB = ap.customHoverB;
    s.themeOnHoverR = ap.customOnHoverR;
    s.themeOnHoverG = ap.customOnHoverG;
    s.themeOnHoverB = ap.customOnHoverB;
    s.overlayOpacityAdvanced = ap.overlayOpacityAdvanced;
    auto pct = [](float f) {
      return static_cast<int>(std::lround(static_cast<double>(std::clamp(f, 0.f, 1.f)) * 100.0));
    };
    s.overlayOpacityMasterPct = pct(ap.overlayOpacityMaster);
    s.overlayOpacityCcPct = pct(ap.overlayOpacityControlCenter);
    s.overlayOpacityCcInnerPct = pct(ap.overlayOpacityControlCenterInner);
    s.overlayOpacityAppDrawerPct = pct(ap.overlayOpacityAppDrawer);
    s.overlayOpacityLaunchpadPct = pct(ap.overlayOpacityLaunchpad);
    s.overlayOpacitySettingsPct = pct(ap.overlayOpacitySettings);
    s.overlayOpacitySettingsSidebarPct = pct(ap.overlayOpacitySettingsSidebar);
    s.overlayOpacityDockMenuPct = pct(ap.overlayOpacityDockMenu);
    s.overlayOpacityDesktopMenuPct = pct(ap.overlayOpacityDesktopMenu);
    s.overlayOpacityTrayMenuPct = pct(ap.overlayOpacityTrayMenu);
    s.overlayOpacityCalendarPct = pct(ap.overlayOpacityCalendar);
    s.overlayOpacityWeatherPct = pct(ap.overlayOpacityWeather);
    s.overlayOpacityTooltipPct = pct(ap.overlayOpacityTooltip);
    s.overlayOpacityWidgetCardPct = pct(ap.overlayOpacityWidgetCard);
    s.overlayOpacityNotificationsPct = pct(ap.overlayOpacityNotifications);
    s.launchpadGridColumns = ap.launchpadGridColumns;
    s.launchpadGridRows = ap.launchpadGridRows;
    s.launchpadCellGapPx = ap.launchpadCellGapPx;
    s.launchpadIconFillPct = ap.launchpadIconFillPct;
    s.launchpadLayoutScalePct = ap.launchpadLayoutScalePct;
    s.launchpadDpiScalePct = ap.launchpadDpiScalePct;
    s.launchpadViewMode = ap.launchpadViewMode;
    s.launchpadFolderSizePct = ap.launchpadFolderSizePct;
    s.launchpadFolderGapPx = ap.launchpadFolderGapPx;
    s.overviewAxis = ap.overviewAxis;
    s.overviewCaptureMode = ap.overviewCaptureMode;
    s.overviewLiveUpdates = ap.overviewLiveUpdates;
    s.overviewMultiMonitor = ap.overviewMultiMonitor;
    s.overviewCardScalePct = ap.overviewCardScalePct;
    s.overviewCardGapPx = ap.overviewCardGapPx;
    s.overviewScrollDelayMs = ap.overviewScrollDelayMs;
    s.overviewCloseBtnSizePx = ap.overviewCloseBtnSizePx;
    s.overviewSearchWidthPx = ap.overviewSearchWidthPx;
  }
  // Launchpad.toml overrides for launcher-specific appearance settings
  // (Overrides settings.toml values after above snapshot copy)
  load_launchpad_appearance_from_file(s);
  s.nightlightEnabled = sc.nightLight.enabled;
  s.nightlightDayTemp = std::clamp(sc.nightLight.dayTemperature, 4000, 6500);
  s.nightlightNightTemp = std::clamp(sc.nightLight.nightTemperature, 2500, 4000);
  s.nightlightSchedule = std::clamp(sc.nightLight.scheduleMode, 0, 2);
  s.nightlightScheduleStart = std::clamp(sc.nightLight.scheduleStartMin, 0, 1439);
  s.nightlightScheduleEnd = std::clamp(sc.nightLight.scheduleEndMin, 0, 1439);
  s.desktopEnabled = sc.desktopEnabled;
  s.defaultApps = sc.defaultApps;
  s.timeUse24h = sc.time.use24h;
  s.timeShowSeconds = sc.time.showSeconds;
  s.timeShowDate = sc.time.showDate;
  s.timeDateFormat = sc.time.dateFormat;
  s.timeCustomFormat = sc.time.customFormat;
  s.timeTimezone = sc.time.timezone;
  s.powerDisplaySleep = sc.power.displaySleep;
  s.powerDisplaySleepTimeout = std::clamp(sc.power.displaySleepTimeoutMin, 1, 120);
  s.powerIdleSuspend = sc.power.idleSuspend;
  s.powerIdleSuspendTimeout = std::clamp(sc.power.idleSuspendTimeoutMin, 5, 240);
  s.powerPowerButtonAction = std::clamp(sc.power.powerButtonAction, 0, 3);
  s.powerLidCloseAction = std::clamp(sc.power.lidCloseAction, 0, 2);
  s.powerShowBatteryPercentage = sc.power.showBatteryPercentage;
  s.powerTunedProfile = sc.power.tunedProfile;
  s.powerEpp = sc.power.epp;
  s.notificationsDbusEnabled = sc.notifications.dbusEnabled;
  s.notificationsDoNotDisturb = sc.notifications.doNotDisturb;
  s.notificationsDefaultTimeoutMs = static_cast<int>(sc.notifications.defaultTimeoutMs);
  s.notificationsToastLayerShellEnabled = sc.notifications.toast.layerShellEnabled;
  s.notificationsToastPosition = sc.notifications.toast.position;
  s.notificationsToastMarginPx = std::clamp(sc.notifications.toast.marginPx, 8, 64);
  s.notificationsToastCornerRadiusPx = std::clamp(sc.notifications.toast.cornerRadiusPx, 0, 40);
  s.notificationsToastMaxWidthPx = std::clamp(sc.notifications.toast.maxWidthPx, 200, 900);
  s.notificationsScalePct = std::clamp(sc.notifications.toast.scalePct, 50, 200);
  s.keyboardLayout = sc.keyboard.layout;
  s.keyboardLayouts = sc.keyboard.layouts;
  s.keyboardSwitchShortcut = sc.keyboard.switchShortcut;
  s.keyboardShowLayout = sc.keyboard.showLayout;
  s.keyboardNumlock = sc.keyboard.numlock;
  s.keyboardInputMethodEnabled = sc.keyboard.inputMethodEnabled;
  s.keyboardCapsLockBehavior = sc.keyboard.capsLockBehavior;
  s.keyboardComposeKey = sc.keyboard.composeKey;
  s.keyboardMiddleClickPaste = sc.keyboard.middleClickPaste;
  s.keyboardRepeatRate = sc.keyboard.repeatRate;
  s.keyboardRepeatDelay = sc.keyboard.repeatDelay;
  s.avatarPath = sc.avatarPath;
  {
    auto mark_disabled_glob = [&](const std::vector<std::string>& v) {
      for (const auto& id : v) {
        if (!eh::config::widget_instance_enabled(sc, id)) s.widgetSlotsDisabled.insert(id);
      }
    };
    auto mark_disabled_per = [&](const std::vector<std::string>& v, std::unordered_set<std::string>& out) {
      for (const auto& id : v) {
        if (!eh::config::widget_instance_enabled(sc, id)) out.insert(id);
      }
    };
    mark_disabled_glob(s.leftWidgets);
    mark_disabled_glob(s.centerWidgets);
    mark_disabled_glob(s.rightWidgets);
    mark_disabled_glob(s.taskbarLeftWidgets);
    mark_disabled_glob(s.taskbarCenterWidgets);
    mark_disabled_glob(s.taskbarRightWidgets);
    mark_disabled_per(s.leftWidgets, s.dockWidgetSlotsDisabled);
    mark_disabled_per(s.centerWidgets, s.dockWidgetSlotsDisabled);
    mark_disabled_per(s.rightWidgets, s.dockWidgetSlotsDisabled);
    mark_disabled_per(s.taskbarLeftWidgets, s.taskbarWidgetSlotsDisabled);
    mark_disabled_per(s.taskbarCenterWidgets, s.taskbarWidgetSlotsDisabled);
    mark_disabled_per(s.taskbarRightWidgets, s.taskbarWidgetSlotsDisabled);
  }
  s.audioDefaultSinkName = sc.audio.default_sink_name;
  s.audioDefaultSourceName = sc.audio.default_source_name;
  s.audioDefaultSinkVolumePct = sc.audio.default_sink_volume_pct;
  s.audioDefaultSinkMuted = sc.audio.default_sink_muted;
  s.audioDefaultSourceVolumePct = sc.audio.default_source_volume_pct;
  s.audioDefaultSourceMuted = sc.audio.default_source_muted;
  s.audioEngineClockRateHz = sc.audio.engine_clock_rate_hz;
  s.audioEngineForceRateHz = sc.audio.engine_force_rate_hz;
  s.audioEngineAllowedRatesHz = sc.audio.engine_allowed_rates_hz;
  s.audioCompatPcmFormat = sc.audio.compat_pcm_format;

  load_desktop_widgets_from_file(s);
  debug_log("settings", "load_settings: desktop_widgets=%zu keyb_layout=%s tuned_profile=%s epp=%d avatar=%s",
            s.desktopWidgets.size(), s.keyboardLayout.c_str(), s.powerTunedProfile.c_str(),
            s.powerEpp, s.avatarPath.c_str());

  if (eh::settings::trace::bench()) {
    const auto t_ls_end = std::chrono::steady_clock::now();
    const int64_t us_snap =
        std::chrono::duration_cast<std::chrono::microseconds>(t_ls1 - t_ls0).count();
    const int64_t us_total =
        std::chrono::duration_cast<std::chrono::microseconds>(t_ls_end - t_ls0).count();
    std::cerr << "[settings-bench] load_settings shell_config_snapshot=" << us_snap << "us total_after_copy=" << us_total
              << "us matugen_ok=" << (sc.appearance.matugenPaletteOk ? 1 : 0)
              << " matugen_on=" << (sc.appearance.matugenThemingEnabled ? 1 : 0) << " scheme=\"" << sc.appearance.matugenScheme
              << "\" mode=\"" << sc.appearance.matugenMode << "\"\n";
  }
  return s;
}

eh::config::ShellConfig settings_to_shell_config(const Settings& s) {

  eh::config::ShellConfig sc{};
  sc.renderer = eh::config::parse_shell_renderer_string(s.renderer);
  sc.dock.dockShowDock = s.dockShowDock;
  sc.dock.dockAutoHide = s.dockAutoHide;
  sc.dock.dockWidgetsEnabled = s.dockWidgetsEnabled;
  sc.dock.dockGroupApps = s.dockGroupApps;
  sc.dock.dockTooltipsEnabled = s.dockTooltipsEnabled;
  sc.dock.dockPinnedAppsTrayPill = s.dockPinnedAppsTrayPill;
  sc.dock.dockRunningAppsTrayPill = s.dockRunningAppsTrayPill;
  sc.dock.dockScale = s.dockScale;
  sc.dock.shellUiScale = s.uiScale;
  sc.dock.dockBarFollowsIcons = s.dockBarFollowsIcons;
  sc.dock.dockManualBarHeightPx = s.dockManualBarHeightPx;
  sc.dock.dockRadius = s.dockRadius;
  sc.dock.dockIconSize = s.dockIconSize;
  sc.dock.dockIconSpacing = s.dockIconSpacing;
  sc.dock.dockBottomGap = s.dockBottomGap;
  sc.dock.dockExclusiveZoneGap = s.dockExclusiveZoneGap;
  sc.dock.dockBorderEnabled = s.dockBorderEnabled;
  sc.dock.dockBorderSize = s.dockBorderSize;
  sc.dock.dockBorderHue = s.dockBorderHue;
  sc.dock.dockBorderOpacity = s.dockBorderOpacity;
  sc.dock.dockOpacity = s.dockOpacity;
  sc.dock.slotPillOpacity = s.slotPillOpacity;
  sc.dock.dockLiquidGlass = s.dockLiquidGlass;
  sc.dock.dockColoredGlass = s.dockColoredGlass;
  sc.dock.iconTheme = s.iconTheme;
  sc.dock.pinnedApps = s.pinnedApps;
  sc.dock.drawerPinnedApps = s.drawerPinnedApps;
  sc.dock.startMenuPinnedApps = s.startMenuPinnedApps;
  sc.dock.leftWidgets = s.leftWidgets;
  sc.dock.centerWidgets = s.centerWidgets;
  sc.dock.rightWidgets = s.rightWidgets;
  sc.taskbar.enabled = s.taskbarEnabled;
  sc.taskbar.widthMode = s.taskbarWidthMode;
  sc.taskbar.height = s.taskbarHeight;
  sc.taskbar.radius = s.taskbarRadius;
  sc.taskbar.opacity = s.taskbarOpacity;
  sc.taskbar.iconSize = s.taskbarIconSize;
  sc.taskbar.iconSpacing = s.taskbarIconSpacing;
  sc.taskbar.floatingAmount = s.taskbarFloatingAmount;
  sc.taskbar.edgeGap = s.taskbarEdgeGap;
  sc.taskbar.exclusiveZoneGap = s.taskbarExclusiveZoneGap;
  sc.taskbar.scale = s.taskbarScale;
  sc.taskbar.positionTop = s.taskbarPositionTop;
  sc.taskbar.groupApps = s.taskbarGroupApps;
  sc.taskbar.slotPillOpacity = s.taskbarSlotPillOpacity;
  sc.taskbar.autoHide = s.taskbarAutoHide;
  sc.taskbar.tooltipsEnabled = s.taskbarTooltipsEnabled;
  sc.taskbar.pinnedAppsTrayPill = s.taskbarPinnedAppsTrayPill;
  sc.taskbar.runningAppsTrayPill = s.taskbarRunningAppsTrayPill;
  sc.taskbar.widgetsEnabled = s.taskbarWidgetsEnabled;
  sc.taskbar.border = s.taskbarBorder;
  sc.taskbar.borderSize = s.taskbarBorderSize;
  sc.taskbar.iconTheme = s.taskbarIconTheme;
  sc.taskbar.leftWidgets = s.taskbarLeftWidgets;
  sc.taskbar.centerWidgets = s.taskbarCenterWidgets;
  sc.taskbar.rightWidgets = s.taskbarRightWidgets;
  sc.taskbar.pinnedApps = s.taskbarPinnedApps;
  sc.dock.outputName = s.dockOutputName;
  sc.taskbar.outputName = s.taskbarOutputName;
  sc.desktopWidgetsOutputName = s.desktopWidgetsOutputName;
  sc.avatarPath = s.avatarPath;
  sc.plasmaTheme = s.plasmaTheme;
  sc.wallpaperEnabled = s.wallpaperEnabled;
  sc.wallpaperMode = s.wallpaperMode;
  sc.wallpaperImage = s.wallpaperImage;
  sc.wallpaperFolder = s.wallpaperFolder;
  sc.wallpaperFolderPickerMode = std::clamp(s.wallpaperFolderPickerMode, 0, 3);
  sc.wallpaperVideoPlayerCmd = s.wallpaperVideoPlayerCmd;
  sc.bingEnabled = s.bingEnabled;
  sc.bingDailyEnabled = s.bingDailyEnabled;
  sc.bingDownloadPath = s.bingDownloadPath;
  sc.bingFilter = s.bingFilter;
  sc.bingBlockedKeywords = s.bingBlockedKeywords;
  sc.appearance.matugenThemingEnabled = s.matugenThemingEnabled;
  sc.appearance.horizonColorsNative = s.horizonColorsNative;
  sc.appearance.horizonColorsPaletteOk = s.horizonColorsPaletteOk;
  sc.appearance.hcAccentR = s.hcAccentR;
  sc.appearance.hcAccentG = s.hcAccentG;
  sc.appearance.hcAccentB = s.hcAccentB;
  sc.appearance.hcTextR = s.hcTextR;
  sc.appearance.hcTextG = s.hcTextG;
  sc.appearance.hcTextB = s.hcTextB;
  sc.appearance.hcDockFillR = s.hcSurfaceR;
  sc.appearance.hcDockFillG = s.hcSurfaceG;
  sc.appearance.hcDockFillB = s.hcSurfaceB;
  sc.appearance.hcPanelFillR = s.hcSurfaceR;
  sc.appearance.hcPanelFillG = s.hcSurfaceG;
  sc.appearance.hcPanelFillB = s.hcSurfaceB;
  sc.appearance.hcOutlineR = s.hcOutlineR;
  sc.appearance.hcOutlineG = s.hcOutlineG;
  sc.appearance.hcOutlineB = s.hcOutlineB;
  sc.appearance.matugenScheme = eh::matugen::normalize_matugen_scheme(s.matugenScheme);
  sc.appearance.matugenMode = eh::matugen::normalize_matugen_mode(s.matugenMode);
  sc.appearance.matugenOutputs = s.matugenOutputs;
  sc.appearance.matugenPaletteOk = s.matugenPaletteOk;
  sc.appearance.matugenAccentR = s.matugenAccentR;
  sc.appearance.matugenAccentG = s.matugenAccentG;
  sc.appearance.matugenAccentB = s.matugenAccentB;
  sc.appearance.matugenTextR = s.matugenTextR;
  sc.appearance.matugenTextG = s.matugenTextG;
  sc.appearance.matugenTextB = s.matugenTextB;
  sc.appearance.matugenDockFillR = s.matugenSurfaceR;
  sc.appearance.matugenDockFillG = s.matugenSurfaceG;
  sc.appearance.matugenDockFillB = s.matugenSurfaceB;
  sc.appearance.matugenPanelFillR = s.matugenSurfaceR;
  sc.appearance.matugenPanelFillG = s.matugenSurfaceG;
  sc.appearance.matugenPanelFillB = s.matugenSurfaceB;
  sc.appearance.matugenOutlineR = s.matugenOutlineR;
  sc.appearance.matugenOutlineG = s.matugenOutlineG;
  sc.appearance.matugenOutlineB = s.matugenOutlineB;
  sc.appearance.colorBrightness = s.colorBrightnessPct / 100.f;
  sc.appearance.colorContrast = s.colorContrastPct / 100.f;
  sc.appearance.colorVibrance = s.colorVibrancePct / 100.f;
  sc.appearance.colorGamma = s.colorGammaPct / 100.f;
  sc.appearance.customThemeEnabled = s.colorThemeEnabled;
  sc.appearance.activeThemeName = s.colorThemeName;
  sc.appearance.activeThemeSource = s.colorThemeSource;
  sc.appearance.customPrimaryR = s.themePrimaryR;
  sc.appearance.customPrimaryG = s.themePrimaryG;
  sc.appearance.customPrimaryB = s.themePrimaryB;
  sc.appearance.customOnPrimaryR = s.themeOnPrimaryR;
  sc.appearance.customOnPrimaryG = s.themeOnPrimaryG;
  sc.appearance.customOnPrimaryB = s.themeOnPrimaryB;
  sc.appearance.customSecondaryR = s.themeSecondaryR;
  sc.appearance.customSecondaryG = s.themeSecondaryG;
  sc.appearance.customSecondaryB = s.themeSecondaryB;
  sc.appearance.customOnSecondaryR = s.themeOnSecondaryR;
  sc.appearance.customOnSecondaryG = s.themeOnSecondaryG;
  sc.appearance.customOnSecondaryB = s.themeOnSecondaryB;
  sc.appearance.customTertiaryR = s.themeTertiaryR;
  sc.appearance.customTertiaryG = s.themeTertiaryG;
  sc.appearance.customTertiaryB = s.themeTertiaryB;
  sc.appearance.customOnTertiaryR = s.themeOnTertiaryR;
  sc.appearance.customOnTertiaryG = s.themeOnTertiaryG;
  sc.appearance.customOnTertiaryB = s.themeOnTertiaryB;
  sc.appearance.customErrorR = s.themeErrorR;
  sc.appearance.customErrorG = s.themeErrorG;
  sc.appearance.customErrorB = s.themeErrorB;
  sc.appearance.customOnErrorR = s.themeOnErrorR;
  sc.appearance.customOnErrorG = s.themeOnErrorG;
  sc.appearance.customOnErrorB = s.themeOnErrorB;
  sc.appearance.customSurfaceR = s.themeSurfaceR;
  sc.appearance.customSurfaceG = s.themeSurfaceG;
  sc.appearance.customSurfaceB = s.themeSurfaceB;
  sc.appearance.customOnSurfaceR = s.themeOnSurfaceR;
  sc.appearance.customOnSurfaceG = s.themeOnSurfaceG;
  sc.appearance.customOnSurfaceB = s.themeOnSurfaceB;
  sc.appearance.customSurfaceVariantR = s.themeSurfaceVariantR;
  sc.appearance.customSurfaceVariantG = s.themeSurfaceVariantG;
  sc.appearance.customSurfaceVariantB = s.themeSurfaceVariantB;
  sc.appearance.customOnSurfaceVariantR = s.themeOnSurfaceVariantR;
  sc.appearance.customOnSurfaceVariantG = s.themeOnSurfaceVariantG;
  sc.appearance.customOnSurfaceVariantB = s.themeOnSurfaceVariantB;
  sc.appearance.customOutlineR = s.themeOutlineR;
  sc.appearance.customOutlineG = s.themeOutlineG;
  sc.appearance.customOutlineB = s.themeOutlineB;
  sc.appearance.customShadowR = s.themeShadowR;
  sc.appearance.customShadowG = s.themeShadowG;
  sc.appearance.customShadowB = s.themeShadowB;
  sc.appearance.customHoverR = s.themeHoverR;
  sc.appearance.customHoverG = s.themeHoverG;
  sc.appearance.customHoverB = s.themeHoverB;
  sc.appearance.customOnHoverR = s.themeOnHoverR;
  sc.appearance.customOnHoverG = s.themeOnHoverG;
  sc.appearance.customOnHoverB = s.themeOnHoverB;
  sc.appearance.overlayOpacityAdvanced = s.overlayOpacityAdvanced;
  sc.appearance.overlayOpacityMaster = s.overlayOpacityMasterPct / 100.f;
  sc.appearance.overlayOpacityControlCenter = s.overlayOpacityCcPct / 100.f;
  sc.appearance.overlayOpacityControlCenterInner = s.overlayOpacityCcInnerPct / 100.f;
  sc.appearance.overlayOpacityAppDrawer = s.overlayOpacityAppDrawerPct / 100.f;
  sc.appearance.overlayOpacityLaunchpad = s.overlayOpacityLaunchpadPct / 100.f;
  sc.appearance.overlayOpacitySettings = s.overlayOpacitySettingsPct / 100.f;
  sc.appearance.overlayOpacitySettingsSidebar = s.overlayOpacitySettingsSidebarPct / 100.f;
  sc.appearance.overlayOpacityDockMenu = s.overlayOpacityDockMenuPct / 100.f;
  sc.appearance.overlayOpacityDesktopMenu = s.overlayOpacityDesktopMenuPct / 100.f;
  sc.appearance.overlayOpacityTrayMenu = s.overlayOpacityTrayMenuPct / 100.f;
  sc.appearance.overlayOpacityCalendar = s.overlayOpacityCalendarPct / 100.f;
  sc.appearance.overlayOpacityWeather = s.overlayOpacityWeatherPct / 100.f;
  sc.appearance.overlayOpacityTooltip = s.overlayOpacityTooltipPct / 100.f;
  sc.appearance.overlayOpacityWidgetCard = s.overlayOpacityWidgetCardPct / 100.f;
  sc.appearance.overlayOpacityNotifications = s.overlayOpacityNotificationsPct / 100.f;
  sc.appearance.launchpadGridColumns = std::clamp(s.launchpadGridColumns, 4, 12);
  sc.appearance.launchpadGridRows = std::clamp(s.launchpadGridRows, 3, 10);
  sc.appearance.launchpadCellGapPx = std::clamp(s.launchpadCellGapPx, 0, 24);
  sc.appearance.launchpadIconFillPct = std::clamp(s.launchpadIconFillPct, 30, 95);
  sc.appearance.launchpadLayoutScalePct = std::clamp(s.launchpadLayoutScalePct, 70, 150);
  sc.appearance.launchpadDpiScalePct = std::clamp(s.launchpadDpiScalePct, 50, 300);
  sc.appearance.launchpadViewMode = std::clamp(s.launchpadViewMode, 0, 1);
  sc.appearance.launchpadFolderSizePct = std::clamp(s.launchpadFolderSizePct, 50, 200);
  sc.appearance.launchpadFolderGapPx = std::clamp(s.launchpadFolderGapPx, 4, 48);
  sc.appearance.overviewAxis = std::clamp(s.overviewAxis, 0, 1);
  sc.appearance.overviewCaptureMode = std::clamp(s.overviewCaptureMode, 0, 1);
  sc.appearance.overviewLiveUpdates = s.overviewLiveUpdates;
  sc.appearance.overviewMultiMonitor = s.overviewMultiMonitor;
  sc.appearance.overviewCardScalePct = std::clamp(s.overviewCardScalePct, 20, 80);
  sc.appearance.overviewCardGapPx = std::clamp(s.overviewCardGapPx, 8, 80);
  sc.appearance.overviewScrollDelayMs = std::clamp(s.overviewScrollDelayMs, 50, 500);
  sc.appearance.overviewCloseBtnSizePx = std::clamp(s.overviewCloseBtnSizePx, 20, 60);
  sc.appearance.overviewSearchWidthPx = std::clamp(s.overviewSearchWidthPx, 200, 800);
  sc.desktopEnabled = s.desktopEnabled;
  sc.defaultApps = s.defaultApps;
  sc.time.use24h = s.timeUse24h;
  sc.time.showSeconds = s.timeShowSeconds;
  sc.time.showDate = s.timeShowDate;
  sc.time.dateFormat = s.timeDateFormat;
  sc.time.customFormat = s.timeCustomFormat;
  sc.time.timezone = s.timeTimezone;
  sc.power.displaySleep = s.powerDisplaySleep;
  sc.power.displaySleepTimeoutMin = std::clamp(s.powerDisplaySleepTimeout, 1, 120);
  sc.power.idleSuspend = s.powerIdleSuspend;
  sc.power.idleSuspendTimeoutMin = std::clamp(s.powerIdleSuspendTimeout, 5, 240);
  sc.power.powerButtonAction = std::clamp(s.powerPowerButtonAction, 0, 3);
  sc.power.lidCloseAction = std::clamp(s.powerLidCloseAction, 0, 2);
  sc.power.showBatteryPercentage = s.powerShowBatteryPercentage;
  sc.power.tunedProfile = s.powerTunedProfile;
  sc.power.epp = s.powerEpp;
  sc.nightLight.enabled = s.nightlightEnabled;
  sc.nightLight.dayTemperature = std::clamp(s.nightlightDayTemp, 4000, 6500);
  sc.nightLight.nightTemperature = std::clamp(s.nightlightNightTemp, 2500, 4000);
  sc.nightLight.scheduleMode = std::clamp(s.nightlightSchedule, 0, 2);
  sc.nightLight.scheduleStartMin = std::clamp(s.nightlightScheduleStart, 0, 1439);
  sc.nightLight.scheduleEndMin = std::clamp(s.nightlightScheduleEnd, 0, 1439);
  sc.notifications.dbusEnabled = s.notificationsDbusEnabled;
  sc.notifications.doNotDisturb = s.notificationsDoNotDisturb;
  sc.notifications.defaultTimeoutMs =
      static_cast<std::int32_t>(std::clamp(s.notificationsDefaultTimeoutMs, 1000, 600000));
  sc.notifications.toast.layerShellEnabled = s.notificationsToastLayerShellEnabled;
  sc.notifications.toast.position = s.notificationsToastPosition;
  sc.notifications.toast.marginPx = std::clamp(s.notificationsToastMarginPx, 8, 64);
  sc.notifications.toast.cornerRadiusPx = std::clamp(s.notificationsToastCornerRadiusPx, 0, 40);
  sc.notifications.toast.maxWidthPx = std::clamp(s.notificationsToastMaxWidthPx, 200, 900);
  sc.notifications.toast.scalePct = std::clamp(s.notificationsScalePct, 50, 200);
  sc.keyboard.layout = s.keyboardLayout;
  sc.keyboard.layouts = s.keyboardLayouts;
  sc.keyboard.switchShortcut = s.keyboardSwitchShortcut;
  sc.keyboard.showLayout = s.keyboardShowLayout;
  sc.keyboard.numlock = s.keyboardNumlock;
  sc.keyboard.inputMethodEnabled = s.keyboardInputMethodEnabled;
  sc.keyboard.capsLockBehavior = s.keyboardCapsLockBehavior;
  sc.keyboard.composeKey = s.keyboardComposeKey;
  sc.keyboard.middleClickPaste = s.keyboardMiddleClickPaste;
  sc.keyboard.repeatRate = s.keyboardRepeatRate;
  sc.keyboard.repeatDelay = s.keyboardRepeatDelay;
  sc.audio.default_sink_name = s.audioDefaultSinkName;
  sc.audio.default_source_name = s.audioDefaultSourceName;
  sc.audio.default_sink_volume_pct = s.audioDefaultSinkVolumePct;
  sc.audio.default_sink_muted = s.audioDefaultSinkMuted;
  sc.audio.default_source_volume_pct = s.audioDefaultSourceVolumePct;
  sc.audio.default_source_muted = s.audioDefaultSourceMuted;
  sc.audio.engine_clock_rate_hz = s.audioEngineClockRateHz;
  sc.audio.engine_force_rate_hz = s.audioEngineForceRateHz;
  sc.audio.engine_allowed_rates_hz = s.audioEngineAllowedRatesHz;
  sc.audio.compat_pcm_format = s.audioCompatPcmFormat;
  return sc;
}

int settings_dock_preview_auto_bar_px(const Settings& s) {
   
  eh::config::ShellConfig sc = settings_to_shell_config(s);
  sc.dock.dockBarFollowsIcons = true;
  return dock_effective_bar_height_px(sc.dock);
}

void wallpaper_apply_if_digest_changed(const Settings& s) {
  WP_SCOPE();
  WP_LOG("enabled=%d mode=%d path=%s", s.wallpaperEnabled ? 1 : 0, s.wallpaperMode, s.wallpaperImage.c_str()); 
  static std::string last{};
  const std::string cur =
      std::to_string(static_cast<int>(s.wallpaperEnabled ? 1 : 0)) + "|" + std::to_string(s.wallpaperMode) + "|" +
      s.wallpaperImage;
  if (cur == last) return;
  last = cur;
  // The split-out `horizon-wallpaper` child owns the native renderer now. When it
  // is alive, the config write that follows save_settings triggers the supervisor's
  // inotify → `config.applied` broadcast, and the child re-applies. Skip the local
  // apply_saved so we don't double-apply via the external wallpaper fallback.
  if (eh::wallpaper::wallpaper_native_child_active()) {
    WP_LOG("native wallpaper child active; deferring to config.applied broadcast");
    eh::config::shell_config_invalidate();
    return;
  }
  eh::wallpaper::apply_saved(s.wallpaperEnabled, s.wallpaperImage, s.wallpaperMode);
}

void save_settings(const Settings& s) {
    
  debug_log("settings", "save_settings: BEGIN");
  const std::chrono::steady_clock::time_point t_sv_apply_start = std::chrono::steady_clock::now();
  eh::config::ShellConfig sc;
  const auto t_p2_0 = std::chrono::steady_clock::now();
  {
    debug_log("settings", "save_settings: calling settings_to_shell_config");
    sc = settings_to_shell_config(s);
    debug_log("settings", "save_settings: settings_to_shell_config returned");
  }
  const auto t_p2_1 = std::chrono::steady_clock::now();
  {
    eh::config::merge_widget_overrides_from_state_file(sc);
  }
  const auto t_p2_2 = std::chrono::steady_clock::now();
  {
    patch_workspaces_widget_in_shell_config(sc, s);
    patch_widget_slot_enabled_into_shell_config(sc, s);
  }
  const auto t_p2_3 = std::chrono::steady_clock::now();
  wallpaper_apply_if_digest_changed(s);
  eh::settings::default_apps::apply_from_config(s.defaultApps);

  // Persist to disk BEFORE shell_config_apply_from_memory: the applied hook in
  // the standalone settings app fires synchronously there and reloads
  // app.settings via load_settings(), which re-reads desktop_widgets.toml. If
  // that toml were written after the in-memory apply, the reload would clobber
  // the just-toggled desktopWidgetSlotsDisabled set and the desktop-widget
  // toggle would snap straight back to the ON position.
  {
    debug_log("settings", "save_settings: calling write_state_settings_toml");
    if (!eh::config::write_state_settings_toml(sc)) {
      debug_log("settings", "save_settings: write_state_settings_toml FAILED");
      std::cerr << "[settings-save] component config write failed\n";
    } else {
      debug_log("settings", "save_settings: write_state_settings_toml OK");
      std::cout << "[settings-save] toml ok";
    }
    debug_log("settings", "save_settings: calling save_launchpad_settings_to_file");
    save_launchpad_settings_to_file(sc);
    debug_log("settings", "save_settings: calling save_desktop_widgets_to_file");
    save_desktop_widgets_to_file(s);
    debug_log("settings", "save_settings: disk writes done");
  }

  const auto t_p2_4 = std::chrono::steady_clock::now();
  debug_log("settings", "save_settings: calling shell_config_apply_from_memory");
  eh::config::shell_config_apply_from_memory(sc);
  debug_log("settings", "save_settings: shell_config_apply_from_memory returned");
  const auto t_p2_5 = std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point t_sv_apply_end = std::chrono::steady_clock::now();

  std::cout << "[settings-save] memory applied r=" << s.dockRadius << " icon=" << s.dockIconSize
            << " gap=" << s.dockIconSpacing << " bottom_gap=" << s.dockBottomGap
            << " border=" << (s.dockBorderEnabled ? 1 : 0) << " border_px=" << s.dockBorderSize
            << " ah=" << (s.dockAutoHide ? 1 : 0);
  if (eh::settings::trace::debug()) {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    std::cout << " XDG_STATE_HOME=\"" << (xdg ? xdg : "") << "\"";
    std::cout << " wallpaper_en=" << (s.wallpaperEnabled ? 1 : 0) << " wp_mode=" << s.wallpaperMode << " wp_image_len="
              << s.wallpaperImage.size();
    std::cout << " matugen=" << (s.matugenThemingEnabled ? 1 : 0) << " matugen_scheme=\"" << s.matugenScheme
              << "\" matugen_mode=\"" << s.matugenMode << "\"";
  }
  std::cout << "\n";
  const int64_t us_p2_1 = std::chrono::duration_cast<std::chrono::microseconds>(t_p2_1 - t_p2_0).count();
  const int64_t us_p2_2 = std::chrono::duration_cast<std::chrono::microseconds>(t_p2_2 - t_p2_1).count();
  const int64_t us_p2_3 = std::chrono::duration_cast<std::chrono::microseconds>(t_p2_3 - t_p2_2).count();
  const int64_t us_p2_4 = std::chrono::duration_cast<std::chrono::microseconds>(t_p2_4 - t_p2_3).count();
  const int64_t us_p2_5 = std::chrono::duration_cast<std::chrono::microseconds>(t_p2_5 - t_p2_4).count();
  const int64_t us_apply = std::chrono::duration_cast<std::chrono::microseconds>(t_sv_apply_end - t_sv_apply_start).count();
  std::cerr << "[settings-save] apply=" << us_apply << "us"
            << " sc=" << us_p2_1 << "us"
            << " ovr=" << us_p2_2 << "us"
            << " pat=" << us_p2_3 << "us"
            << " wal=" << us_p2_4 << "us"
            << " mem=" << us_p2_5 << "us"
            << " r=" << s.dockRadius << " icon=" << s.dockIconSize << " gap=" << s.dockIconSpacing
            << " bg=" << s.dockBottomGap << " border=" << (s.dockBorderEnabled ? 1 : 0)
             << " bp=" << s.dockBorderSize << " bh=" << s.dockBorderHue << " bo=" << s.dockBorderOpacity << " ah=" << (s.dockAutoHide ? 1 : 0) << "\n";
  debug_log("settings", "save_settings: END");
}

