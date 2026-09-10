#include "platform/widgets/app_drawer/list/desktop_list.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string toLower(std::string_view s) {
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

}

std::string eh_app_drawer_desktop_stem_from_path(std::string_view desktop_file_path) {
  auto p = fs::path(desktop_file_path);
  return p.stem().string();
}

void eh_app_drawer_menu_query(std::string_view filter, const std::vector<std::string>* startMenuPins,
                              std::vector<SpotlightHit>* out) {
  (void)startMenuPins;
  if (!filter.empty() && out) {
    auto lower = toLower(filter);
    for (const auto& entry : eh::shell::dock::app_drawer::get_cached_entries()) {
      if (toLower(entry.name).find(lower) != std::string::npos ||
          toLower(entry.genericName).find(lower) != std::string::npos) {
        SpotlightHit hit;
        hit.path = entry.path;
        hit.name = entry.name;
        hit.genericName = entry.genericName;
        hit.comment = entry.comment;
        hit.exec = entry.exec;
        hit.iconKey = entry.icon;
        hit.categories = entry.categories;
        hit.score = 1;
        out->push_back(std::move(hit));
      }
    }
  }
}

void eh_app_drawer_menu_query_filtered(std::string_view filter, const std::vector<size_t>&,
                                        const std::vector<std::string>* startMenuPins,
                                        std::vector<SpotlightHit>* out) {
  eh_app_drawer_menu_query(filter, startMenuPins, out);
}

namespace eh::shell::dock::app_drawer {

static std::vector<DesktopEntry> g_cached_entries;
static bool g_cache_valid = false;

void invalidate_desktop_entries_cache() {
  g_cache_valid = false;
}

const std::vector<DesktopEntry>& get_cached_entries() {
  if (!g_cache_valid) {
    g_cached_entries = copy_desktop_entries();
    g_cache_valid = true;
  }
  return g_cached_entries;
}

std::vector<DesktopEntry> copy_desktop_entries() {
  std::vector<DesktopEntry> entries;

  // Search standard XDG data directories for .desktop files
  const char* xdg_data_dirs = std::getenv("XDG_DATA_DIRS");
  std::string search_paths = xdg_data_dirs ? xdg_data_dirs : "/usr/local/share:/usr/share";
  search_paths += ":" + (std::getenv("XDG_DATA_HOME") ? std::string(std::getenv("XDG_DATA_HOME")) : std::string(std::getenv("HOME")) + "/.local/share");

  std::string apps_dir;
  size_t start = 0, end;
  while ((end = search_paths.find(':', start)) != std::string::npos) {
    apps_dir = search_paths.substr(start, end - start) + "/applications";
    start = end + 1;
    if (fs::is_directory(apps_dir)) break;
  }
  if (start < search_paths.size())
    apps_dir = search_paths.substr(start) + "/applications";
  if (!fs::is_directory(apps_dir))
    apps_dir = "/usr/share/applications";

  if (!fs::is_directory(apps_dir)) return entries;

  for (const auto& de : fs::directory_iterator(apps_dir)) {
    if (de.path().extension() != ".desktop") continue;
    std::ifstream f(de.path());
    if (!f.is_open()) continue;

    DesktopEntry entry;
    entry.path = de.path().string();

    std::string line;
    bool in_desktop_entry = false;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      if (line[0] == '[') {
        in_desktop_entry = (line == "[Desktop Entry]");
        continue;
      }
      if (!in_desktop_entry) continue;

      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);

      if (key == "Name") entry.name = val;
      else if (key == "GenericName") entry.genericName = val;
      else if (key == "Comment") entry.comment = val;
      else if (key == "Exec") entry.exec = val;
      else if (key == "Icon") entry.icon = val;
      else if (key == "Categories") entry.categories = val;
      else if (key == "MimeType") entry.mimeTypesLower = toLower(val);
      else if (key == "Keywords") entry.keywords = val;
      else if (key == "NoDisplay") entry.noDisplay = (val == "true");
      else if (key == "Hidden") entry.hidden = (val == "true");
      else if (key == "Terminal") entry.terminal = (val == "true");
    }

    if (!entry.name.empty()) {
      entries.push_back(std::move(entry));
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const DesktopEntry& a, const DesktopEntry& b) {
              return a.name < b.name;
            });

  return entries;
}

int entry_score(std::string_view pattern, const DesktopEntry& e) {
  auto lower = toLower(pattern);
  int score = 0;
  auto nl = toLower(e.name);
  auto gl = toLower(e.genericName);
  auto cl = toLower(e.comment);

  if (nl == lower) score += 100;
  else if (nl.find(lower) == 0) score += 50;
  else if (nl.find(lower) != std::string::npos) score += 25;

  if (gl.find(lower) != std::string::npos) score += 10;
  if (cl.find(lower) != std::string::npos) score += 5;

  return score;
}

bool is_standard_category(const std::string& cat) {
  (void)cat;
  return true;
}

}

std::optional<eh::shell::dock::app_drawer::DesktopEntry> dock_app_drawer_entry_for_pin_in_catalog(
    const std::string& pinRaw,
    const std::vector<eh::shell::dock::app_drawer::DesktopEntry>& catalog) {
  for (const auto& e : catalog) {
    if (e.path == pinRaw || e.name == pinRaw) return e;
  }
  return std::nullopt;
}
