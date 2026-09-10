#pragma once

#include <cairo/cairo.h>

#include <string>
#include <vector>

namespace eh::theming {

struct GtkThemeInfo {
  std::string id;
  std::string name;
  std::string path;
  bool hidden = false;
};

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

struct GtkThemeDesign {
  double windowRad = 6, buttonRad = 3, entryRad = 2;
  int titlebarH = 16;
  int borderW = 1;
  bool valid = false;
};

struct PlasmaThemeInfo {
  std::string id;
  std::string name;
  std::string path;
};

struct CursorThemeInfo {
  std::string id;
  std::string name;
  std::string path;
};

// Platform theming.

std::vector<GtkThemeInfo> list_installed_gtk_themes();
std::string detect_system_gtk_theme();
bool apply_gtk_theme(const std::string& themeId);
GtkThemeColors extract_gtk_theme_colors(const std::string& themeDir);
void clear_gtk_theme_colors_cache();
GtkThemeDesign extract_gtk_theme_design(const std::string& themeDir);
void clear_gtk_theme_design_cache();

// Qt.

std::string detect_system_qt_style();
std::vector<std::string> known_qt_styles();
bool apply_qt_style(const std::string& style, bool isQt6);
std::vector<std::string> list_qt_color_schemes();
std::string detect_current_qt_color_scheme();
bool apply_qt_color_scheme(const std::string& schemeName, bool isQt6);
GtkThemeColors qt_style_preview_colors(const std::string& styleName);
GtkThemeColors qt_color_scheme_preview_colors(const std::string& schemeName);

// Desktop-environment.

std::vector<PlasmaThemeInfo> list_installed_plasma_themes();
GtkThemeColors extract_plasma_theme_colors(const std::string& themeDir);
bool apply_plasma_theme(const std::string& themeId);

// Cursor.

std::vector<CursorThemeInfo> list_installed_cursor_themes();
cairo_surface_t* load_cursor_shape_surface(const std::string& cursorThemePath,
                                           const std::string& cursorName, int targetPx);

} // namespace eh::theming
