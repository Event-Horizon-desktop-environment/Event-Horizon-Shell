#include "core/screenshot/icon_cache.hpp"
#include "core/screenshot/logging.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <optional>
#include <poll.h>
#include <sstream>
#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>

#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#endif

extern "C" char** environ;

namespace hpv::sc {

namespace {
constexpr std::size_t kIconSurfaceCacheMaxBytes = 2 * 1024 * 1024;
constexpr int kMaxIconRasterSidePx = 128;

std::shared_ptr<IconCacheData>& shared_icon_cache_data()
{
  static auto data = std::make_shared<IconCacheData>();
  return data;
}
}

static std::uint64_t s_iconThemeGeneration = 0;
static std::string s_iconThemeOverride;

static bool file_exists(const std::string& path) { return access(path.c_str(), R_OK) == 0; }

static std::vector<std::string> split_colon_list(const char* s)
{
  std::vector<std::string> out;
  if (!s) return out;
  std::string cur;
  for (const char* p = s; *p; p++) {
    if (*p == ':') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(*p);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

static std::string trim(std::string s)
{
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i) s.erase(0, i);
  return s;
}

static std::string trim_quotes(std::string s)
{
  s = trim(std::move(s));
  if (s.size() >= 2) {
    if ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"')) {
      s = s.substr(1, s.size() - 2);
    }
  }
  return trim(std::move(s));
}

static std::string run_cmd_capture(const std::string& cmd)
{
  std::array<int, 2> fd{};
  if (pipe(fd.data()) < 0) return {};

  std::vector<char> arg_c(cmd.begin(), cmd.end());
  arg_c.push_back('\0');

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(fd[0]); close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]); close(fd[1]);
    return {};
  }
  if (fd[0] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[0]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]); close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_addclose(&fa, fd[1]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]); close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]); close(fd[1]);
    return {};
  }

  char argv0[] = "/bin/sh";
  char argv1[] = "sh";
  char argv2[] = "-c";
  char* argv[] = {argv0, argv1, argv2, arg_c.data(), nullptr};

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, "/bin/sh", &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fd[1]);
  if (spawn_err != 0 || pid < 0) {
    close(fd[0]);
    return {};
  }
  std::string out;
  std::array<char, 256> buf{};
  for (;;) {
    pollfd pfd{fd[0], POLLIN, 0};
    const int pr = poll(&pfd, 1, 5000);
    if (pr <= 0) break;
    const ssize_t n = read(fd[0], buf.data(), buf.size());
    if (n <= 0) break;
    out.append(buf.data(), static_cast<size_t>(n));
  }
  close(fd[0]);
  (void)waitpid(pid, nullptr, 0);
  return out;
}

static std::string read_ini_kv(const std::string& path, const std::string& wantSection, const std::string& wantKey)
{
  std::ifstream f(path);
  if (!f.is_open()) return {};
  std::string line;
  bool inSec = wantSection.empty();
  std::string out;
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      inSec = (s == wantSection);
      continue;
    }
    if (!inSec) continue;
    if (s.rfind(wantKey + "=", 0) == 0) {
      out = s.substr(wantKey.size() + 1);
      break;
    }
  }
  return out;
}

static std::vector<std::string> icon_base_dirs()
{
  std::vector<std::string> bases;
  auto push_unique = [&](const std::string& s) {
    if (s.empty()) return;
    if (std::find(bases.begin(), bases.end(), s) != bases.end()) return;
    bases.push_back(s);
  };

  const char* home = std::getenv("HOME");
  if (home) {
    push_unique(std::string(home) + "/.icons");
    push_unique(std::string(home) + "/.themes");
    push_unique(std::string(home) + "/.local/share/icons");
    push_unique(std::string(home) + "/.local/share/flatpak/exports/share/icons");
  }

  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    push_unique(std::string(xdg) + "/icons");
  }

  auto dataDirs = split_colon_list(std::getenv("XDG_DATA_DIRS"));
  if (dataDirs.empty()) dataDirs = {"/usr/local/share", "/usr/share"};
  for (const auto& d : dataDirs) {
    push_unique(d + "/icons");
    push_unique(d + "/themes");
  }

  push_unique("/var/lib/flatpak/exports/share/icons");
  push_unique("/run/flatpak/exports/share/icons");
  return bases;
}

