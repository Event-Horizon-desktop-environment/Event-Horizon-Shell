#pragma once

#include <cairo/cairo.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eh::icons {

constexpr std::size_t kMaxIconMissLogged = 256;

struct ThemeInfo {
  std::string id;
  std::string name;
  std::string path;
  bool hidden = false;
};

struct GtkThemeInfo {
  std::string id;
  std::string name;
  std::string path;
  bool hidden = false;
};

std::string detect_system_icon_theme();


std::vector<ThemeInfo> list_installed_icon_themes();

cairo_surface_t* load_theme_preview_icon(const std::string& themeDir, const std::string& iconName, int targetPx);

std::string theme_example_icon_name(const std::string& themeDir);

std::vector<std::string> theme_find_any_icons(const std::string& themeDir, int maxCount);


std::vector<std::string> list_qt_color_schemes();
std::string detect_current_qt_color_scheme();
bool apply_qt_color_scheme(const std::string& schemeName, bool isQt6);

struct GtkThemeColors {
  double bgR = 1, bgG = 1, bgB = 1, bgA = 1;
  double fgR = 0, fgG = 0, fgB = 0, fgA = 1;
  double accentR = 0.2, accentG = 0.5, accentB = 0.9, accentA = 1;
  double baseR = 1, baseG = 1, baseB = 1, baseA = 1;
  double textR = 0, textG = 0, textB = 0, textA = 1;
  double headerBgR = 0.9, headerBgG = 0.9, headerBgB = 0.9, headerBgA = 1;
  double headerFgR = 0, headerFgG = 0, headerFgB = 0, headerFgA = 1;
  bool valid = false;
};

GtkThemeColors extract_gtk_theme_colors(const std::string& themeDir);
void clear_gtk_theme_colors_cache();

GtkThemeColors qt_style_preview_colors(const std::string& styleName);
GtkThemeColors qt_color_scheme_preview_colors(const std::string& schemeName);

std::string detect_system_gtk_theme();
std::vector<GtkThemeInfo> list_installed_gtk_themes();
bool apply_gtk_theme(const std::string& themeId);

std::string detect_system_qt_style();
std::vector<std::string> known_qt_styles();
bool apply_qt_style(const std::string& style, bool isQt6);

struct PlasmaThemeInfo {
  std::string id;
  std::string name;
  std::string path;
};

std::vector<PlasmaThemeInfo> list_installed_plasma_themes();
GtkThemeColors extract_plasma_theme_colors(const std::string& themeDir);
bool apply_plasma_theme(const std::string& themeId);

struct CursorThemeInfo {
  std::string id;
  std::string name;
  std::string path;
};

std::vector<CursorThemeInfo> list_installed_cursor_themes();
cairo_surface_t* load_cursor_shape_surface(const std::string& cursorThemePath, const std::string& cursorName, int targetPx);

struct GtkThemeDesign {
  double windowRad = 6, buttonRad = 3, entryRad = 2;
  int titlebarH = 16;
  int borderW = 1;
  bool valid = false;
};

GtkThemeDesign extract_gtk_theme_design(const std::string& themeDir);

struct IconEntry {
  cairo_surface_t* surface = nullptr;
  int width = 0;
  int height = 0;
  std::size_t bytes = 0;

  std::list<std::string>::iterator lru_it{};
};

struct IconCacheData {
  std::unordered_map<std::string, IconEntry> cache{};
  std::size_t totalBytes = 0;
  std::string keyBuf;
  std::list<std::string> lru{};
  std::unordered_set<std::string> missLogged{};
  std::unordered_set<std::string> execBasenameMiss{};
  std::string themeOverride;
  std::string resolvedThemeId;
  std::vector<std::string> searchDirs{};
  bool searchDirsBuilt = false;
  std::uint64_t generation = 0;
};

class IconCache {
public:
  IconCache();
  IconCache(const IconCache&) = delete;
  IconCache& operator=(const IconCache&) = delete;
  IconCache(IconCache&&) = delete;
  IconCache& operator=(IconCache&&) = delete;
  ~IconCache();

  static cairo_surface_t* load_settings_logo_surface();

  const IconEntry* app_icon(const std::string& appId);

  const IconEntry* tray_icon(const std::string& iconName);

  const IconEntry* app_icon_from_exec_basename(const std::string& execBasename);

  void set_icon_theme(std::string themeId);
  const std::string& icon_theme_override() const { return d_->themeOverride; }

  bool refresh_auto_theme_if_needed();
  void prewarm_search_dirs();

private:
  std::shared_ptr<IconCacheData> d_;
  const IconEntry* resolve_and_cache(const std::string& key, const std::string& iconName, bool isTray);
  void clear();
  void rebuild_search_dirs_if_needed();
  void touch_lru_key(const std::string& key);
  void evict_excess_icon_cache_entries();
  void ensureFresh();
};

}
