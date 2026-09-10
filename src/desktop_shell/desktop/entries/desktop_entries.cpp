#include "desktop_shell/desktop/entries/desktop_entries.hpp"

#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"

#include "desktop_shell/common/ns/namespaces.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <dirent.h>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <unistd.h>

using eh::shell::str::file_exists;
using eh::shell::str::split_colon_list;
using eh::shell::str::trim;

namespace {

std::vector<std::string> split_list(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ';' || c == ',') {
      std::string t = trim(cur);
      if (!t.empty()) out.push_back(std::move(t));
      cur.clear();
    } else {
      cur += c;
    }
  }
  std::string t = trim(cur);
  if (!t.empty()) out.push_back(std::move(t));
  return out;
}

void push_unique_desktop_candidate(std::vector<std::string>& v, std::string name) {
   
  if (name.size() < 9 || !name.ends_with(".desktop")) return;
  for (const auto& x : v)
    if (x == name) return;
  v.push_back(std::move(name));
}

std::vector<std::string> desktop_id_candidate_bases(const std::string& appId) {
   
  std::string base = appId;
  if (base.size() > 8 && base.ends_with(".desktop")) base.resize(base.size() - 8);

  if (base.size() > 4 && base.ends_with(".exe")) base.resize(base.size() - 4);
  if (base.size() > 4 && base.ends_with(".bin")) base.resize(base.size() - 4);

  std::vector<std::string> out;
  auto pushBase = [&](std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t j = 0;
    while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) j++;
    if (j) s.erase(0, j);
    if (s.empty()) return;
    for (const auto& x : out)
      if (x == s) return;
    out.push_back(std::move(s));
  };

  pushBase(base);
  std::string lower = base;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  pushBase(lower);

  auto spaces_to_hyphens = [](std::string s) {
    for (char& c : s)
      if (c == ' ' || c == '\t') c = '-';
    return s;
  };
  pushBase(spaces_to_hyphens(base));
  pushBase(spaces_to_hyphens(lower));

  auto remove_spaces = [](std::string s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '\t'; }), s.end());
    return s;
  };
  pushBase(remove_spaces(base));
  pushBase(remove_spaces(lower));

  auto underscores_to_hyphens = [](std::string s) {
    for (char& c : s)
      if (c == '_') c = '-';
    return s;
  };
  pushBase(underscores_to_hyphens(base));
  pushBase(underscores_to_hyphens(lower));

  return out;
}

std::string desktop_lowercase(std::string s) {
    
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Reads a single `key=` value from the `[Desktop Entry]` section of a .desktop file.
std::string desktop_read_desktop_entry_key(const std::string& path, const std::string& key) {
    
  std::ifstream f(path);
  if (!f.is_open()) return {};
  std::string line;
  std::string section;
  const std::string want = key + "=";
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      section = s.substr(1, s.size() - 2);
      continue;
    }
    if (section != "Desktop Entry") continue;
    if (s.rfind(want, 0) == 0) return trim(s.substr(want.size()));
  }
  return {};
}

struct DesktopResolutionFallbackIndex {
  bool built = false;
  // Legacy ids (e.g. flatpak `X-Flatpak-RenamedFrom`) -> desktop file path.
  std::unordered_map<std::string, std::string> renamedFromToPath;
  // Lowercased trailing dotted segment of a desktop file stem -> matching paths.
  std::unordered_map<std::string, std::vector<std::string>> lastSegmentToPaths;
};

DesktopResolutionFallbackIndex& desktop_resolution_fallback_index() {
    
  static DesktopResolutionFallbackIndex idx;
  return idx;
}