std::string detect_system_icon_theme()
{
  if (const char* e = std::getenv("EH_ICON_THEME")) return std::string(e);

  static std::string cached;
  static auto lastCheck = std::chrono::steady_clock::time_point{};
  constexpr auto kCacheDuration = std::chrono::seconds(30);
  auto now = std::chrono::steady_clock::now();
  if (!cached.empty() && (now - lastCheck) < kCacheDuration) return cached;
  lastCheck = now;

  {
    const std::string raw = run_cmd_capture("gsettings get org.gnome.desktop.interface icon-theme 2>/dev/null");
    const std::string v = trim_quotes(raw);
    if (!v.empty()) { cached = v; return cached; }
  }

  if (const char* home = std::getenv("HOME")) {
    for (const char* rel : {"/.config/gtk-4.0/settings.ini", "/.config/gtk-3.0/settings.ini"}) {
      const std::string v = read_ini_kv(std::string(home) + rel, "[Settings]", "gtk-icon-theme-name");
      if (!v.empty()) { cached = v; return cached; }
    }
    {
      const std::string v = read_ini_kv(std::string(home) + "/.config/kdeglobals", "[Icons]", "Theme");
      if (!v.empty()) { cached = v; return cached; }
    }
  }
  cached = "hicolor";
  return cached;
}

static std::vector<std::string> split_csv(std::string s)
{
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ',') {
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
}

static std::optional<std::string> find_index_theme_path(const std::vector<std::string>& bases, const std::string& themeId)
{
  for (const auto& base : bases) {
    const std::string p = base + "/" + themeId + "/index.theme";
    if (file_exists(p)) return p;
    const std::string p2 = base + "/" + themeId + "/icons/index.theme";
    if (file_exists(p2)) return p2;
  }
  return std::nullopt;
}

static std::vector<std::string> read_icon_theme_dirs(const std::vector<std::string>& bases, const std::string& themeId)
{
  const auto idx = find_index_theme_path(bases, themeId);
  if (!idx) return {};

  struct DirMeta {
    int size = 0;
    bool scalable = false;
    bool symbolic = false;
  };

  std::vector<std::string> dirs;
  std::unordered_map<std::string, DirMeta> meta;
  std::vector<std::string> inherits;

  std::ifstream f(*idx);
  if (!f.is_open()) return {};
  std::string line;
  std::string section;
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      section = s.substr(1, s.size() - 2);
      continue;
    }
    const auto eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = s.substr(0, eq);
    const std::string value = s.substr(eq + 1);

    if (section == "Icon Theme") {
      if (key == "Directories" || key == "ScaledDirectories") {
        for (const auto& part : split_csv(value)) {
          if (part.empty()) continue;
          dirs.push_back(part);
          DirMeta m{};
          m.symbolic = part.find("symbolic") != std::string::npos;
          meta[part] = m;
        }
      } else if (key == "Inherits") {
        inherits = split_csv(value);
      }
      continue;
    }

    auto it = meta.find(section);
    if (it == meta.end()) continue;
    if (key == "Size") {
      const int v = std::atoi(value.c_str());
      if (v > 0) it->second.size = v;
    } else if (key == "Type") {
      const std::string t = value;
      it->second.scalable = (t == "Scalable" || t == "Threshold");
    } else if (key == "MaxSize") {
      const int v = std::atoi(value.c_str());
      if (v > it->second.size) it->second.size = v;
    }
  }

  std::stable_sort(dirs.begin(), dirs.end(), [&](const std::string& a, const std::string& b) {
    const auto& da = meta[a];
    const auto& db = meta[b];
    if (da.symbolic != db.symbolic) return da.symbolic < db.symbolic;
    if (da.scalable != db.scalable) return da.scalable > db.scalable;
    return da.size > db.size;
  });

  std::vector<std::string> out;
  out.reserve(dirs.size());
  for (const auto& d : dirs) {
    if (d.empty()) continue;
    if (std::find(out.begin(), out.end(), d) != out.end()) continue;
    out.push_back(d);
  }
  return out;
}

static std::vector<std::string> build_theme_chain(std::string themeId)
{
  themeId = trim(themeId);
  if (themeId.empty()) themeId = detect_system_icon_theme();

  const auto bases = icon_base_dirs();
  std::vector<std::string> chain;
  std::unordered_set<std::string> seen;

  auto push = [&](const std::string& t) {
    const std::string tt = trim(t);
    if (tt.empty()) return;
    if (seen.contains(tt)) return;
    seen.insert(tt);
    chain.push_back(tt);
  };

  push(themeId);
  for (size_t i = 0; i < chain.size(); ++i) {
    const auto idx = find_index_theme_path(bases, chain[i]);
    if (!idx) continue;
    const std::string inh = read_ini_kv(*idx, "[Icon Theme]", "Inherits");
    for (const auto& parent : split_csv(inh)) push(parent);
  }

  push("hicolor");
  return chain;
}

