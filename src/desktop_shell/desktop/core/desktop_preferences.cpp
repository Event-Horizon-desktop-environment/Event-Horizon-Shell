#include "desktop_shell/desktop/core/desktop_preferences.hpp"

#include "desktop_shell/desktop/core/desktop_app.hpp"

#include "configuration/shell_config.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace eh::shell::desktop {

namespace {

constexpr const char* kFileName = "desktop_icons_layout.txt";

std::string layout_file_path() {
   
  const std::string dir = eh::config::declarative_config_dir();
  if (dir.empty()) return {};
  return dir + "/" + kFileName;
}

std::string arrangement_token(DesktopIconArrangement a) {
   
  switch (a) {
    case DesktopIconArrangement::AutoArrange: return "auto";
    case DesktopIconArrangement::AlignToGrid: return "align";
    case DesktopIconArrangement::Stacked: return "stacked";
    case DesktopIconArrangement::StackedRight: return "stacked-right";
  }
  return "align";
}

bool parse_arrangement(std::string_view s, DesktopIconArrangement* out) {
   
  if (s == "auto") {
    *out = DesktopIconArrangement::AutoArrange;
    return true;
  }
  if (s == "align") {
    *out = DesktopIconArrangement::AlignToGrid;
    return true;
  }
  if (s == "stacked") {
    *out = DesktopIconArrangement::Stacked;
    return true;
  }
  if (s == "stacked-right") {
    *out = DesktopIconArrangement::StackedRight;
    return true;
  }
  return false;
}

std::string size_token(DesktopIconSizeKind k) {
   
  switch (k) {
    case DesktopIconSizeKind::Small: return "small";
    case DesktopIconSizeKind::Medium: return "medium";
    case DesktopIconSizeKind::Large: return "large";
  }
  return "medium";
}

bool parse_size(std::string_view s, DesktopIconSizeKind* out) {
   
  if (s == "small") {
    *out = DesktopIconSizeKind::Small;
    return true;
  }
  if (s == "large") {
    *out = DesktopIconSizeKind::Large;
    return true;
  }
  if (s == "medium") {
    *out = DesktopIconSizeKind::Medium;
    return true;
  }
  return false;
}

std::string sort_token(DesktopIconSort s) {
   
  switch (s) {
    case DesktopIconSort::Name: return "name";
    case DesktopIconSort::Type: return "type";
  }
  return "name";
}

bool parse_sort(std::string_view s, DesktopIconSort* out) {
   
  if (s == "type") {
    *out = DesktopIconSort::Type;
    return true;
  }
  if (s == "name") {
    *out = DesktopIconSort::Name;
    return true;
  }
  return false;
}

}

IconLayoutMetrics desktop_icon_metrics_for(DesktopIconSizeKind k) {
   
  switch (k) {
    case DesktopIconSizeKind::Small: return {36.0, 78.0, 72.0};
    case DesktopIconSizeKind::Large: return {64.0, 130.0, 108.0};
    case DesktopIconSizeKind::Medium:
    default: return {48.0, 104.0, 88.0};
  }
}

void desktop_prefs_ensure_loaded(DesktopApp& app) {
   
  if (app.desktopPrefsLoaded) return;
  app.desktopPrefsLoaded = true;
  app.iconGridByPath.clear();
  const std::string path = layout_file_path();
  if (path.empty()) return;
  std::ifstream f(path);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq != std::string::npos) {
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
      if (key == "arrangement") {
        DesktopIconArrangement a{};
        if (parse_arrangement(std::string_view(val), &a)) app.iconArrangement = a;
      } else if (key == "sort") {
        DesktopIconSort s{};
        if (parse_sort(std::string_view(val), &s)) app.iconSortMode = s;
      } else if (key == "size") {
        DesktopIconSizeKind z{};
        if (parse_size(std::string_view(val), &z)) app.iconSizeKind = z;
      } else if (key == "icons_visible") {
        const std::string v = val;
        if (v == "0" || v == "false") app.desktopIconsVisible = false;
        else if (v == "1" || v == "true") app.desktopIconsVisible = true;
      }
      continue;
    }
    const auto tab1 = line.find('\t');
    if (tab1 == std::string::npos) continue;
    const auto tab2 = line.find('\t', tab1 + 1);
    if (tab2 == std::string::npos) continue;
    const auto tab3 = line.find('\t', tab2 + 1);
    std::string pathkey = line.substr(0, tab1);
    const std::string cols = line.substr(tab1 + 1, tab2 - tab1 - 1);
    const std::string rows = line.substr(tab2 + 1, (tab3 != std::string::npos) ? (tab3 - tab2 - 1) : std::string::npos);
    const std::string layers = (tab3 != std::string::npos) ? line.substr(tab3 + 1) : "0";
    try {
      const int c = std::stoi(cols);
      const int r = std::stoi(rows);
      const int l = std::stoi(layers);
      if (c >= 0 && r >= 0 && l >= 0) app.iconGridByPath[pathkey] = DesktopIconPrefs{c, r, l};
    } catch (const std::exception&) {
    }
  }
}

void desktop_prefs_save(DesktopApp& app) {
    
  const std::string path = layout_file_path();
  if (path.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream f(path, std::ios::trunc);
  if (!f) return;
  f << "# EventHorizon desktop icon layout (generated)\n";
  f << "arrangement=" << arrangement_token(app.iconArrangement) << '\n';
  f << "sort=" << sort_token(app.iconSortMode) << '\n';
  f << "size=" << size_token(app.iconSizeKind) << '\n';
  f << "icons_visible=" << (app.desktopIconsVisible ? "1" : "0") << '\n';
  if (app.iconArrangement == DesktopIconArrangement::AlignToGrid
      || app.iconArrangement == DesktopIconArrangement::Stacked
      || app.iconArrangement == DesktopIconArrangement::StackedRight) {
    for (const auto& ic : app.icons)
      f << ic.desktop_path << '\t' << ic.grid_col << '\t' << ic.grid_row << '\t' << ic.layer << '\n';
  }
}

void desktop_prefs_set_arrangement(DesktopApp& app, DesktopIconArrangement a) {
   
  app.iconArrangement = a;
  app.iconLayoutW.clear();
  if (a == DesktopIconArrangement::Stacked || a == DesktopIconArrangement::StackedRight) {
    for (auto& ic : app.icons) {
      ic.grid_col = -1;
      ic.grid_row = -1;
    }
    app.iconGridByPath.clear();
  }
  desktop_prefs_save(app);
}

void desktop_prefs_set_sort_mode(DesktopApp& app, DesktopIconSort s) {
   
  app.iconSortMode = s;
  app.iconLayoutW.clear();
  app.iconLayoutH.clear();
  desktop_prefs_save(app);
}

void desktop_prefs_set_icon_size(DesktopApp& app, DesktopIconSizeKind k) {
   
  app.iconSizeKind = k;
  app.iconLayoutW.clear();
  app.iconLayoutH.clear();
  desktop_prefs_save(app);
}

void desktop_prefs_set_icons_visible(DesktopApp& app, bool visible) {
   
  app.desktopIconsVisible = visible;
  app.iconLayoutW.clear();
  app.iconLayoutH.clear();
  desktop_prefs_save(app);
}

}