void ensure_desktop_resolution_fallback_index_built() {
    
  auto& idx = desktop_resolution_fallback_index();
  if (idx.built) return;
  idx.built = true;

  std::vector<std::string> dirs;
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) dirs.push_back(std::string(xdg) + "/applications");
  if (dirs.empty()) {
    if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.local/share/applications");
  }
  auto dataDirs = split_colon_list(std::getenv("XDG_DATA_DIRS"));
  if (dataDirs.empty()) dataDirs = {"/usr/local/share", "/usr/share"};
  for (const auto& d : dataDirs) dirs.push_back(d + "/applications");

  for (const auto& dir : dirs) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (dirent* ent = readdir(d)) {
      std::string name(ent->d_name);
      if (name.size() < 9 || !name.ends_with(".desktop")) continue;
      const std::string path = dir + "/" + name;
      if (!file_exists(path) || !std::filesystem::is_regular_file(path)) continue;

      const std::string stem = name.substr(0, name.size() - 8);
      const auto dot = stem.rfind('.');
      if (dot != std::string::npos && dot + 1 < stem.size()) {
        std::string seg = desktop_lowercase(stem.substr(dot + 1));
        if (!seg.empty()) idx.lastSegmentToPaths[seg].push_back(path);
      }

      const std::string renamed = desktop_read_desktop_entry_key(path, "X-Flatpak-RenamedFrom");
      if (!renamed.empty()) {
        std::string cur;
        const auto index_legacy = [&](std::string legacy) {
          std::string norm = eh::shell::paths::normalize_desktop_app_id(legacy);
          if (norm.empty() || norm == "unknown") return;
          if (idx.renamedFromToPath.find(norm) == idx.renamedFromToPath.end())
            idx.renamedFromToPath.emplace(std::move(norm), path);
        };
        for (char c : renamed) {
          if (c == ';') {
            index_legacy(cur);
            cur.clear();
          } else {
            cur.push_back(c);
          }
        }
        index_legacy(cur);
      }
    }
    closedir(d);
  }
}

std::string strip_mixer_exec_field_codes(std::string s) {
   
  static const char* codes[] = {"%f", "%F", "%u", "%U", "%d", "%D", "%n", "%N",
                               "%i", "%c", "%k", "%v", "%m", "%e"};
  for (;;) {
    const size_t before = s.size();
    for (auto c : codes) {
      const size_t len = std::strlen(c);
      for (;;) {
        const auto pos = s.find(c);
        if (pos == std::string::npos) break;
        s.erase(pos, len);
      }
    }
    if (s.size() == before) break;
  }
  return trim(std::move(s));
}

std::string mixer_path_basename(std::string p) {
   
  while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
  const auto slash = p.find_last_of("/\\");
  if (slash != std::string::npos && slash + 1 < p.size()) return p.substr(slash + 1);
  return p;
}