std::vector<ThemeInfo> list_installed_icon_themes()
{
  const auto bases = icon_base_dirs();
  std::unordered_map<std::string, ThemeInfo> byId;

  for (const auto& base : bases) {
    DIR* d = opendir(base.c_str());
    if (!d) continue;
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      const std::string id(ent->d_name);
      if (id.empty() || id == "." || id == "..") continue;
      std::string idx = base + "/" + id + "/index.theme";
      if (!file_exists(idx)) {
        idx = base + "/" + id + "/icons/index.theme";
        if (!file_exists(idx)) continue;
      }
      if (byId.contains(id)) continue;

      {
        std::ifstream ft(idx);
        bool hasIconTheme = false;
        if (ft.is_open()) {
          std::string line;
          while (std::getline(ft, line)) {
            std::string s = trim(line);
            if (s == "[Icon Theme]") { hasIconTheme = true; break; }
          }
        }
        if (!hasIconTheme) continue;
      }

      {
        const std::string dirs = read_ini_kv(idx, "[Icon Theme]", "Directories");
        const std::string sdirs = read_ini_kv(idx, "[Icon Theme]", "ScaledDirectories");
        if (dirs.empty() && sdirs.empty()) continue;
      }

      ThemeInfo info{};
      info.id = id;
      info.name = read_ini_kv(idx, "[Icon Theme]", "Name");
      if (info.name.empty()) info.name = id;
      const std::string hid = read_ini_kv(idx, "[Icon Theme]", "Hidden");
      if (!hid.empty()) {
        const char c = hid[0];
        info.hidden = (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
      }
      {
        const std::string a = base + "/" + id;
        if (file_exists(a + "/index.theme")) info.path = a;
        else if (file_exists(a + "/icons/index.theme")) info.path = a + "/icons";
      }
      byId[id] = info;
    }
    closedir(d);
  }

  std::vector<ThemeInfo> out;
  out.reserve(byId.size());
  for (auto& [_, v] : byId) out.push_back(v);
  std::sort(out.begin(), out.end(), [](const ThemeInfo& a, const ThemeInfo& b) { return a.name < b.name; });
  return out;
}

static std::optional<std::string> read_desktop_desktop_entry_key(const std::string& desktopFilePath, const char* keyPrefix)
{
  if (!keyPrefix || !*keyPrefix) return std::nullopt;
  const size_t prefixLen = std::strlen(keyPrefix);
  std::ifstream f(desktopFilePath);
  if (!f.is_open()) return std::nullopt;
  std::string line;
  std::optional<std::string> value;
  bool inDesktopEntry = false;
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      inDesktopEntry = (s == "[Desktop Entry]");
      continue;
    }
    if (!inDesktopEntry) continue;
    if (s.size() >= prefixLen && s.rfind(keyPrefix, 0) == 0) {
      value = trim(s.substr(prefixLen));
      break;
    }
  }
  if (!value || value->empty()) return std::nullopt;
  return value;
}

static std::optional<std::string> read_desktop_icon_key(const std::string& desktopFilePath)
{
  return read_desktop_desktop_entry_key(desktopFilePath, "Icon=");
}

static void push_unique_desktop_candidate(std::vector<std::string>& v, std::string name)
{
  if (name.size() < 9 || !name.ends_with(".desktop")) return;
  for (const auto& x : v)
    if (x == name) return;
  v.push_back(std::move(name));
}

static std::vector<std::string> desktop_id_candidate_bases(const std::string& appId)
{
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
  const auto last_segment = [](const std::string& s) -> std::string {
    const auto pos = s.find_last_of('.');
    if (pos == std::string::npos) return {};
    if (pos + 1 >= s.size()) return {};
    return s.substr(pos + 1);
  };
  pushBase(last_segment(base));
  pushBase(last_segment(lower));
  auto spaces_to_hyphens = [](std::string s) {
    for (char& c : s)
      if (c == ' ' || c == '\t') c = '-';
    return s;
  };
  pushBase(spaces_to_hyphens(base));
  pushBase(spaces_to_hyphens(lower));
  auto underscores_to_hyphens = [](std::string s) {
    for (char& c : s)
      if (c == '_') c = '-';
    return s;
  };
  pushBase(underscores_to_hyphens(base));
  pushBase(underscores_to_hyphens(lower));
  auto hyphens_to_underscores = [](std::string s) {
    for (char& c : s)
      if (c == '-') c = '_';
    return s;
  };
  pushBase(hyphens_to_underscores(base));
  pushBase(hyphens_to_underscores(lower));
  auto dots_to_hyphens = [](std::string s) {
    for (char& c : s)
      if (c == '.') c = '-';
    return s;
  };
  pushBase(dots_to_hyphens(base));
  pushBase(dots_to_hyphens(lower));

  {
    std::string posix = base;
    for (char& c : posix) {
      if (c == '\\') c = '/';
    }
    const auto slash = posix.find_last_of('/');
    if (slash != std::string::npos && slash + 1 < posix.size()) {
      std::string seg = posix.substr(slash + 1);
      pushBase(seg);
      std::string segLower = seg;
      std::transform(segLower.begin(), segLower.end(), segLower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      pushBase(segLower);
      auto stripExe = [](std::string s) -> std::string {
        std::string low = s;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low.size() > 4 && low.ends_with(".exe")) s.resize(s.size() - 4);
        return s;
      };
      pushBase(stripExe(seg));
      pushBase(stripExe(segLower));
    }
  }
  return out;
}

