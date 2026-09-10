#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

using namespace eh::shell::dock::app_drawer;

static std::mutex g_desktop_list_mutex;

namespace {

std::string toLower(std::string_view s) {
   
  std::string r(s);
  std::transform(r.begin(), r.end(), r.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return r;
}

std::string trim(std::string_view s) {
   
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
  return std::string(s.substr(a, b - a));
}

static int fuzzyScoreLower(std::string_view patLower, std::string_view text) {
   
  if (patLower.empty()) return 1;
  if (text.empty()) return 0;

  std::string textLower(text.size(), '\0');
  std::transform(text.begin(), text.end(), textLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  {
    std::size_t ti = 0;
    for (std::size_t pi = 0; pi < patLower.size(); ++pi) {
      bool found = false;
      while (ti < textLower.size()) {
        if (textLower[ti] == patLower[pi]) {
          ++ti;
          found = true;
          break;
        }
        ++ti;
      }
      if (!found) return 0;
    }
  }

  auto isWordBoundary = [](std::string_view t, std::size_t pos) {
    if (pos == 0) return true;
    char prev = t[pos - 1];
    char curr = t[pos];
    if (prev == ' ' || prev == '-' || prev == '_' || prev == '.') return true;
    if (std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(curr))) return true;
    return false;
  };

  int totalScore = 0;
  std::size_t patIdx = 0;
  std::size_t textIdx = 0;
  int consecutive = 0;

  while (patIdx < patLower.size() && textIdx < textLower.size()) {
    if (textLower[textIdx] == patLower[patIdx]) {
      int charScore = 1;
      if (isWordBoundary(text, textIdx)) charScore += 10;
      if (consecutive > 0) charScore += 4 * consecutive;
      if (textIdx == 0) charScore += 8;
      if (std::isupper(static_cast<unsigned char>(text[textIdx]))) charScore += 1;
      totalScore += charScore;
      ++consecutive;
      ++patIdx;
    } else {
      consecutive = 0;
    }
    ++textIdx;
  }
  if (patIdx < patLower.size()) return 0;
  if (text.size() <= patLower.size() + 2) totalScore += 15;
  return totalScore;
}

static int fuzzyScoreLowerDirect(std::string_view patLower, std::string_view textLower,
                                 std::string_view textOriginal) {
   
  if (patLower.empty()) return 1;
  if (textLower.empty()) return 0;

  {
    std::size_t ti = 0;
    for (std::size_t pi = 0; pi < patLower.size(); ++pi) {
      bool found = false;
      while (ti < textLower.size()) {
        if (textLower[ti] == patLower[pi]) {
          ++ti;
          found = true;
          break;
        }
        ++ti;
      }
      if (!found) return 0;
    }
  }

  auto isWordBoundary = [](std::string_view t, std::size_t pos) {
    if (pos == 0) return true;
    char prev = t[pos - 1];
    char curr = t[pos];
    if (prev == ' ' || prev == '-' || prev == '_' || prev == '.') return true;
    if (std::islower(static_cast<unsigned char>(prev)) && std::isupper(static_cast<unsigned char>(curr))) return true;
    return false;
  };

  int totalScore = 0;
  std::size_t patIdx = 0;
  std::size_t textIdx = 0;
  int consecutive = 0;

  while (patIdx < patLower.size() && textIdx < textLower.size()) {
    if (textLower[textIdx] == patLower[patIdx]) {
      int charScore = 1;
      if (isWordBoundary(textOriginal, textIdx)) charScore += 10;
      if (consecutive > 0) charScore += 4 * consecutive;
      if (textIdx == 0) charScore += 8;
      if (textIdx < textOriginal.size() && std::isupper(static_cast<unsigned char>(textOriginal[textIdx]))) charScore += 1;
      totalScore += charScore;
      ++consecutive;
      ++patIdx;
    } else {
      consecutive = 0;
    }
    ++textIdx;
  }
  if (patIdx < patLower.size()) return 0;
  if (textOriginal.size() <= patLower.size() + 2) totalScore += 15;
  return totalScore;
}

static int scoreListLower(std::string_view patLower, std::string_view list) {
  int best = 0;
  std::size_t start = 0;
  while (start < list.size()) {
    const auto semi = list.find(';', start);
    const auto word = (semi == std::string_view::npos) ? list.substr(start) : list.substr(start, semi - start);
    if (!word.empty()) best = std::max(best, fuzzyScoreLower(patLower, word));
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return best;
}

static int scoreListLowerDirect(std::string_view patLower, std::string_view listLower) {
  int best = 0;
  std::size_t start = 0;
  while (start < listLower.size()) {
    const auto semi = listLower.find(';', start);
    const auto word = (semi == std::string_view::npos) ? listLower.substr(start) : listLower.substr(start, semi - start);
    if (!word.empty()) best = std::max(best, fuzzyScoreLowerDirect(patLower, word, word));
    if (semi == std::string_view::npos) break;
    start = semi + 1;
  }
  return best;
}

std::vector<std::string> xdg_application_dirs() {
   
  std::vector<std::string> dirs;
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) dirs.push_back(std::string(xdg) + "/applications");
  if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.local/share/applications");

  bool have_explicit_data_dirs = false;
  if (const char* ed = std::getenv("XDG_DATA_DIRS")) {
    if (*ed != '\0') {
      have_explicit_data_dirs = true;
      std::string cur;
      for (const char* p = ed; *p || !cur.empty(); ++p) {
        if (*p == ':' || *p == '\0') {
          if (!cur.empty()) {
            while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
            if (!cur.empty()) dirs.push_back(cur + "/applications");
          }
          cur.clear();
          if (*p == '\0') break;
          continue;
        }
        cur.push_back(*p);
      }
    }
  }
  if (!have_explicit_data_dirs) {
    dirs.push_back("/usr/local/share/applications");
    dirs.push_back("/usr/share/applications");
  }
  return dirs;
}

std::optional<DesktopEntry> parse_desktop_file(const fs::path& filepath) {
   
  std::ifstream file(filepath);
  if (!file.is_open()) return std::nullopt;

  DesktopEntry e;
  e.path = filepath.string();

  bool inDesktopEntry = false;
  std::string line;
  std::string type;

  while (std::getline(file, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line.size() >= 2 && line.front() == '[') {
      inDesktopEntry = (line == "[Desktop Entry]");
      continue;
    }
    if (!inDesktopEntry) continue;

    const auto eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = line.substr(eq + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());

    const bool localized = (!key.empty() && key.back() == ']');
    if (!localized && (key.find('[') != std::string::npos)) continue;

    if (key.rfind("Name", 0) == 0) {
      if (!localized && key == "Name" && e.name.empty()) e.name = val;
    } else if (key.rfind("GenericName", 0) == 0) {
      if (!localized && key == "GenericName" && e.genericName.empty()) e.genericName = val;
    } else if (key.rfind("Comment", 0) == 0) {
      if (!localized && key == "Comment" && e.comment.empty()) e.comment = val;
    } else if (key == "Exec") {
      if (e.exec.empty()) e.exec = val;
    } else if (key == "Icon") {
      if (e.icon.empty()) e.icon = val;
    } else if (key == "Keywords") {
      e.keywords = val;
    } else if (key == "Categories") {
      e.categories = val;
    } else if (key == "MimeType") {
      if (e.mimeTypesLower.empty())
        e.mimeTypesLower = toLower(val);
      else
        e.mimeTypesLower += ';' + toLower(val);
    } else if (key == "NoDisplay") {
      e.noDisplay = (trim(val) == "true");
    } else if (key == "Hidden") {
      e.hidden = (trim(val) == "true");
    } else if (key == "Terminal") {
      e.terminal = (trim(val) == "true");
    } else if (key == "Type") {
      type = trim(val);
    }
  }

  if (type != "Application" && !type.empty()) return std::nullopt;
  if (e.name.empty() || e.exec.empty()) return std::nullopt;
  if (e.noDisplay || e.hidden) return std::nullopt;

  e.nameLower = toLower(e.name);
  e.genericNameLower = toLower(e.genericName);
  e.commentLower = toLower(e.comment);
  e.keywordsLower = toLower(e.keywords);
  e.categoriesLower = toLower(e.categories);

  return e;
}

std::vector<DesktopEntry> scan_desktop_entries() {
   
  std::vector<DesktopEntry> out;
  std::unordered_set<std::string> seenStem;

  for (const std::string& dir : xdg_application_dirs()) {
    const fs::path root(dir);
    if (!fs::is_directory(root)) continue;
    std::error_code ec;
    for (const auto& rec : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
      if (!rec.is_regular_file(ec)) continue;
      auto p = rec.path();
      if (p.extension() != ".desktop") continue;
      const auto stemKey = toLower(p.stem().string());
      if (seenStem.count(stemKey)) continue;
      if (auto e = parse_desktop_file(p)) {
        seenStem.insert(stemKey);
        out.push_back(std::move(*e));
      }
    }
  }
  std::sort(out.begin(), out.end(),
            [](const DesktopEntry& a, const DesktopEntry& b) {
              const int cmp = strcasecmp(a.name.c_str(), b.name.c_str());
              if (cmp != 0) return cmp < 0;
              return a.path < b.path;
            });
  return out;
}

static std::vector<DesktopEntry> g_entries_cache = scan_desktop_entries();

std::vector<DesktopEntry>& entries_cache_mut() {
   
  return g_entries_cache;
}

std::optional<DesktopEntry> desktop_list_entry_for_pin_raw(const std::vector<DesktopEntry>& local,
                                                           const std::string& pinRaw) {
   
  if (auto resolved = find_desktop_file_for_appid(pinRaw)) {
    for (const auto& e : local) {
      if (e.path == *resolved) return e;
    }
  }
  const std::string nw = eh::shell::paths::normalize_desktop_app_id(pinRaw);
  if (nw.empty() || nw == "unknown") return std::nullopt;
  for (const auto& e : local) {
    if (e.noDisplay || e.hidden) continue;
    const std::string stem = eh::shell::paths::normalize_desktop_app_id(eh_app_drawer_desktop_stem_from_path(e.path));
    if (stem == nw) return e;
  }
  return std::nullopt;
}

}

void eh_app_drawer_menu_query(std::string_view filter, const std::vector<std::string>* startMenuPins,
                              std::vector<SpotlightHit>* out) {
   
  if (!out) return;
  out->clear();

  const auto t0 = ShellBenchClock::now();

  while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.front()))) filter.remove_prefix(1);
  while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.back()))) filter.remove_suffix(1);

  if (eh_app_drawer_debug_level() >= 3) {
    std::string prev(filter);
    eh::app_drawer::trace_line(3, "catalog", "menu_query trimmed_len=" + std::to_string(filter.size()) + " filter=\"" + prev + "\"");
  }

  const auto& local = eh::shell::dock::app_drawer::get_cached_entries();

  auto buildHit = [&](const DesktopEntry& e, int score) {
    SpotlightHit h;
    h.path = e.path;
    h.name = e.name;
    h.genericName = e.genericName;
    h.comment = e.comment;
    h.exec = e.exec;
    h.iconKey = e.icon.empty() ? std::string("application-x-executable") : e.icon;
    h.categories = e.categories;
    h.score = score;
    return h;
  };

  std::vector<SpotlightHit> base;
  if (filter.empty()) {
    base.reserve(local.size());
    for (const auto& e : local) base.push_back(buildHit(e, 0));
  } else {
    std::vector<std::pair<int, size_t>> scored;
    scored.reserve(local.size());
    std::string patLower = toLower(filter);
    for (size_t i = 0; i < local.size(); ++i) {
      const int s = eh::app_drawer::entry_score_pre(patLower, local[i]);
      if (s > 0) scored.emplace_back(s, i);
    }
    std::sort(scored.begin(), scored.end(), [&local](const auto& a, const auto& b) {
      if (a.first != b.first) return a.first > b.first;
      const int cmp = strcasecmp(local[a.second].name.c_str(), local[b.second].name.c_str());
      return cmp < 0;
    });

    base.reserve(scored.size());
    for (const auto& pr : scored) base.push_back(buildHit(local[pr.second], pr.first));
  }

  if (!startMenuPins || startMenuPins->empty()) {
    *out = std::move(base);
  } else {
    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(base.size() + startMenuPins->size());
    std::vector<SpotlightHit> merged;
    merged.reserve(base.size() + startMenuPins->size());
    std::string patLower = toLower(filter);
    for (const std::string& pinRaw : *startMenuPins) {
      auto ent = desktop_list_entry_for_pin_raw(local, pinRaw);
      if (!ent.has_value()) continue;
      const int sc = filter.empty() ? 1 : eh::app_drawer::entry_score_pre(patLower, *ent);
      if (sc <= 0) continue;
      if (seenPaths.count(ent->path) != 0) continue;
      seenPaths.insert(ent->path);
      merged.push_back(buildHit(*ent, sc));
    }
    for (auto& h : base) {
      if (seenPaths.count(h.path) != 0) continue;
      seenPaths.insert(h.path);
      merged.push_back(std::move(h));
    }
    *out = std::move(merged);
  }

  if (eh_app_drawer_debug_level() >= 1) {
    eh::app_drawer::trace_line(1, "catalog",
                               "menu_query done out=" + std::to_string(out->size()) + " catalog_entries=" + std::to_string(local.size()) +
                                   (filter.empty() ? std::string(" mode=full_list") : std::string(" mode=filter")));
  }
  {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
    std::cerr << "[search-bench] menu_query filter=\"" << std::string(filter)
              << "\" hits=" << out->size() << " catalog=" << local.size()
              << " mode=" << (filter.empty() ? "full" : "filter")
              << " " << us << "us" << std::endl;
  }
}