// Lowercased basename of the first real executable token in an `Exec=` line (field codes stripped).
std::string desktop_exec_first_binary_basename_lower(std::string exec_line) {
   
  exec_line = strip_mixer_exec_field_codes(trim(std::move(exec_line)));
  if (exec_line.empty()) return {};

  std::vector<std::string> toks;
  std::string cur;
  for (char ch : exec_line) {
    if (ch == ' ' || ch == '\t') {
      if (!cur.empty()) {
        toks.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) toks.push_back(cur);

  size_t i = 0;
  if (!toks.empty() && toks[0] == "env") {
    i = 1;
    while (i < toks.size()) {
      const std::string& t = toks[i];
      const bool looks_like_assign =
          (t.find('=') != std::string::npos && t.find('/') == std::string::npos);
      if (looks_like_assign && !t.starts_with("-")) {
        ++i;
        continue;
      }
      break;
    }
  }

  for (; i < toks.size(); ++i) {
    const std::string& t = toks[i];
    if (t.empty()) continue;
    if (t[0] == '-') continue;
    const bool assign_no_slash = (t.find('=') != std::string::npos && t.find('/') == std::string::npos);
    if (assign_no_slash) continue;

    std::string bn = mixer_path_basename(t);
    std::transform(bn.begin(), bn.end(), bn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (bn == "flatpak" || bn == "snap") return {};
    return bn;
  }
  return {};
}

}  // namespace

std::optional<DesktopEntryInfo> read_desktop_entry_info(const std::string& desktopFilePath) {
   
  DesktopEntryInfo info;

  std::ifstream f(desktopFilePath);
  if (!f.is_open()) return std::nullopt;

  std::string line;
  std::string section;
  std::vector<std::string> actionIds;

  auto parse_actions_list = [](const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : v) {
      if (c == ';') {
        cur = trim(cur);
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    cur = trim(cur);
    if (!cur.empty()) out.push_back(cur);
    return out;
  };

  auto find_action = [&](const std::string& id) -> DesktopAction* {
    for (auto& a : info.actions) {
      if (a.id == id) return &a;
    }
    info.actions.push_back(DesktopAction{.id = id, .name = {}, .exec = {}, .icon = {}});
    return &info.actions.back();
  };

  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      section = s.substr(1, s.size() - 2);
      continue;
    }

    const auto eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(s.substr(0, eq));
    const std::string val = s.substr(eq + 1);

    if (section == "Desktop Entry") {
      if (key == "Type") {
        std::string t = val;
        for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!t.empty()) info.type = t;
      } else if (key == "Name" && info.name.empty()) {
        info.name = val;
      } else if (key == "Icon" && info.icon.empty()) {
        info.icon = val;
      } else if (key == "Exec" && info.exec.empty()) {
        info.exec = val;
      } else if (key == "StartupWMClass" && info.startup_wm_class.empty()) {
        info.startup_wm_class = val;
      } else if (key == "URL" && info.url.empty()) {
        info.url = val;
      } else if (key == "DBusActivatable") {
        std::string v = val;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info.dbus_activatable = (v == "true" || v == "1");
      } else if (key == "Terminal") {
        std::string v = val;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info.terminal = (v == "true" || v == "1");
      } else if (key == "Hidden") {
        std::string v = val;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info.hidden = (v == "true" || v == "1");
      } else if (key == "NoDisplay") {
        std::string v = val;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info.noDisplay = (v == "true" || v == "1");
      } else if (key == "TryExec") {
        if (info.tryExec.empty()) info.tryExec = val;
      } else if (key == "OnlyShowIn") {
        // comma or semicolon separated
        for (auto& part : split_list(val)) if (!part.empty()) info.onlyShowIn.push_back(part);
      } else if (key == "NotShowIn") {
        for (auto& part : split_list(val)) if (!part.empty()) info.notShowIn.push_back(part);
      } else if (key == "X-GNOME-Autostart-enabled") {
        std::string v = val;
        for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info.autostartEnabled = !(v == "false" || v == "0");
      } else if (key == "X-GNOME-Autostart-Delay") {
        info.autostartDelaySec = std::max(0, atoi(val.c_str()));
      } else if (key == "Actions") {
        actionIds = parse_actions_list(val);
      }
    } else if (section.rfind("Desktop Action ", 0) == 0) {
      const std::string id = section.substr(std::strlen("Desktop Action "));
      if (id.empty()) continue;
      DesktopAction* act = find_action(id);
      if (!act) continue;
      if (key == "Name" && act->name.empty()) act->name = val;
      else if (key == "Exec" && act->exec.empty()) act->exec = val;
      else if (key == "Icon" && act->icon.empty()) act->icon = val;
    }
  }

  if (!actionIds.empty()) {
    std::vector<DesktopAction> ordered;
    ordered.reserve(actionIds.size());
    for (const auto& id : actionIds) {
      for (const auto& a : info.actions) {
        if (a.id == id) {
          ordered.push_back(a);
          break;
        }
      }
    }
    info.actions = std::move(ordered);
  }

  return info;
}