static std::vector<std::string> desktop_application_dirs()
{
  std::vector<std::string> dirs;
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) dirs.push_back(std::string(xdg) + "/applications");
  if (dirs.empty()) {
    if (const char* home = std::getenv("HOME")) dirs.push_back(std::string(home) + "/.local/share/applications");
  }
  auto dataDirs = split_colon_list(std::getenv("XDG_DATA_DIRS"));
  if (dataDirs.empty()) dataDirs = {"/usr/local/share", "/usr/share"};
  for (const auto& d : dataDirs) dirs.push_back(d + "/applications");
  return dirs;
}

static std::optional<std::string> find_desktop_file_for_appid(const std::string& appId)
{
  const std::vector<std::string> bases = desktop_id_candidate_bases(appId);
  std::vector<std::string> dirs = desktop_application_dirs();

  std::vector<std::string> candidates;
  for (const auto& b : bases) {
    push_unique_desktop_candidate(candidates, b + ".desktop");
  }
  for (const auto& dir : dirs) {
    for (const auto& c : candidates) {
      const std::string path = dir + "/" + c;
      if (file_exists(path)) return path;
    }
  }
  for (const auto& dir : dirs) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (dirent* ent = readdir(d)) {
      std::string name(ent->d_name);
      for (const auto& c : candidates) {
        if (name == c) {
          const std::string path = dir + "/" + name;
          if (file_exists(path)) {
            closedir(d);
            return path;
          }
        }
      }
    }
    closedir(d);
  }
  return std::nullopt;
}

static std::vector<std::string> resolve_theme_roots_for_base(const std::string& base, const std::string& themeId)
{
  std::vector<std::string> roots;
  const std::string a = base + "/" + themeId;
  if (file_exists(a + "/index.theme")) roots.push_back(a);
  if (file_exists(a + "/icons/index.theme")) roots.push_back(a + "/icons");
  return roots;
}

static void append_theme_search_dirs(std::vector<std::string>& out, const std::string& themeRoot, const std::string& themeId,
                                     const std::vector<std::string>& iconBases)
{
  const std::vector<std::string> dirs = read_icon_theme_dirs(iconBases, themeId);
  if (!dirs.empty()) {
    for (const auto& d : dirs) out.push_back(themeRoot + "/" + d + "/");
  }

  for (const char* p : {"/scalable/apps/", "/256x256/apps/", "/128x128/apps/", "/64x64/apps/", "/48x48/apps/", "/32x32/apps/"}) {
    out.push_back(themeRoot + p);
  }

  static const char* kCtx[] = {"apps",       "actions",    "status",   "devices",  "places",   "categories",
                                "emblems",    "preferences","mimetypes","panel",    "legacy",   "emotes",
                                "animations", "logos"};
  static const char* kSizes[] = {"scalable", "symbolic", "512x512", "256x256", "128x128", "96x96", "64x64",
                                "48x48",    "32x32",   "24x24",   "22x22",   "16x16"};

  for (const char* sz : kSizes) {
    for (const char* ctx : kCtx) {
      out.push_back(themeRoot + "/" + std::string(sz) + "/" + std::string(ctx) + "/");
    }
  }
  for (const char* ctx : kCtx) {
    out.push_back(themeRoot + "/" + std::string(ctx) + "/");
  }
}

static std::vector<std::string> icon_aliases(const std::string& name)
{
  static const std::unordered_map<std::string, std::vector<std::string>> kAliases = {
      {"microsoft-edge-dev", {"microsoft-edge", "microsoft-edge-beta", "edge"}},
      {"microsoft-edge", {"microsoft-edge-dev", "edge"}},
      {"msedge", {"microsoft-edge", "microsoft-edge-stable", "com.microsoft.Edge", "edge"}},
      {"steam", {"steam_icon", "steam-icon", "steam_client"}},
      {"co.anysphere.cursor", {"cursor", "cursor-ide"}},
      {"github-desktop", {"github", "github-client"}},
      {"com.github.GitHubDesktop", {"github-desktop"}},
      {"org.gnome.Nautilus", {"system-file-manager", "nautilus"}},
      {"nautilus", {"system-file-manager"}},
      {"system-file-manager", {"nautilus"}},
      {"org.gnome.Ptyxis", {"utilities-terminal", "terminal"}},
      {"Cider", {"cider", "cider-music"}},
      {"otter-term", {"utilities-terminal", "terminal"}},
  };

  auto it = kAliases.find(name);
  if (it == kAliases.end()) return {};
  return it->second;
}