void eh_app_drawer_menu_query_filtered(std::string_view filter, const std::vector<size_t>& previous_cat_indices,
                                       const std::vector<std::string>* startMenuPins, std::vector<SpotlightHit>* out) {
    
  if (!out) return;
  out->clear();

  const auto t0 = ShellBenchClock::now();

  while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.front()))) filter.remove_prefix(1);
  while (!filter.empty() && std::isspace(static_cast<unsigned char>(filter.back()))) filter.remove_suffix(1);
  if (filter.empty()) return;

  if (eh_app_drawer_debug_level() >= 3) {
    std::string prev(filter);
    eh::app_drawer::trace_line(3, "catalog", "menu_query_filtered filter=\"" + prev + "\" prev_ix=" + std::to_string(previous_cat_indices.size()));
  }

  const auto& local = eh::shell::dock::app_drawer::get_cached_entries();

  auto buildHit = [&](const DesktopEntry& e, int score) {
    SpotlightHit h;
    h.path = e.path;
    h.name = e.name;
    h.genericName = e.genericName;
    h.comment = e.comment;
    h.exec = e.exec;
    h.iconKey = e.icon.empty() ? std::string("application-x-executable") : e.icon;
    h.categories = e.categories;
    h.score = score;
    return h;
  };

  std::vector<std::pair<int, size_t>> scored;
  scored.reserve(previous_cat_indices.size());
  std::string patLower = toLower(filter);
  for (size_t idx : previous_cat_indices) {
    if (idx >= local.size()) continue;
    const int s = eh::app_drawer::entry_score_pre(patLower, local[idx]);
    if (s > 0) scored.emplace_back(s, idx);
  }
  std::sort(scored.begin(), scored.end(), [&local](const auto& a, const auto& b) {
    if (a.first != b.first) return a.first > b.first;
    const int cmp = strcasecmp(local[a.second].name.c_str(), local[b.second].name.c_str());
    return cmp < 0;
  });

  std::vector<SpotlightHit> base;
  base.reserve(scored.size());
  for (const auto& pr : scored) base.push_back(buildHit(local[pr.second], pr.first));

  if (!startMenuPins || startMenuPins->empty()) {
    *out = std::move(base);
    return;
  }

  std::unordered_set<std::string> seenPaths;
  seenPaths.reserve(base.size() + startMenuPins->size());
  std::vector<SpotlightHit> merged;
  merged.reserve(base.size() + startMenuPins->size());
  for (const std::string& pinRaw : *startMenuPins) {
    auto ent = desktop_list_entry_for_pin_raw(local, pinRaw);
    if (!ent.has_value()) continue;
    const int sc = eh::app_drawer::entry_score_pre(patLower, *ent);
    if (sc <= 0) continue;
    if (seenPaths.count(ent->path) != 0) continue;
    seenPaths.insert(ent->path);
    merged.push_back(buildHit(*ent, sc));
  }
  for (auto& h : base) {
    if (seenPaths.count(h.path) != 0) continue;
    seenPaths.insert(h.path);
    merged.push_back(std::move(h));
  }
  *out = std::move(merged);
  {
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(ShellBenchClock::now() - t0).count();
    std::cerr << "[search-bench] menu_query_filtered filter=\"" << std::string(filter)
              << "\" hits=" << out->size() << " prev_ix=" << previous_cat_indices.size()
              << " " << us << "us" << std::endl;
  }
}

