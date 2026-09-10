#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include "desktop_shell/common/fs/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eh::theming {

using eh::theming::detail::read_ini_kv;
using eh::theming::detail::update_ini_key;

namespace {

static void scan_qt_plugin_dir(const std::string& dir, std::unordered_set<std::string>& seen, std::vector<std::string>& out) {
   
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
    const std::string name(ent->d_name);
    if (name.size() < 6) continue;
    // style plugin files: lib<name>.so or <name>.so
    if (name.compare(name.size() - 3, 3, ".so") != 0) continue;
    std::string styleName;
    if (name.rfind("lib", 0) == 0)
      styleName = name.substr(3, name.size() - 6);
    else
      styleName = name.substr(0, name.size() - 3);
    if (styleName.empty() || styleName == "qt6ct-style" || styleName == "qt5ct-style") continue;
    // title-case: first letter uppercase
    if (!styleName.empty()) {
      styleName[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(styleName[0])));
    }
    if (seen.insert(styleName).second) out.push_back(styleName);
  }
  closedir(d);
}

static std::vector<std::string> scan_color_scheme_dir(const std::string& dir) {
   
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
    const std::string name(ent->d_name);
    if (name.empty() || name == "." || name == "..") continue;
    // must end with .conf
    if (name.size() < 5 || name.compare(name.size() - 5, 5, ".conf") != 0) continue;
    out.push_back(name.substr(0, name.size() - 5));
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

static GtkThemeColors parse_color_scheme_file(const std::string& path) {
   
  GtkThemeColors out;
  const std::string winBg = read_ini_kv(path, "[Colors:Window]", "BackgroundNormal");
  const std::string winFg = read_ini_kv(path, "[Colors:Window]", "ForegroundNormal");
  const std::string selBg = read_ini_kv(path, "[Colors:Selection]", "BackgroundNormal");
  const std::string viewBg = read_ini_kv(path, "[Colors:View]", "BackgroundNormal");
  const std::string viewFg = read_ini_kv(path, "[Colors:View]", "ForegroundNormal");
  const std::string btnBg = read_ini_kv(path, "[Colors:Button]", "BackgroundNormal");

  auto parse_rgb = [](const std::string& str) -> std::tuple<double, double, double> {
    double r = 0, g = 0, b = 0;
    if (std::sscanf(str.c_str(), "%lf,%lf,%lf", &r, &g, &b) == 3) {
      r = std::clamp(r / 255.0, 0.0, 1.0);
      g = std::clamp(g / 255.0, 0.0, 1.0);
      b = std::clamp(b / 255.0, 0.0, 1.0);
    }
    return {r, g, b};
  };

  bool any = false;
  if (!winBg.empty()) { auto [r,g,b] = parse_rgb(winBg); out.bgR = r; out.bgG = g; out.bgB = b; any = true; }
  if (!winFg.empty()) { auto [r,g,b] = parse_rgb(winFg); out.fgR = r; out.fgG = g; out.fgB = b; any = true; }
  if (!selBg.empty()) { auto [r,g,b] = parse_rgb(selBg); out.accentR = r; out.accentG = g; out.accentB = b; any = true; }
  if (!viewBg.empty()) { auto [r,g,b] = parse_rgb(viewBg); out.baseR = r; out.baseG = g; out.baseB = b; out.textR = r; out.textG = g; out.textB = b; any = true; }
  if (!viewFg.empty()) { auto [r,g,b] = parse_rgb(viewFg); out.textR = r; out.textG = g; out.textB = b; any = true; }
  if (!btnBg.empty()) { auto [r,g,b] = parse_rgb(btnBg); out.headerBgR = r; out.headerBgG = g; out.headerBgB = b; any = true; }
  if (!winFg.empty()) { auto [r,g,b] = parse_rgb(winFg); out.headerFgR = r; out.headerFgG = g; out.headerFgB = b; }
  out.valid = any;
  return out;
}

} // namespace

std::string detect_system_qt_style() {
   
  const char* home = std::getenv("HOME");
  if (!home) return {};
  for (const char* rel : {"/.config/qt6ct/qt6ct.conf", "/.config/qt5ct/qt5ct.conf"}) {
    const std::string v = read_ini_kv(std::string(home) + rel, "[Appearance]", "style");
    if (!v.empty()) return v;
  }
  return {};
}