static std::vector<std::string> icon_name_variants(const std::string& iconName)
{
  std::vector<std::string> iconVariants;
  std::unordered_set<std::string> seenVariants;
  auto push_unique = [&](const std::string& s) {
    if (s.empty()) return;
    if (seenVariants.contains(s)) return;
    seenVariants.insert(s);
    iconVariants.push_back(s);
  };

  std::string iconLower = iconName;
  std::transform(iconLower.begin(), iconLower.end(), iconLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  const std::string iconSymbolic = iconName + "-symbolic";
  const std::string iconLowerSymbolic = iconLower + "-symbolic";
  const std::string iconShort =
      iconName.find_last_of('.') != std::string::npos ? iconName.substr(iconName.find_last_of('.') + 1) : std::string{};
  const std::string iconLowerShort =
      iconLower.find_last_of('.') != std::string::npos ? iconLower.substr(iconLower.find_last_of('.') + 1) : std::string{};
  const std::string iconShortSymbolic = iconShort.empty() ? std::string{} : (iconShort + "-symbolic");
  const std::string iconLowerShortSymbolic = iconLowerShort.empty() ? std::string{} : (iconLowerShort + "-symbolic");

  push_unique(iconName);
  push_unique(iconSymbolic);
  push_unique(iconLower);
  push_unique(iconLowerSymbolic);
  push_unique(iconShort);
  push_unique(iconShortSymbolic);
  push_unique(iconLowerShort);
  push_unique(iconLowerShortSymbolic);

  for (const auto& a : icon_aliases(iconName)) {
    push_unique(a);
    push_unique(a + "-symbolic");
  }
  for (const auto& a : icon_aliases(iconLower)) {
    push_unique(a);
    push_unique(a + "-symbolic");
  }

  if (iconLower.rfind("steam_icon_", 0) == 0) {
    push_unique("steam");
    push_unique("steam-symbolic");
    push_unique("steam-client");
    push_unique("steam-client-symbolic");
  }

  if (iconLower.rfind("msedge-", 0) == 0 || iconLower.rfind("microsoft-edge-", 0) == 0) {
    for (const char* b : {"microsoft-edge", "edge", "msedge"}) {
      push_unique(b);
      push_unique(std::string(b) + "-symbolic");
    }
  }
  if (iconLower.rfind("chrome-", 0) == 0 || iconLower.rfind("google-chrome-", 0) == 0) {
    for (const char* b : {"google-chrome", "chrome-browser", "chromium-browser", "chromium"}) {
      push_unique(b);
      push_unique(std::string(b) + "-symbolic");
    }
  }
  if (iconLower == "msedge" || iconLower.starts_with("msedge")) {
    for (const char* b : {"microsoft-edge", "microsoft-edge-stable", "com.microsoft.Edge", "edge"}) {
      push_unique(b);
      push_unique(std::string(b) + "-symbolic");
    }
  }

  auto add_dash_fallbacks = [&](const std::string& base) {
    std::string cur = base;
    for (int i = 0; i < 6; ++i) {
      const auto pos = cur.find_last_of('-');
      if (pos == std::string::npos) break;
      cur = cur.substr(0, pos);
      push_unique(cur);
      push_unique(cur + "-symbolic");
    }
  };
  add_dash_fallbacks(iconName);
  add_dash_fallbacks(iconLower);
  if (!iconShort.empty()) add_dash_fallbacks(iconShort);
  if (!iconLowerShort.empty()) add_dash_fallbacks(iconLowerShort);

  return iconVariants;
}

static std::optional<IconEntry> load_svg_or_png(const std::string& path)
{
  cairo_surface_t* s = nullptr;
  if (path.ends_with(".png")) {
    s = cairo_image_surface_create_from_png(path.c_str());
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
      if (s) cairo_surface_destroy(s);
      return std::nullopt;
    }
    const int sw = cairo_image_surface_get_width(s);
    const int sh = cairo_image_surface_get_height(s);
    if (sw > 0 && sh > 0 && (sw > kMaxIconRasterSidePx || sh > kMaxIconRasterSidePx)) {
      const double sc = static_cast<double>(kMaxIconRasterSidePx) / static_cast<double>(std::max(sw, sh));
      const int dw = std::max(1, static_cast<int>(std::lround(sw * sc)));
      const int dh = std::max(1, static_cast<int>(std::lround(sh * sc)));
      cairo_surface_t* d = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
      if (!d || cairo_surface_status(d) != CAIRO_STATUS_SUCCESS) {
        if (d) cairo_surface_destroy(d);
        cairo_surface_destroy(s);
        return std::nullopt;
      }
      cairo_t* cr = cairo_create(d);
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, s, 0, 0);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      cairo_paint(cr);
      cairo_destroy(cr);
      cairo_surface_destroy(s);
      s = d;
    }
  } else if (path.ends_with(".svg")) {
#ifdef HAVE_LIBRSVG
    GError* err = nullptr;
    RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), &err);
    if (!h) {
      if (err) g_error_free(err);
      return std::nullopt;
    }
    double wpx = 0.0, hpx = 0.0;
    (void)rsvg_handle_get_intrinsic_size_in_pixels(h, &wpx, &hpx);
    int w = std::max(128, static_cast<int>(std::lround(wpx > 0.0 ? wpx : 128.0)));
    int hgt = std::max(128, static_cast<int>(std::lround(hpx > 0.0 ? hpx : 128.0)));
    if (w > kMaxIconRasterSidePx || hgt > kMaxIconRasterSidePx) {
      const double sc = static_cast<double>(kMaxIconRasterSidePx) / static_cast<double>(std::max(w, hgt));
      w = std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * sc)));
      hgt = std::max(1, static_cast<int>(std::lround(static_cast<double>(hgt) * sc)));
    }

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, hgt);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      if (surf) cairo_surface_destroy(surf);
      g_object_unref(h);
      return std::nullopt;
    }

    cairo_t* cr = cairo_create(surf);
    RsvgRectangle viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = static_cast<double>(w);
    viewport.height = static_cast<double>(hgt);
    GError* renderErr = nullptr;
    const gboolean ok = rsvg_handle_render_document(h, cr, &viewport, &renderErr);
    if (!ok) {
      if (renderErr) g_error_free(renderErr);
      cairo_destroy(cr);
      cairo_surface_destroy(surf);
      g_object_unref(h);
      return std::nullopt;
    }
    cairo_destroy(cr);
    g_object_unref(h);
    s = surf;
