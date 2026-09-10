#define TOML_IMPLEMENTATION
#include "config/shell_config.hpp"
#include <toml++/toml.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace eh::config {

namespace fs = std::filesystem;

// ── path helpers ──────────────────────────────────────────────────────

static std::string config_dir() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/event-horizon";
  const char* home = std::getenv("HOME");
  return home ? std::string(home) + "/.config/event-horizon" : ".config/event-horizon";
}

static std::string file_browser_toml_path() {
  return config_dir() + "/file-browser.toml";
}

std::string state_settings_toml_path() {
  return config_dir() + "/state-settings.toml";
}

std::string legacy_ini_path() {
  return config_dir() + "/settings.ini";
}

// ── singleton config snapshot ────────────────────────────────────────

static ShellConfig g_snapshot;

const ShellConfig& shell_config_snapshot_skip_matugen() {
  return g_snapshot;
}

const ShellConfig& shell_config_snapshot() {
  return g_snapshot;
}

void shell_config_reload_from_disk_now() {
  // Read state-settings.toml into g_snapshot
  std::string path = state_settings_toml_path();
  std::ifstream f(path);
  if (!f.is_open()) return;

  try {
    toml::table tbl = toml::parse(f);

    // appearance
    if (auto* ap = tbl["appearance"].as_table()) {
      auto& a = g_snapshot.appearance;
      if (auto* v = ap->get("matugenThemingEnabled")) a.matugenThemingEnabled = v->value_or(false);
      if (auto* v = ap->get("matugenPaletteOk")) a.matugenPaletteOk = v->value_or(false);
      if (auto* v = ap->get("colorBrightness")) a.colorBrightness = v->value_or(1.0);
      if (auto* v = ap->get("colorContrast")) a.colorContrast = v->value_or(1.0);
      if (auto* v = ap->get("colorVibrance")) a.colorVibrance = v->value_or(1.0);
      if (auto* v = ap->get("colorGamma")) a.colorGamma = v->value_or(1.0);
    }

    // dock
    if (auto* dock = tbl["dock"].as_table()) {
      if (auto* v = dock->get("iconTheme")) g_snapshot.dock.iconTheme = v->value_or("");
    }

    // defaultApps
    if (auto* da = tbl["defaultApps"].as_table()) {
      if (auto* v = da->get("terminal")) g_snapshot.defaultApps.terminal = v->value_or("");
    }

    if (auto* v = tbl["wallpaperImage"].as_string()) g_snapshot.wallpaperImage = v->get();
  } catch (const std::exception& e) {
    std::cerr << "[horizon-files] TOML parse error: " << e.what() << "\n";
  }
}

void shell_config_apply_from_memory(ShellConfig sc) {
  g_snapshot = std::move(sc);
}

// ── chrome colors ────────────────────────────────────────────────────

ChromePaintColors derived_chrome_colors(const ShellAppearance& appearance) {
  ChromePaintColors mc;
  // From the palette data: default dark theme
  mc.accentR = 0.30; mc.accentG = 0.58; mc.accentB = 0.90;
  mc.textR = 0.94; mc.textG = 0.94; mc.textB = 0.96;
  mc.panelFillR = 0.08; mc.panelFillG = 0.08; mc.panelFillB = 0.10;
  mc.outlineR = 0.06; mc.outlineG = 0.06; mc.outlineB = 0.08;
  return mc;
}

// ── icon theme ───────────────────────────────────────────────────────

std::string read_dock_icon_theme_from_disk() {
  std::string path = state_settings_toml_path();
  std::ifstream f(path);
  if (!f.is_open()) return {};
  try {
    toml::table tbl = toml::parse(f);
    if (auto* dock = tbl["dock"].as_table()) {
      if (auto* v = dock->get("iconTheme")) return v->value_or("");
    }
  } catch (...) {}
  return {};
}

// ── File Browser settings ────────────────────────────────────────────