std::vector<std::string> known_qt_styles() {
   
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;

  // Built-in Qt styles (compiled into QtGui, no plugin file)
  static const char* kBuiltInQtStyles[] = {"Fusion", "Windows"};
  for (const char* s : kBuiltInQtStyles) {
    if (seen.insert(s).second) out.push_back(s);
  }

  // Scan common Qt plugin directories for style plugins
  const char* home = std::getenv("HOME");
  const char* dataDirs = std::getenv("XDG_DATA_DIRS");
  std::vector<std::string> searchDirs;

  // User-local Qt plugin paths
  if (home) {
    searchDirs.push_back(std::string(home) + "/.local/share/qt6/styles");
    searchDirs.push_back(std::string(home) + "/.local/share/qt5/styles");
    // Flatpak
    searchDirs.push_back(std::string(home) + "/.local/share/flatpak/exports/share/qt6/styles");
    searchDirs.push_back(std::string(home) + "/.local/share/flatpak/exports/share/qt5/styles");
  }

  // XDG data dirs
  if (dataDirs) {
    std::string dd(dataDirs);
    size_t start = 0;
    while (start < dd.size()) {
      size_t colon = dd.find(':', start);
      std::string dir = dd.substr(start, colon == std::string::npos ? dd.size() - start : colon - start);
      searchDirs.push_back(dir + "/qt6/styles");
      searchDirs.push_back(dir + "/qt5/styles");
      start = (colon == std::string::npos) ? dd.size() : colon + 1;
    }
  }

  // Common fixed paths
  static const char* kCommonQtStyleDirs[] = {
    "/usr/lib/qt6/plugins/styles",
    "/usr/lib/x86_64-linux-gnu/qt6/plugins/styles",
    "/usr/lib64/qt6/plugins/styles",
    "/usr/lib/qt5/plugins/styles",
    "/usr/lib/x86_64-linux-gnu/qt5/plugins/styles",
    "/usr/lib64/qt5/plugins/styles",
    "/usr/local/lib/qt6/plugins/styles",
    "/usr/local/lib/qt5/plugins/styles",
  };
  for (const char* p : kCommonQtStyleDirs) searchDirs.push_back(p);

  for (const auto& dir : searchDirs) {
    scan_qt_plugin_dir(dir, seen, out);
  }

  std::sort(out.begin(), out.end());
  return out;
}

bool apply_qt_style(const std::string& style, bool isQt6) {
   
  if (style.empty()) return false;
  const char* home = std::getenv("HOME");
  if (!home) return false;

  // Write to the Qt platform-theme config files
  {
    const std::string rel = isQt6 ? "/.config/qt6ct/qt6ct.conf" : "/.config/qt5ct/qt5ct.conf";
    const std::string path = std::string(home) + rel;
    update_ini_key(path, "[Appearance]", "style", style);
  }

  // Write to the shared session globals (for platform-theme / integration users)
  {
    const std::string kgPath = std::string(home) + "/.config/kdeglobals";
    update_ini_key(kgPath, "[KDE]", "widgetStyle", style);
  }

  return true;
}