#else
    return std::nullopt;
#endif
  } else {
    return std::nullopt;
  }
  IconEntry out{};
  out.surface = s;
  out.width = cairo_image_surface_get_width(s);
  out.height = cairo_image_surface_get_height(s);
  return out;
}

IconCache::IconCache() : d_(shared_icon_cache_data()) {}

IconCache::~IconCache() { clear(); }

void IconCache::touch_lru_key(const std::string& key)
{
  auto cit = d_->cache.find(key);
  if (cit == d_->cache.end()) return;

  d_->lru.erase(cit->second.lru_it);
  d_->lru.push_front(key);
  cit->second.lru_it = d_->lru.begin();
}

void IconCache::evict_excess_icon_cache_entries()
{
  while (d_->totalBytes > kIconSurfaceCacheMaxBytes && !d_->lru.empty()) {
    const std::string victim = d_->lru.back();
    d_->lru.pop_back();
    auto vit = d_->cache.find(victim);
    if (vit != d_->cache.end()) {
      if (vit->second.bytes > d_->totalBytes) d_->totalBytes = 0;
      else d_->totalBytes -= vit->second.bytes;
      if (vit->second.surface) cairo_surface_destroy(vit->second.surface);
      d_->cache.erase(vit);
    }
  }
}

void IconCache::clear()
{
  for (auto& [k, v] : d_->cache) {
    (void)k;
    if (v.surface) cairo_surface_destroy(v.surface);
    v.surface = nullptr;
  }
  d_->cache.clear();
  d_->lru.clear();
  d_->totalBytes = 0;
  d_->missLogged.clear();
  d_->execBasenameMiss.clear();
  d_->searchDirs.clear();
  d_->resolvedThemeId.clear();
  d_->searchDirsBuilt = false;
}

void IconCache::set_icon_theme(std::string themeId)
{
  themeId = trim(themeId);
  if (themeId.empty()) themeId = detect_system_icon_theme();
  SC_LOG("IconCache::set_icon_theme: theme=%s", themeId.c_str());
  if (d_->themeOverride == themeId) return;
  d_->themeOverride = themeId;
  s_iconThemeOverride = themeId;
  ++s_iconThemeGeneration;
  clear();
}

void IconCache::rebuild_search_dirs_if_needed()
{
  if (d_->searchDirsBuilt) return;

  std::string theme = trim(d_->themeOverride);
  if (theme.empty()) theme = detect_system_icon_theme();
  const std::vector<std::string> themeChain = build_theme_chain(theme);
  const std::string primaryThemeId = theme.empty() ? detect_system_icon_theme() : theme;
  d_->resolvedThemeId = primaryThemeId;

  const std::vector<std::string> bases = icon_base_dirs();
  const std::vector<std::string> iconBases = icon_base_dirs();

  d_->searchDirs.clear();
  d_->searchDirs.reserve(512);

  auto append_theme = [&](const std::string& themeId) {
    for (const auto& base : bases) {
      if (base.ends_with("/pixmaps")) continue;
      for (const auto& root : resolve_theme_roots_for_base(base, themeId)) {
        append_theme_search_dirs(d_->searchDirs, root, themeId, iconBases);
      }
    }
  };

  if (!primaryThemeId.empty()) append_theme(primaryThemeId);
  append_theme("hicolor");

  d_->searchDirs.push_back("/usr/share/pixmaps/");
  d_->searchDirs.push_back("/usr/local/share/pixmaps/");

  for (const auto& themeId : themeChain) {
    if (themeId == primaryThemeId) continue;
    if (themeId == "hicolor") continue;
    append_theme(themeId);
  }

  {
    std::vector<std::string> uniq;
    uniq.reserve(d_->searchDirs.size());
    std::unordered_set<std::string> seen;
    seen.reserve(d_->searchDirs.size());
    for (auto& d : d_->searchDirs) {
      if (d.empty()) continue;
      if (seen.insert(d).second) uniq.push_back(std::move(d));
    }
    d_->searchDirs = std::move(uniq);
  }

  d_->searchDirsBuilt = true;
  SC_LOG("IconCache: search dirs built, theme=%s n=%zu",
          (d_->themeOverride.empty() ? "auto" : d_->themeOverride).c_str(), d_->searchDirs.size());
}