std::string eh_app_drawer_desktop_stem_from_path(std::string_view desktop_file_path) {
   
  const size_t slash = desktop_file_path.rfind('/');
  std::string base =
      (slash == std::string_view::npos) ? std::string(desktop_file_path)
                                        : std::string(desktop_file_path.substr(slash + 1));
  if (base.size() > 8 && base.compare(base.size() - 8, 8, ".desktop") == 0) base.resize(base.size() - 8);
  return base;
}

namespace eh::shell::dock::app_drawer {

bool is_standard_category(const std::string& cat) {
   
  static const std::unordered_set<std::string_view> kStandardCats = {
    "AudioVideo", "Audio", "Video",
    "Development",
    "Education",
    "Game",
    "Graphics",
    "Network",
    "Office",
    "Settings",
    "System",
    "Utility",
  };
  return kStandardCats.count(cat) > 0;
}

int entry_score(std::string_view pattern, const DesktopEntry& e) {
   
  if (pattern.empty()) return 1;
  std::string patLower = toLower(pattern);
  const int nameScore = fuzzyScoreLower(patLower, e.name) * 3;
  const int genericScore = fuzzyScoreLower(patLower, e.genericName) * 2;
  const int commentScore = fuzzyScoreLower(patLower, e.comment) * 2;
  const int keywordScore = scoreListLower(patLower, e.keywords);
  const int catScore = scoreListLower(patLower, e.categories);
  return std::max({nameScore, genericScore, commentScore, keywordScore, catScore});
}

int entry_score_pre(std::string_view patLower, const DesktopEntry& e) {
   
  if (patLower.empty()) return 1;
  const int nameScore = fuzzyScoreLowerDirect(patLower, e.nameLower, e.name) * 3;
  const int genericScore = fuzzyScoreLowerDirect(patLower, e.genericNameLower, e.genericName) * 2;
  const int commentScore = fuzzyScoreLowerDirect(patLower, e.commentLower, e.comment) * 2;
  const int keywordScore = scoreListLowerDirect(patLower, e.keywordsLower);
  const int catScore = scoreListLowerDirect(patLower, e.categoriesLower);
  return std::max({nameScore, genericScore, commentScore, keywordScore, catScore});
}

std::vector<DesktopEntry> copy_desktop_entries() {
   
  std::lock_guard<std::mutex> lock(g_desktop_list_mutex);
  return entries_cache_mut();
}

const std::vector<DesktopEntry>& get_cached_entries() {
   
  return entries_cache_mut();
}

void invalidate_desktop_entries_cache() {
   
  g_entries_cache = scan_desktop_entries();
}

}

std::optional<DesktopEntry> dock_app_drawer_entry_for_pin_in_catalog(
    const std::string& pinRaw, const std::vector<DesktopEntry>& catalog) {
   
  if (auto resolved = find_desktop_file_for_appid(pinRaw)) {
    for (const auto& e : catalog) {
      if (e.path == *resolved) return e;
    }
  }
  const std::string nw = eh::shell::paths::normalize_desktop_app_id(pinRaw);
  if (nw.empty() || nw == "unknown") return std::nullopt;
  for (const auto& e : catalog) {
    if (e.noDisplay || e.hidden) continue;
    const std::string stem = eh::shell::paths::normalize_desktop_app_id(eh_app_drawer_desktop_stem_from_path(e.path));
    if (stem == nw) return e;
  }
  return std::nullopt;
}