std::vector<std::string> list_qt_color_schemes() {
   
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  const char* home = std::getenv("HOME");
  if (!home) return out;
  for (const char* rel : {"/.config/qt6ct/colors", "/.config/qt5ct/colors",
                          "/.local/share/qt6ct/colors", "/.local/share/qt5ct/colors",
                          "/usr/share/qt6ct/colors", "/usr/share/qt5ct/colors"}) {
    for (const auto& s : scan_color_scheme_dir(std::string(home) + rel)) {
      if (seen.insert(s).second) out.push_back(s);
    }
  }
  // Also scan system dirs
  for (const char* rel : {"/usr/share/qt6ct/colors", "/usr/share/qt5ct/colors",
                          "/usr/local/share/qt6ct/colors", "/usr/local/share/qt5ct/colors"}) {
    for (const auto& s : scan_color_scheme_dir(rel)) {
      if (seen.insert(s).second) out.push_back(s);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::string detect_current_qt_color_scheme() {
   
  const char* home = std::getenv("HOME");
  if (!home) return {};
  for (const char* rel : {"/.config/qt6ct/qt6ct.conf", "/.config/qt5ct/qt5ct.conf"}) {
    const std::string v = read_ini_kv(std::string(home) + rel, "[Appearance]", "color_scheme_path");
    if (!v.empty()) {
      // extract just the filename without extension
      const size_t sl = v.rfind('/');
      const std::string fn = (sl != std::string::npos) ? v.substr(sl + 1) : v;
      if (fn.size() >= 5 && fn.compare(fn.size() - 5, 5, ".conf") == 0)
        return fn.substr(0, fn.size() - 5);
      return fn;
    }
  }
  return {};
}

bool apply_qt_color_scheme(const std::string& schemeName, bool isQt6) {
   
  const char* home = std::getenv("HOME");
  if (!home) return false;
  const std::string rel = isQt6 ? "/.config/qt6ct/qt6ct.conf" : "/.config/qt5ct/qt5ct.conf";
  const std::string path = std::string(home) + rel;
  std::string style = detect_system_qt_style();
  if (style.empty()) style = "Fusion";
  update_ini_key(path, "[Appearance]", "style", style);
  if (schemeName.empty() || schemeName == "None") {
    // Remove color_scheme_path by setting it empty
    update_ini_key(path, "[Appearance]", "color_scheme_path", "");
    // Remove the empty line
    // (We'll just leave it; update_ini_key wrote an empty value, the platform theme will ignore it)
  } else {
    const std::string colorsDir = isQt6 ? "/.config/qt6ct/colors" : "/.config/qt5ct/colors";
    const std::string schemePath = std::string(home) + colorsDir + "/" + schemeName + ".conf";
    update_ini_key(path, "[Appearance]", "color_scheme_path", schemePath);
  }
  return true;
}

GtkThemeColors qt_style_preview_colors(const std::string& styleName) {
   
  std::string s = styleName;
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }

  // Each style gets distinctive colors for preview
  struct QtStylePalette { double bgR, bgG, bgB; double fgR, fgG, fgB; double accentR, accentG, accentB; double baseR, baseG, baseB; };
  static const std::unordered_map<std::string, QtStylePalette> kPalettes = {
    {"fusion",        {0.88, 0.88, 0.88, 0.12, 0.12, 0.12, 0.55, 0.55, 0.55, 1.0, 1.0, 1.0}},
    {"breeze",        {0.85, 0.88, 0.92, 0.10, 0.12, 0.15, 0.25, 0.55, 0.88, 0.98, 0.98, 1.0}},
    {"kvantum",       {0.82, 0.82, 0.86, 0.15, 0.15, 0.18, 0.60, 0.50, 0.70, 0.95, 0.95, 0.98}},
    {"windows",       {0.92, 0.92, 0.94, 0.10, 0.10, 0.10, 0.45, 0.65, 0.82, 1.0, 1.0, 1.0}},
    {"windowsxp",     {0.88, 0.92, 0.85, 0.10, 0.15, 0.08, 0.30, 0.70, 0.35, 0.98, 1.0, 0.95}},
    {"windowsvista",  {0.90, 0.90, 0.94, 0.10, 0.10, 0.14, 0.30, 0.65, 0.80, 1.0, 1.0, 1.0}},
    {"fluent",        {0.88, 0.88, 0.90, 0.10, 0.10, 0.12, 0.35, 0.55, 0.85, 0.98, 0.98, 1.0}},
    {"material",      {0.90, 0.90, 0.92, 0.10, 0.10, 0.12, 0.40, 0.60, 0.85, 1.0, 1.0, 1.0}},
    {"gtk2",          {0.88, 0.88, 0.88, 0.12, 0.12, 0.12, 0.50, 0.60, 0.70, 0.98, 0.98, 0.98}},
    {"gtk3",          {0.86, 0.88, 0.90, 0.12, 0.14, 0.16, 0.40, 0.55, 0.75, 0.96, 0.97, 0.99}},
    {"qtcurve",       {0.85, 0.85, 0.88, 0.14, 0.14, 0.16, 0.55, 0.55, 0.55, 0.96, 0.96, 0.98}},
    {"oxygen",        {0.86, 0.88, 0.90, 0.12, 0.14, 0.16, 0.25, 0.55, 0.75, 0.96, 0.97, 0.99}},
    {"cleanlooks",    {0.88, 0.88, 0.88, 0.12, 0.12, 0.12, 0.50, 0.50, 0.50, 1.0, 1.0, 1.0}},
    {"plastique",     {0.89, 0.89, 0.91, 0.11, 0.11, 0.13, 0.50, 0.55, 0.60, 0.99, 0.99, 1.0}},
    {"chrome",        {0.89, 0.89, 0.91, 0.11, 0.11, 0.13, 0.45, 0.65, 0.75, 0.99, 0.99, 1.0}},
    {"redmond",       {0.92, 0.92, 0.94, 0.10, 0.10, 0.12, 0.45, 0.65, 0.80, 1.0, 1.0, 1.0}},
  };

  GtkThemeColors out;
  auto it = kPalettes.find(s);
  if (it != kPalettes.end()) {
    const auto& p = it->second;
    out.bgR = p.bgR; out.bgG = p.bgG; out.bgB = p.bgB;
    out.fgR = p.fgR; out.fgG = p.fgG; out.fgB = p.fgB;
    out.accentR = p.accentR; out.accentG = p.accentG; out.accentB = p.accentB;
    out.baseR = p.baseR; out.baseG = p.baseG; out.baseB = p.baseB;
    out.textR = p.fgR; out.textG = p.fgG; out.textB = p.fgB;
    out.headerBgR = p.bgR * 0.85; out.headerBgG = p.bgG * 0.85; out.headerBgB = p.bgB * 0.85;
    out.headerFgR = p.fgR; out.headerFgG = p.fgG; out.headerFgB = p.fgB;
    out.valid = true;
  }
  return out;
}

GtkThemeColors qt_color_scheme_preview_colors(const std::string& schemeName) {
   
  if (schemeName.empty()) return {};
  const char* home = std::getenv("HOME");
  if (!home) return {};
  static const char* kColorDirs[] = {
    "/.config/qt6ct/colors", "/.config/qt5ct/colors",
    "/.local/share/qt6ct/colors", "/.local/share/qt5ct/colors",
    "/usr/share/qt6ct/colors", "/usr/share/qt5ct/colors",
    "/usr/local/share/qt6ct/colors", "/usr/local/share/qt5ct/colors",
  };
  for (const char* rel : kColorDirs) {
    const std::string path = std::string(home) + rel + "/" + schemeName + ".conf";
    GtkThemeColors c = parse_color_scheme_file(path);
    if (c.valid) return c;
  }
  return {};
}

} // namespace eh::theming