void IconCache::prewarm_search_dirs()
{
  ensureFresh();
  rebuild_search_dirs_if_needed();
}

bool IconCache::refresh_auto_theme_if_needed()
{
  if (!d_->themeOverride.empty()) return false;

  rebuild_search_dirs_if_needed();
  const std::string cur = detect_system_icon_theme();
  if (!d_->resolvedThemeId.empty() && cur == d_->resolvedThemeId) return false;

  SC_LOG("IconCache: auto theme changed old=%s new=%s", d_->resolvedThemeId.c_str(), cur.c_str());
  s_iconThemeOverride = cur;
  ++s_iconThemeGeneration;
  clear();
  return true;
}

void IconCache::ensureFresh()
{
  if (d_->generation != s_iconThemeGeneration) {
    if (d_->themeOverride != s_iconThemeOverride) {
      d_->themeOverride = s_iconThemeOverride;
      d_->searchDirsBuilt = false;
    }
    clear();
    d_->generation = s_iconThemeGeneration;
  }
}

const IconEntry* IconCache::resolve_and_cache(const std::string& key, const std::string& iconName, bool isTray)
{
  auto it = d_->cache.find(key);
  if (it != d_->cache.end()) {
    if (!it->second.surface) return nullptr;
    touch_lru_key(key);
    return &it->second;
  }

  std::string resolvedName = iconName;
  if (!isTray) {
    std::optional<std::string> desktop = find_desktop_file_for_appid(iconName);
    if (desktop) {
      if (auto iconKey = read_desktop_icon_key(*desktop)) resolvedName = *iconKey;
    }
  }

  rebuild_search_dirs_if_needed();

  if (!resolvedName.empty() && resolvedName[0] == '/') {
    if (file_exists(resolvedName)) {
      auto entry = load_svg_or_png(resolvedName);
      if (!entry || !entry->surface) return nullptr;
      d_->cache[key] = *entry;
      d_->lru.push_front(key);
      d_->cache[key].lru_it = d_->lru.begin();
      evict_excess_icon_cache_entries();
      return &d_->cache[key];
    }
    return nullptr;
  }

  auto strict_variants = [&](const std::string& n) -> std::vector<std::string> {
    std::vector<std::string> v;
    auto pushu = [&](const std::string& s) {
      if (s.empty()) return;
      if (std::find(v.begin(), v.end(), s) != v.end()) return;
      v.push_back(s);
    };
    std::string lower = n;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    pushu(n);
    pushu(n + "-symbolic");
    if (lower != n) {
      pushu(lower);
      pushu(lower + "-symbolic");
    }
    for (const auto& a : icon_aliases(n)) {
      pushu(a);
      pushu(a + "-symbolic");
    }
    for (const auto& a : icon_aliases(lower)) {
      pushu(a);
      pushu(a + "-symbolic");
    }
    return v;
  };

  const std::vector<std::string> variants_strict = strict_variants(resolvedName);
  const std::vector<std::string> variants_full = icon_name_variants(resolvedName);
  std::optional<std::string> path;
  std::string resolvedLower = resolvedName;
  std::transform(resolvedLower.begin(), resolvedLower.end(), resolvedLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool isSteamGameIcon = (resolvedLower.rfind("steam_icon_", 0) == 0);
  auto scan = [&](const std::vector<std::string>& variants) -> bool {
    for (const char* ext : {".svg", ".png"}) {
      for (const auto& dir : d_->searchDirs) {
        for (const auto& name : variants) {
          const std::string p = dir + name + ext;
          if (file_exists(p)) {
            path = p;
            return true;
          }
        }
      }
    }
    return false;
  };

  if (!scan(variants_strict)) {
    if (!isSteamGameIcon) (void)scan(variants_full);
  }
  if (!path) {
    std::vector<std::string> bases;
    bases.reserve(8);
    if (const char* home = std::getenv("HOME")) {
      bases.push_back(std::string(home) + "/.local/share/icons/hicolor/");
      bases.push_back(std::string(home) + "/.icons/hicolor/");
    }
    bases.push_back("/usr/share/icons/hicolor/");
    bases.push_back("/usr/local/share/icons/hicolor/");
    bases.push_back("/usr/share/icons/");
    bases.push_back("/usr/local/share/icons/");
    bases.push_back("/var/lib/flatpak/exports/share/icons/hicolor/");
    bases.push_back("/run/flatpak/exports/share/icons/hicolor/");
    if (const char* home = std::getenv("HOME")) bases.push_back(std::string(home) + "/.local/share/flatpak/exports/share/icons/hicolor/");
    bases.push_back("/usr/share/pixmaps/");
    bases.push_back("/usr/local/share/pixmaps/");

    auto find_in_bases = [&]() -> bool {
      for (const auto& base : bases) {
        for (const auto& name : variants_full) {
          if (base.ends_with("/pixmaps/")) {
            for (const char* ext : {".svg", ".png"}) {
              const std::string p = base + name + ext;
              if (file_exists(p)) { path = p; return true; }
            }
            continue;
          }
          if (base.ends_with("/icons/")) {
            for (const char* ext : {".svg", ".png"}) {
              const std::string p = base + name + ext;
              if (file_exists(p)) { path = p; return true; }
            }
            continue;
          }
          for (const char* sc : {"scalable/apps/", "symbolic/apps/", "512x512/apps/", "256x256/apps/", "128x128/apps/",
                                 "64x64/apps/", "48x48/apps/", "32x32/apps/", "24x24/apps/", "16x16/apps/", "apps/"}) {
            for (const char* ext : {".svg", ".png"}) {
              const std::string p = base + sc + name + ext;
              if (file_exists(p)) { path = p; return true; }
            }
          }
        }
      }
      return false;
    };
    (void)find_in_bases();
  }
  if (!path) {
    auto find_fallback = [&]() -> bool {
      for (const char* fb : {"application-x-executable", "application-default-icon", "system-run", "dialog-question"}) {
        for (const auto& dir : d_->searchDirs) {
          for (const char* ext : {".svg", ".png"}) {
            const std::string p = dir + std::string(fb) + ext;
            if (file_exists(p)) { path = p; return true; }
          }
        }
      }
      return false;
    };
    (void)find_fallback();
  }
  if (!path && isSteamGameIcon) {
    (void)scan(variants_full);
  }
  if (!path) {
    for (const char* fb : {"application-x-executable", "application-default-icon", "system-run", "dialog-question"}) {
      for (const auto& dir : d_->searchDirs) {
        for (const char* ext : {".svg", ".png"}) {
          const std::string p = dir + std::string(fb) + ext;
          if (file_exists(p)) {
            path = p;
            break;
          }
        }
        if (path) break;
      }
      if (path) break;
    }
  }
  if (!path) {
    if (!d_->missLogged.contains(key)) {
      d_->missLogged.insert(key);
      if (d_->missLogged.size() > kMaxIconMissLogged) d_->missLogged.clear();
      SC_LOG("IconCache: miss key=%s name=%s theme=%s",
              key.c_str(), resolvedName.c_str(),
              (d_->themeOverride.empty() ? "auto" : d_->themeOverride).c_str());
    }
    return nullptr;
  }
  auto entry = load_svg_or_png(*path);
  if (!entry || !entry->surface) return nullptr;
  std::size_t sz = static_cast<std::size_t>(cairo_image_surface_get_stride(entry->surface)) *
                   static_cast<std::size_t>(cairo_image_surface_get_height(entry->surface));
  {
    auto [eit, inserted] = d_->cache.try_emplace(key);
    if (!inserted) {
      if (eit->second.surface) cairo_surface_destroy(eit->second.surface);
      if (eit->second.bytes > 0) {
        if (eit->second.bytes > d_->totalBytes) d_->totalBytes = 0;
        else d_->totalBytes -= eit->second.bytes;
      }
      d_->lru.erase(eit->second.lru_it);
    }
    eit->second = *entry;
    eit->second.bytes = sz;
    d_->totalBytes += sz;
    d_->lru.push_front(key);
    eit->second.lru_it = d_->lru.begin();
    evict_excess_icon_cache_entries();
    return &eit->second;
  }
}

const IconEntry* IconCache::app_icon(const std::string& appId)
{
  ensureFresh();
  d_->keyBuf.clear();
  d_->keyBuf.append("app:");
  d_->keyBuf.append(appId);
  return resolve_and_cache(d_->keyBuf, appId, false);
}

const IconEntry* IconCache::tray_icon(const std::string& iconName)
{
  ensureFresh();
  d_->keyBuf.clear();
  d_->keyBuf.append("tray:");
  d_->keyBuf.append(iconName);
  return resolve_and_cache(d_->keyBuf, iconName, true);
}

}