std::optional<std::string> find_desktop_file_for_appid(const std::string& appId) {
   
  // Fast path for absolute paths (e.g., pinned apps stored as full .desktop paths)
  if (appId.starts_with("/")) {
    // Already an absolute path - check if it exists as-is or with .desktop suffix
    // Must be a regular file (not a block device like /dev/sdb1) to prevent
    // read_desktop_pin_identity_fields from blocking on raw device I/O.
    if (file_exists(appId) && std::filesystem::is_regular_file(appId)) return appId;
    if (!appId.empty() && appId.back() != '.' && !appId.ends_with(".desktop")) {
      const std::string withDesktop = appId + ".desktop";
      if (std::filesystem::is_regular_file(withDesktop)) return withDesktop;
    }
    // If we get here with a .desktop path but it doesn't exist, fall through to normal resolution

    // Case-insensitive fallback: scan the parent directory for a matching name
    {
      const auto slash = appId.find_last_of('/');
      if (slash != std::string::npos && slash + 1 < appId.size()) {
        const std::string parentDir = appId.substr(0, slash);
        std::string targetBase = appId.substr(slash + 1);
        std::string targetDesktop = targetBase + ".desktop";
        std::transform(targetBase.begin(), targetBase.end(), targetBase.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(targetDesktop.begin(), targetDesktop.end(), targetDesktop.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        DIR* d = opendir(parentDir.c_str());
        if (d) {
          while (dirent* ent = readdir(d)) {
            std::string name(ent->d_name);
            std::string nameLower = name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (nameLower == targetBase || nameLower == targetDesktop) {
              const std::string found = parentDir + "/" + name;
              if (std::filesystem::is_regular_file(found)) {
                closedir(d);
                return found;
              }
            }
          }
          closedir(d);
        }
      }
    }

  }

  const std::vector<std::string> bases = desktop_id_candidate_bases(appId);

  std::vector<std::string> dirs;
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) dirs.push_back(std::string(xdg) + "/applications");
  if (dirs.empty()) {
    if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.local/share/applications");
  }
  auto dataDirs = split_colon_list(std::getenv("XDG_DATA_DIRS"));
  if (dataDirs.empty()) dataDirs = {"/usr/local/share", "/usr/share"};
  for (const auto& d : dataDirs) dirs.push_back(d + "/applications");

  std::vector<std::string> candidates;
  for (const auto& b : bases) {
    push_unique_desktop_candidate(candidates, b + ".desktop");
    std::string lower = b;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower != b) push_unique_desktop_candidate(candidates, lower + ".desktop");
  }

  for (const auto& dir : dirs) {
    for (const auto& c : candidates) {
      const std::string path = dir + "/" + c;
      if (file_exists(path)) return path;
    }
  }

  for (const auto& dir : dirs) {
    for (const auto& c : candidates) {
      std::string wantLower = c;
      std::transform(wantLower.begin(), wantLower.end(), wantLower.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      DIR* d = opendir(dir.c_str());
      if (!d) continue;
      while (dirent* ent = readdir(d)) {
        std::string name(ent->d_name);
        if (name.size() < 9) continue;
        std::string nameLower = name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (nameLower == wantLower) {
          const std::string path = dir + "/" + name;
          if (file_exists(path)) {
            closedir(d);
            return path;
          }
        }
      }
      closedir(d);
    }
  }

  // Fallback for pins stored under legacy/short ids that no longer match a
  // .desktop filename exactly. Two cases are common:
  //  - Flatpak apps renamed via `X-Flatpak-RenamedFrom` (e.g. "cider" was
  //    renamed to "sh.cider.Cider").
  //  - Reverse-DNS ids where only the trailing segment is pinned (e.g.
  //    "resolve" instead of "com.blackmagicdesign.resolve").
  ensure_desktop_resolution_fallback_index_built();
  const auto& idx = desktop_resolution_fallback_index();
  const std::string norm = eh::shell::paths::normalize_desktop_app_id(appId);
  if (!norm.empty() && norm != "unknown") {
    if (auto it = idx.renamedFromToPath.find(norm); it != idx.renamedFromToPath.end()) return it->second;
    const auto dot = norm.rfind('.');
    const std::string seg =
        (dot != std::string::npos && dot + 1 < norm.size()) ? norm.substr(dot + 1) : norm;
    if (!seg.empty()) {
      if (auto vit = idx.lastSegmentToPaths.find(seg);
          vit != idx.lastSegmentToPaths.end() && vit->second.size() == 1) {
        return vit->second.front();
      }
    }
  }
  return std::nullopt;
}

const eh::icons::IconEntry* mixer_icon_from_pinned_apps(eh::icons::IconCache& icons,
                                                        const std::vector<std::string>& pinnedApps,
                                                        const std::string& process_binary) {
   
  std::string binLower = mixer_path_basename(process_binary);
  std::transform(binLower.begin(), binLower.end(), binLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (binLower.empty() || binLower == "bwrap" || binLower == "flatpak-bwrap" || binLower == "fuse-overlayfs" ||
      binLower == "snap-confine" || binLower == "xdg-dbus-proxy") {
    return nullptr;
  }

  for (const std::string& pinRaw : pinnedApps) {
    if (pinRaw.empty()) continue;
    const auto path = find_desktop_file_for_appid(pinRaw);
    if (!path) continue;
    const auto info = read_desktop_entry_info(*path);
    if (!info || info->type != "application") continue;
    if (desktop_exec_first_binary_basename_lower(info->exec) != binLower) continue;

    if (const eh::icons::IconEntry* ic = icons.app_icon(pinRaw))
      if (ic->surface) return ic;

    std::string stem = pinRaw;
    if (stem.size() > 8 && stem.ends_with(".desktop")) stem.resize(stem.size() - 8);
    if (const auto slash = stem.find_last_of('/'); slash != std::string::npos && slash + 1 < stem.size()) {
      stem = stem.substr(slash + 1);
    }
    if (!stem.empty() && stem != pinRaw) {
      if (const eh::icons::IconEntry* ic = icons.app_icon(stem))
        if (ic->surface) return ic;
    }
  }
  return nullptr;
}

bool is_generic_runtime_binary(const std::string& lower) {
   
  static const std::unordered_set<std::string> kGeneric = {
    "chromium", "chromium-browser", "chrome", "google-chrome",
    "chromium input", "chromium output",
    "electron", "electron32", "electron31", "electron30",
    "node", "python", "python3", "java", "wine", "wineserver",
  };
  return kGeneric.count(lower) > 0;
}

std::vector<std::string> stream_desktop_candidates(const StreamDesktopIds& ids) {
   
  std::vector<std::string> out;

  auto add = [&](const std::string& s) {
    if (s.empty()) return;
    std::string low = s;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (is_generic_runtime_binary(low)) return;
    for (const auto& x : out) if (x == low) return;
    out.push_back(std::move(low));
  };

  if (!ids.process_binary.empty()) {
    std::string bin = ids.process_binary;
    while (!bin.empty() && (bin.back() == '/' || bin.back() == '\\')) bin.pop_back();
    const auto slash = bin.find_last_of("/\\");
    if (slash != std::string::npos && slash + 1 < bin.size()) bin = bin.substr(slash + 1);
    else bin = ids.process_binary;
    add(bin);
  }

  // App ID (full + first dot segment)
  if (!ids.app_id.empty()) {
    add(ids.app_id);
    const auto dot = ids.app_id.find('.');
    if (dot != std::string::npos && dot > 0) add(ids.app_id.substr(0, dot));
  }

  // Application name
  add(ids.app_name);

  // Node name first token (split by [-_.])
  if (!ids.node_name.empty()) {
    std::string tok;
    for (char c : ids.node_name) {
      if (c == '-' || c == '_' || c == '.') { if (!tok.empty()) break; }
      else tok.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (!tok.empty()) add(tok);
  }

  // Node description
  add(ids.node_description);

  // Known-app reverse-domain IDs: append well-known desktop entry identifiers when stream
  // properties match known applications.  This handles cases where the .desktop file is
  // named with its full reverse-domain ID (e.g. dev.vencord.Vesktop.desktop) rather than
  // its short name (vesktop.desktop).
  auto contains_ci = [](const std::string& hay, const std::string& needle) -> bool {
    if (hay.size() < needle.size()) return false;
    std::string h = hay;
    std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return h.find(needle) != std::string::npos;
  };
  auto try_known = [&](const std::vector<std::string>& needles, const std::vector<std::string>& app_ids) {
    for (const auto& n : needles)
      if (contains_ci(ids.process_binary, n) || contains_ci(ids.app_id, n) ||
          contains_ci(ids.app_name, n) || contains_ci(ids.node_name, n) ||
          contains_ci(ids.node_description, n)) {
        for (const auto& a : app_ids) add(a);
        return;
      }
  };
  try_known({"vesktop", "vencord"},          {"dev.vencord.Vesktop", "vesktop"});
  try_known({"cider"},                        {"sh.cider.Cider", "cider"});
  try_known({"discord"},                      {"discord"});
  try_known({"msedge", "microsoft edge"},     {"com.microsoft.Edge", "microsoft-edge", "msedge"});

  return out;
}

std::optional<DesktopEntryInfo> resolve_desktop_entry_for_stream(const StreamDesktopIds& ids) {
   
  const auto candidates = stream_desktop_candidates(ids);
  for (const auto& c : candidates) {
    auto path = find_desktop_file_for_appid(c);
    if (!path) continue;
    auto info = read_desktop_entry_info(*path);
    if (info && info->type == "application") return info;
  }
  return std::nullopt;
}