FileBrowserSettings read_file_browser_toml() {
  FileBrowserSettings fbs;
  std::string path = file_browser_toml_path();
  std::ifstream f(path);
  if (!f.is_open()) return fbs;

  try {
    toml::table tbl = toml::parse(f);
    fbs.zoom_pct = tbl["zoom_pct"].value_or(100.0);
    fbs.folders_before_files = tbl["folders_before_files"].value_or(true);
    fbs.surface_opacity_pct = tbl["surface_opacity_pct"].value_or(100);
    fbs.sidebar_opacity_pct = tbl["sidebar_opacity_pct"].value_or(100);
    fbs.topbar_opacity_pct = tbl["topbar_opacity_pct"].value_or(100);
    fbs.statusbar_opacity_pct = tbl["statusbar_opacity_pct"].value_or(100);
    fbs.preview_opacity_pct = tbl["preview_opacity_pct"].value_or(100);
    fbs.view_mode = tbl["view_mode"].value_or(0);
    fbs.sort_field = tbl["sort_field"].value_or(0);
    fbs.sort_descending = tbl["sort_descending"].value_or(false);
    fbs.show_hidden = tbl["show_hidden"].value_or(false);
    fbs.window_controls_left = tbl["window_controls_left"].value_or(false);
    fbs.group_by_type = tbl["group_by_type"].value_or(false);

    if (auto* fav = tbl["favorites"].as_array()) {
      for (auto& el : *fav) {
        if (auto s = el.value<std::string>()) fbs.favorites.push_back(*s);
      }
    }

    if (auto* pf = tbl["per_folder"].as_table()) {
      for (auto& [path, val] : *pf) {
        if (auto* t = val.as_table()) {
          FileBrowserSettings::PerFolder p;
          if (auto* v = t->get("view_mode")) p.view_mode = v->value_or(0);
          if (auto* v = t->get("sort_field")) p.sort_field = v->value_or(0);
          if (auto* v = t->get("sort_descending")) p.sort_descending = v->value_or(false);
          if (auto* v = t->get("group_by_type")) p.group_by_type = v->value_or(false);
          fbs.per_folder[std::string{path.str()}] = p;
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[horizon-files] TOML parse error (file-browser): " << e.what() << "\n";
  }

  return fbs;
}

bool write_file_browser_toml(const FileBrowserSettings& fbs) {
  try {
    toml::table tbl;
    tbl.emplace("zoom_pct", fbs.zoom_pct);
    tbl.emplace("folders_before_files", fbs.folders_before_files);
    tbl.emplace("surface_opacity_pct", fbs.surface_opacity_pct);
    tbl.emplace("sidebar_opacity_pct", fbs.sidebar_opacity_pct);
    tbl.emplace("topbar_opacity_pct", fbs.topbar_opacity_pct);
    tbl.emplace("statusbar_opacity_pct", fbs.statusbar_opacity_pct);
    tbl.emplace("preview_opacity_pct", fbs.preview_opacity_pct);
    tbl.emplace("view_mode", fbs.view_mode);
    tbl.emplace("sort_field", fbs.sort_field);
    tbl.emplace("sort_descending", fbs.sort_descending);
    tbl.emplace("show_hidden", fbs.show_hidden);
    tbl.emplace("window_controls_left", fbs.window_controls_left);
    tbl.emplace("group_by_type", fbs.group_by_type);

    toml::array favs;
    for (const auto& f : fbs.favorites) favs.push_back(f);
    tbl.emplace("favorites", std::move(favs));

    toml::table pf_tbl;
    for (const auto& [path, pf] : fbs.per_folder) {
      toml::table p;
      p.emplace("view_mode", pf.view_mode);
      p.emplace("sort_field", pf.sort_field);
      p.emplace("sort_descending", pf.sort_descending);
      p.emplace("group_by_type", pf.group_by_type);
      pf_tbl.emplace(path, std::move(p));
    }
    tbl.emplace("per_folder", std::move(pf_tbl));

    std::string path = file_browser_toml_path();
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << tbl << "\n";
    return true;
  } catch (...) { return false; }
}

bool write_state_settings_toml(const ShellConfig& c) {
  try {
    std::string path = state_settings_toml_path();
    fs::create_directories(fs::path(path).parent_path());

    toml::table tbl;

    toml::table ap;
    ap.emplace("matugenThemingEnabled", c.appearance.matugenThemingEnabled);
    ap.emplace("matugenPaletteOk", c.appearance.matugenPaletteOk);
    ap.emplace("colorBrightness", c.appearance.colorBrightness);
    ap.emplace("colorContrast", c.appearance.colorContrast);
    ap.emplace("colorVibrance", c.appearance.colorVibrance);
    ap.emplace("colorGamma", c.appearance.colorGamma);
    tbl.emplace("appearance", std::move(ap));

    toml::table dock;
    dock.emplace("iconTheme", c.dock.iconTheme);
    tbl.emplace("dock", std::move(dock));

    toml::table da;
    da.emplace("terminal", c.defaultApps.terminal);
    tbl.emplace("defaultApps", std::move(da));

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << tbl << "\n";
    return true;
  } catch (...) { return false; }
}

}
