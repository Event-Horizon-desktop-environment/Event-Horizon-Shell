#define _GNU_SOURCE 1
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/bundled_assets.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "desktop_shell/common/log/debug_log.hpp"

namespace fs = std::filesystem;

extern "C" char** environ;

#if EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

namespace eh::icons {

using eh::theming::detail::update_ini_key;

namespace {
// Byte budget for cached icon rasters. Most entries land at 128px (64 KiB)
// or the 256px default bucket (256 KiB); the kHiResIconPx/384px bucket (576
// KiB) is only used by callers that explicitly request a HiDPI-sized icon
// (see icon_cache.hpp). 96 MiB comfortably covers a full app-grid plus
// dock/tray/taskbar even with some entries at the top bucket, without LRU
// thrash. The old 2 MiB budget held ~32 icons and caused constant
// re-rasterization (multi-hundred-ms frames) whenever more apps were
// visible than fit.
std::size_t icon_surface_cache_max_bytes() {
  static const std::size_t v = []() -> std::size_t {
    if (const char* s = std::getenv("EH_ICON_CACHE_MB")) {
      char* end = nullptr;
      const long mb = std::strtol(s, &end, 10);
      if (end && *end == '\0' && mb >= 2 && mb <= 1024)
        return static_cast<std::size_t>(mb) << 20;
    }
    return std::size_t{96} << 20;
  }();
  return v;
}
// Top raster bucket, used by bucket_for() for requests above 192px (see
// kHiResIconPx in icon_cache.hpp). This used to be declared but never
// actually applied anywhere -- bucket_for() capped every request at 256px
// regardless, which is what caused icons to look upscaled/blurry once a
// caller's on-screen size (after HiDPI buffer_scale and dock/taskbar
// "scale" settings) exceeded 256 physical px.
constexpr int kMaxIconRasterSidePx = 384;

std::shared_ptr<IconCacheData>& shared_icon_cache_data() {
   
  static auto data = std::make_shared<IconCacheData>();
  return data;
}
}

static std::atomic<std::uint64_t> s_iconThemeGeneration{0};
static std::string s_iconThemeOverride;

std::uint64_t IconCache::icon_theme_generation() const {
  return s_iconThemeGeneration.load(std::memory_order_relaxed);
}

namespace {

// Windowed perf counters for resolve_and_cache, reported through
// eh_icons_perf_log_reset().
struct IconPerf {
  std::mutex mtx;
  uint64_t hits = 0, misses = 0, missesSlow = 0;
  double resolveUs = 0.0, loadUs = 0.0;
  double maxResolveUs = 0.0, maxLoadUs = 0.0;
  std::string worstKey;

  void addResolve(double us) {
    resolveUs += us;
    if (us > maxResolveUs) maxResolveUs = us;
  }
  void addLoad(double us) {
    loadUs += us;
    if (us > maxLoadUs) maxLoadUs = us;
  }
};

IconPerf& icon_perf() {
  static IconPerf p;
  return p;
}

double mono_ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

// Shared-data teardown: signal the worker and detach it. The worker lambda
// holds its own shared_ptr copy of this data, so detaching can never leave
// the thread using freed memory, and an unjoined std::thread can never hit
// std::terminate during static destruction.
IconCacheData::~IconCacheData() {
  quit.store(true);
  cv.notify_all();
  if (worker && worker->joinable()) {
    std::thread to_detach = std::move(*worker);
    worker.reset();
    to_detach.detach();
  }
}

void eh_icons_perf_log_reset(const char* ctx) {
  IconPerf& p = icon_perf();
  std::lock_guard<std::mutex> lk(p.mtx);
  debug_log("icons",
            "perf[%s]: hits=%llu misses=%llu slow(>3ms)=%llu "
            "resolve avg=%.2fms max=%.2fms | load avg=%.2fms max=%.2fms worst=%s",
            ctx,
            static_cast<unsigned long long>(p.hits),
            static_cast<unsigned long long>(p.misses),
            static_cast<unsigned long long>(p.missesSlow),
            p.misses > 0 ? p.resolveUs / 1000.0 / static_cast<double>(p.misses) : 0.0,
            p.maxResolveUs / 1000.0,
            p.misses > 0 ? p.loadUs / 1000.0 / static_cast<double>(p.misses) : 0.0,
            p.maxLoadUs / 1000.0,
            p.worstKey.c_str());
  p.hits = p.misses = p.missesSlow = 0;
  p.resolveUs = p.loadUs = 0.0;
  p.maxResolveUs = p.maxLoadUs = 0.0;
  p.worstKey.clear();
}

static bool file_exists(const std::string& path) { return access(path.c_str(), R_OK) == 0; }

static bool eh_icon_debug() {
   
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_ICON_DEBUG") ? 1 : 0;
  return cached != 0;
}

static std::string join_csv(const std::vector<std::string>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) out += ", ";
    out += v[i];
  }
  return out;
}
// Kept referenced for ad-hoc theme-chain diagnostics; silences -Wunused.
[[maybe_unused]] static const auto s_join_csv_keepalive = &join_csv;

static std::vector<std::string> split_colon_list(const char* s) {
   
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

static std::string trim(std::string s) {
   
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  if (i) s.erase(0, i);
  return s;
}

static std::string trim_quotes(std::string s) {
   
  s = trim(std::move(s));
  if (s.size() >= 2) {
    if ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"')) {
      s = s.substr(1, s.size() - 2);
    }
  }
  return trim(std::move(s));
}

static std::string run_cmd_capture(const std::string& cmd) {
   
  std::array<int, 2> fd{};
  if (pipe(fd.data()) < 0) return {};

  std::vector<char> arg_c(cmd.begin(), cmd.end());
  arg_c.push_back('\0');

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (fd[0] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[0]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (fd[1] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[1]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return {};
  }
  if (posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
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

static std::string read_ini_kv(const std::string& path, const std::string& wantSection, const std::string& wantKey) {
   
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

static std::vector<std::string> icon_base_dirs() {
   
  std::vector<std::string> bases;
  auto push_unique = [&](const std::string& s) {
    if (s.empty()) return;
    if (std::find(bases.begin(), bases.end(), s) != bases.end()) return;
    bases.push_back(s);
  };

  // Test hook: hermetic fixture theme directory (see test/test_icon_cache.cpp).
  if (const char* extra = std::getenv("EH_ICON_EXTRA_DIR")) push_unique(extra);

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

std::string detect_system_icon_theme() {
   
  if (const char* e = std::getenv("EH_ICON_THEME")) return std::string(e);

  static std::string cached;
  static auto lastCheck = std::chrono::steady_clock::time_point{};
  constexpr auto kCacheDuration = std::chrono::seconds(5);
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

static std::vector<std::string> split_csv(std::string s) {
   
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

static std::optional<std::string> find_index_theme_path(const std::vector<std::string>& bases, const std::string& themeId) {
   
  for (const auto& base : bases) {
    const std::string p = base + "/" + themeId + "/index.theme";
    if (file_exists(p)) return p;
    const std::string p2 = base + "/" + themeId + "/icons/index.theme";
    if (file_exists(p2)) return p2;
  }
  return std::nullopt;
}

static std::vector<std::string> read_icon_theme_dirs(const std::vector<std::string>& bases, const std::string& themeId) {
   
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

static std::vector<std::string> build_theme_chain(std::string themeId) {
   
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

std::vector<ThemeInfo> list_installed_icon_themes() {
   
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

cairo_surface_t* IconCache::load_settings_logo_surface() {
   
  static cairo_surface_t* cached = nullptr;
  static bool tried = false;
  if (tried) return cached;
  tried = true;
  cached = eh::shell::create_material_glyph_surface("settings", 48);
  return cached;
}

static std::optional<std::string> read_desktop_desktop_entry_key(const std::string& desktopFilePath, const char* keyPrefix) {
   
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

static std::optional<std::string> read_desktop_icon_key(const std::string& desktopFilePath) {
  return read_desktop_desktop_entry_key(desktopFilePath, "Icon=");
}

static void push_unique_desktop_candidate(std::vector<std::string>& v, std::string name) {
   
  if (name.size() < 9 || !name.ends_with(".desktop")) return;
  for (const auto& x : v)
    if (x == name) return;
  v.push_back(std::move(name));
}

static std::vector<std::string> desktop_id_candidate_bases(const std::string& appId) {
   
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

static std::string path_basename(std::string p);
static std::vector<std::string> desktop_application_dirs();

static std::string wmclass_lookup_key(std::string s) {
   
  s = trim(std::move(s));
  for (char& c : s) {
    if (c == '\\') c = '/';
  }
  const auto slash = s.find_last_of('/');
  if (slash != std::string::npos && slash + 1 < s.size()) s = s.substr(slash + 1);
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (s.size() > 4 && s.ends_with(".exe")) s.resize(s.size() - 4);
  return s;
}

static std::unordered_map<std::string, std::string> g_wmClassToDesktop;
static bool g_wmClassIndexBuilt = false;

static void reset_wmclass_desktop_index() {
   
  g_wmClassToDesktop.clear();
  g_wmClassIndexBuilt = false;
}

static void ensure_wmclass_desktop_index_built() {
   
  if (g_wmClassIndexBuilt) return;
  g_wmClassIndexBuilt = true;

  auto index_wm_labels = [](std::string label, const std::string& desktopPath) {
    label = trim(std::move(label));
    if (label.empty()) return;
    const std::string k = wmclass_lookup_key(label);
    if (!k.empty() && g_wmClassToDesktop.find(k) == g_wmClassToDesktop.end()) g_wmClassToDesktop.emplace(k, desktopPath);
    std::string low = label;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!low.empty() && low != k && g_wmClassToDesktop.find(low) == g_wmClassToDesktop.end()) g_wmClassToDesktop.emplace(low, desktopPath);
  };

  for (const auto& dir : desktop_application_dirs()) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (dirent* ent = readdir(d)) {
      std::string name(ent->d_name);
      if (name.size() < 9 || !name.ends_with(".desktop")) continue;
      const std::string path = dir + "/" + name;
      auto wm = read_desktop_desktop_entry_key(path, "StartupWMClass=");
      if (!wm || wm->empty()) continue;
      index_wm_labels(*wm, path);
    }
    closedir(d);
  }
}

static std::optional<std::string> find_desktop_file_by_wmclass(const std::string& appId) {
   
  ensure_wmclass_desktop_index_built();
  const std::string k1 = wmclass_lookup_key(appId);
  if (!k1.empty()) {
    if (auto it = g_wmClassToDesktop.find(k1); it != g_wmClassToDesktop.end()) return it->second;
  }
  const std::string k2 = wmclass_lookup_key(path_basename(appId));
  if (!k2.empty() && k2 != k1) {
    if (auto it = g_wmClassToDesktop.find(k2); it != g_wmClassToDesktop.end()) return it->second;
  }
  return std::nullopt;
}

static std::optional<std::string> find_desktop_file_for_appid(const std::string& appId) {
   
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

static std::string path_basename(std::string p) {
   
  while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
  const auto slash = p.find_last_of("/\\");
  if (slash != std::string::npos && slash + 1 < p.size()) return p.substr(slash + 1);
  return p;
}

static std::vector<std::string> desktop_application_dirs() {
   
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

static bool desktop_truthy_value(std::string v) {
   
  v = trim(std::move(v));
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return v == "true" || v == "1" || v == "yes";
}

static std::string strip_exec_field_codes(std::string s) {
   
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

struct ParsedDesktopEntry {
  std::string exec;
  std::string icon;
  std::string type;
  bool has_type = false;
  bool hidden = false;
  bool nodisplay = false;
};

static std::optional<ParsedDesktopEntry> read_desktop_entry_fields(const std::string& desktopFilePath) {
   
  std::ifstream f(desktopFilePath);
  if (!f.is_open()) return std::nullopt;
  std::string line;
  ParsedDesktopEntry out;
  bool inDesktopEntry = false;
  while (std::getline(f, line)) {
    std::string s = trim(line);
    if (s.empty() || s[0] == '#') continue;
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
      inDesktopEntry = (s == "[Desktop Entry]");
      continue;
    }
    if (!inDesktopEntry) continue;
    if (s.rfind("Exec=", 0) == 0) {
      if (out.exec.empty()) out.exec = trim(s.substr(5));
    } else if (s.rfind("Icon=", 0) == 0) {
      if (out.icon.empty()) out.icon = trim(s.substr(5));
    } else if (s.rfind("Type=", 0) == 0) {
      out.type = trim(s.substr(5));
      out.has_type = true;
    } else if (s.rfind("Hidden=", 0) == 0) {
      out.hidden = desktop_truthy_value(s.substr(7));
    } else if (s.rfind("NoDisplay=", 0) == 0) {
      out.nodisplay = desktop_truthy_value(s.substr(10));
    }
  }
  return std::optional<ParsedDesktopEntry>(std::move(out));
}

static std::string exec_line_first_binary(std::string exec_line) {
   
  exec_line = strip_exec_field_codes(trim(std::move(exec_line)));
  if (exec_line.rfind("Exec=", 0) == 0) exec_line = strip_exec_field_codes(trim(exec_line.substr(5)));

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

    const std::string bn = path_basename(t);
    std::string lower = bn;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "flatpak" || lower == "snap") return {};

    return t;
  }
  return {};
}

static std::optional<std::string> scan_desktop_icon_for_exec_basename(const std::string& wantLower) {
   
  for (const auto& dir : desktop_application_dirs()) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (dirent* ent = readdir(d)) {
      std::string name(ent->d_name);
      if (name.size() < 9 || !name.ends_with(".desktop")) continue;
      const std::string path = dir + "/" + name;
      auto fields = read_desktop_entry_fields(path);
      if (!fields) continue;
      if (fields->hidden || fields->nodisplay) continue;
      if (fields->has_type) {
        std::string tl = fields->type;
        std::transform(tl.begin(), tl.end(), tl.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tl != "application") continue;
      }
      if (fields->exec.empty() || fields->icon.empty()) continue;

      std::string binPath = exec_line_first_binary(fields->exec);
      if (binPath.empty()) continue;
      std::string base = path_basename(binPath);
      std::transform(base.begin(), base.end(), base.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (base != wantLower) continue;
      return fields->icon;
    }
    closedir(d);
  }
  return std::nullopt;
}

static std::vector<std::string> resolve_theme_roots_for_base(const std::string& base, const std::string& themeId) {
   
  // Returns one or more possible roots that contain an index.theme and icon subdirs.
  // Supports:
  //   <base>/<theme>/index.theme
  //   <base>/<theme>/icons/index.theme   (theme bundle)
  std::vector<std::string> roots;
  const std::string a = base + "/" + themeId;
  if (file_exists(a + "/index.theme")) roots.push_back(a);
  if (file_exists(a + "/icons/index.theme")) roots.push_back(a + "/icons");
  return roots;
}

static void append_theme_search_dirs(std::vector<std::string>& out, const std::string& themeRoot, const std::string& themeId,
                                    const std::vector<std::string>& iconBases) {
   
  const std::vector<std::string> dirs = read_icon_theme_dirs(iconBases, themeId);
  if (!dirs.empty()) {
    for (const auto& d : dirs) out.push_back(themeRoot + "/" + d + "/");
    // Don't return: many third-party themes have incomplete/misleading index.theme.

  }

  for (const char* p : {"/scalable/apps/", "/256x256/apps/", "/128x128/apps/", "/64x64/apps/", "/48x48/apps/", "/32x32/apps/"}) {
    out.push_back(themeRoot + p);
  }

  static const char* kCtx[] = {"apps",       "actions",    "status",   "devices",  "places",   "categories",
                               "emblems",    "preferences","mimetypes","mimes",    "panel",    "legacy",
                               "emotes",     "animations", "logos"};
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

static std::vector<std::string> icon_aliases(const std::string& name) {
   
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

static std::vector<std::string> icon_name_variants(const std::string& iconName) {
   
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

  // Mime-family fallback chains for names themes commonly omit: specific
  // siblings first, then family generics so every known file type degrades
  // to a sensible icon instead of the drawn placeholder.
  {
    auto add = [&](const std::string& s) { push_unique(s); };
    auto starts = [&](const char* p) { return iconLower.rfind(p, 0) == 0; };
    auto contains = [&](std::initializer_list<const char*> keys) {
      for (auto k : keys)
        if (iconLower.find(k) != std::string::npos) return true;
      return false;
    };

    if (iconLower == "text-x-c++hdr" || iconLower == "text-x-c++-header") {
      add("text-x-chdr"); add("text-x-c++src"); add("text-x-c++");
    } else if (iconLower == "text-x-chdr" || iconLower == "text-x-c-header") {
      add("text-x-csrc"); add("text-x-c");
    } else if (iconLower == "text-x-objchdr") {
      add("text-x-chdr");
    } else if (iconLower == "text-x-objective-c") {
      add("text-x-csrc");
    } else if (iconLower == "text-x-objective-c++") {
      add("text-x-c++");
    } else if (iconLower == "application-x-shellscript" || iconLower == "application-x-sh") {
      add("text-x-script");
    }

    if (starts("image/") || starts("image-")) {
      add("image-x-generic");
    } else if (starts("audio/") || starts("audio-")) {
      add("audio-x-generic");
    } else if (starts("video/") || starts("video-")) {
      add("video-x-generic");
    } else if (starts("font/") || starts("font-") || starts("application/x-font") ||
               starts("application/font")) {
      add("font-x-generic");
    } else if (starts("application/") || starts("application-")) {
      if (contains({"zip", "tar", "gzip", "bzip", "compress", "archive", "7z",
                    "rar", "xz", "cab", "cpio", "iso", "cd-image", "disk-image",
                    "package", "deb", "rpm", "flatpak", "snap", "appimage",
                    "zoo", "stuffit", "binhex"})) {
        add("application-zip");
        add("package-x-generic");
      } else if (contains({"officedocument", "opendocument", "msword",
                           "ms-powerpoint", "ms-excel", "ms-publisher",
                           "presentation", "spreadsheet", "wordprocessing",
                           "iwork", "indesign"})) {
        add("x-office-document");
      } else {
        add("text-x-generic");
      }
    } else if (starts("text-") || starts("text/")) {
      add("text-x-source");
      add("text-x-script");
      add("text-x-generic");
    }
  }

  return iconVariants;
}

// Render-size buckets: bounded set of raster sizes so a handful of cache
// entries covers every display size while cutting SVG raster cost versus
// always rendering large.
static int bucket_for(int px) {
  if (px <= 24) return 32;
  if (px <= 48) return 64;
  if (px <= 96) return 128;
  if (px <= 192) return 256;
  return kMaxIconRasterSidePx;  // 384 — HiDPI outputs / large icon-size settings
}

static cairo_surface_t* load_png(const std::string& path, int max_px) {
  cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
  if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    if (s) cairo_surface_destroy(s);
    return nullptr;
  }
  const int sw = cairo_image_surface_get_width(s);
  const int sh = cairo_image_surface_get_height(s);
  // Never upscale rasters — downscale to the bucket only when the source is
  // larger, so cache memory stays bounded.
  if (sw > 0 && sh > 0 && (sw > max_px || sh > max_px)) {
    const double sc = static_cast<double>(max_px) / static_cast<double>(std::max(sw, sh));
    const int dw = std::max(1, static_cast<int>(std::lround(sw * sc)));
    const int dh = std::max(1, static_cast<int>(std::lround(sh * sc)));
    cairo_surface_t* d = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
    if (!d || cairo_surface_status(d) != CAIRO_STATUS_SUCCESS) {
      if (d) cairo_surface_destroy(d);
      cairo_surface_destroy(s);
      return nullptr;
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
  return s;
}

#if EH_HAVE_RSVG
static cairo_surface_t* load_svg(const std::string& path, int size) {
  GError* err = nullptr;
  RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), &err);
  if (!h) {
    if (err) g_error_free(err);
    return nullptr;
  }
  double wpx = 0.0, hpx = 0.0;
  (void)rsvg_handle_get_intrinsic_size_in_pixels(h, &wpx, &hpx);
  double aspect = (hpx > 0.0) ? (wpx / hpx) : 1.0;
  if (!(aspect > 0.01) || !(aspect < 100.0)) aspect = 1.0;
  int w = size, hgt = size;
  if (aspect >= 1.0) hgt = std::max(1, static_cast<int>(std::lround(size / aspect)));
  else w = std::max(1, static_cast<int>(std::lround(size * aspect)));

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, hgt);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    g_object_unref(h);
    return nullptr;
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
    return nullptr;
  }
  cairo_destroy(cr);
  g_object_unref(h);
  return surf;
}
#else
static cairo_surface_t* load_svg(const std::string& /*path*/, int /*size*/) { return nullptr; }
#endif

// Theme directory index.
//
// One readdir pass per theme builds an in-memory name -> candidates map.
// After that every icon lookup is pure hash probing — zero filesystem
// syscalls on the draw path. The scan walks the whole theme tree to depth
// 2 so every known layout works: scalable/<category>, <category>/scalable,
// <size>x<size>/<category>, mimes/16, ...

// Nominal size of a themed icon directory. Two layout conventions exist:
//   Convention A: <size>x<size>/<category>   e.g. hicolor/48x48/apps
//   Convention B: <category>/<size>          e.g. <theme>/places/16
// Any component shaped "NNNxNNN" or a bare number "NNN" sets the size;
// anything else (scalable/, symbolic/, plain categories) means
// resolution independent.
static unsigned short icon_dir_size(const fs::path& dir) {
  auto parse_num = [](const std::string& s) -> int {
    if (s.empty()) return -1;
    int v = 0;
    for (char c : s) {
      if (c < '0' || c > '9') return -1;
      v = v * 10 + (c - '0');
    }
    return v;
  };
  std::vector<std::string> parts;
  for (const auto& part : dir) {
    auto s = part.string();
    if (!s.empty() && s != "/") parts.push_back(std::move(s));
  }
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    const std::string& leaf = *it;
    auto sep = leaf.find('x');
    if (sep != std::string::npos) {
      if (sep == 0 || sep + 1 >= leaf.size()) continue; // "x…", "…x", "@2x"
      int a = parse_num(leaf.substr(0, sep));
      if (a < 0) continue; // not "NNNxNNN" (e.g. "apps@2x")
      int b = parse_num(leaf.substr(sep + 1));
      if (b >= 0 && a > 0 && a <= 4096) return static_cast<unsigned short>(a);
      continue;
    }
    int v = parse_num(leaf); // bare number: "16", "22", …
    if (v > 0 && v <= 4096) return static_cast<unsigned short>(v);
  }
  return 0;
}

// Index every svg/png/xpm directly inside one theme subdirectory.
// `dsize` is the directory's nominal pixel size (0 = resolution-independent);
// computed by the caller from index.theme metadata or dirname inference.
static void scan_index_subdir(
    const fs::path& dir,
    unsigned short dsize,
    std::unordered_map<std::string, std::vector<IconCacheData::IconCandidate>>& idx) {
  DIR* dp = opendir(dir.c_str());
  if (!dp) return;
  while (struct dirent* de = readdir(dp)) {
    std::string_view fn(de->d_name);
    if (fn.empty() || fn[0] == '.') continue;
    auto dot = fn.rfind('.');
    if (dot == std::string_view::npos) continue;
    std::string_view ext = fn.substr(dot + 1);
    if (ext != "svg" && ext != "png" && ext != "xpm") continue;
    std::string stem(fn.substr(0, dot));
    IconCacheData::IconCandidate c;
    c.path = dir.string() + "/" + std::string(fn);
    c.dirSize = dsize;
    c.svg = (ext == "svg");
    auto it = idx.find(stem);
    if (it == idx.end()) {
      idx.emplace(std::move(stem),
                  std::vector<IconCacheData::IconCandidate>{std::move(c)});
    } else {
      // Same stem in the same dir with a second extension (foo.svg + foo.png).
      auto& vec = it->second;
      bool dup = false;
      for (const auto& e : vec)
        if (e.path == c.path) { dup = true; break; }
      if (!dup) vec.push_back(std::move(c));
    }
  }
  closedir(dp);
}

// Authoritative per-subdirectory sizes from a theme's index.theme:
// each Directories= entry has its own section ([16], [scalable], [dark/16]…)
// declaring Size=/MinSize=/MaxSize=. Falls back to dirname inference when a
// section is missing, so partially-specified themes still rank sanely.
static std::unordered_map<std::string, unsigned short> theme_index_dir_sizes(const fs::path& root) {
  std::unordered_map<std::string, unsigned short> out;
  std::ifstream f(root / "index.theme");
  if (!f.is_open()) return out;
  std::string line, section;
  while (std::getline(f, line)) {
    const std::string s = trim(line);
    if (s.empty() || s[0] == '#' || s.starts_with(';')) continue;
    if (s.front() == '[' && s.back() == ']') {
      section = s.substr(1, s.size() - 2);
      continue;
    }
    if (section.empty() || section == "Icon Theme") continue;
    const auto eq = s.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = trim(s.substr(0, eq));
    if (key != "Size" && key != "MinSize" && key != "MaxSize") continue;
    if (out.contains(section)) continue; // first hit wins: Size > MinSize > MaxSize
    int v = 0;
    for (char c : trim(s.substr(eq + 1))) {
      if (c < '0' || c > '9') { v = -1; break; }
      v = v * 10 + (c - '0');
    }
    if (v > 0 && v <= 4096) out.emplace(section, static_cast<unsigned short>(v));
  }
  return out;
}

// Walk an entire theme tree and index every icon file at ANY depth
// (variant roots like "dark/", context/size mixes, symbolic dirs…),
// bounded to guard against pathological or self-referencing layouts.
static void scan_theme_tree(
    const fs::path& root,
    std::unordered_map<std::string, std::vector<IconCacheData::IconCandidate>>& idx) {
  struct Node {
    fs::path dir;
    std::string rel; // path relative to the theme root ("", "16", "dark/16"…)
    int depth;
  };
  constexpr int kMaxDepth = 6;       // root + 5 nested levels: covers every real layout
  constexpr std::size_t kMaxDirs = 8192;

  const auto sizes = theme_index_dir_sizes(root);
  std::vector<Node> stack;
  stack.push_back({root, "", 0});
  std::size_t visited = 0;
  while (!stack.empty()) {
    const Node node = stack.back();
    stack.pop_back();
    if (++visited > kMaxDirs) break;

    unsigned short dsize = 0;
    if (!node.rel.empty()) {
      if (auto it = sizes.find(node.rel); it != sizes.end()) dsize = it->second;
      if (dsize == 0) dsize = icon_dir_size(node.dir);
    }
    scan_index_subdir(node.dir, dsize, idx);

    if (node.depth >= kMaxDepth) continue;
    std::error_code ec;
    fs::directory_iterator it(node.dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      std::error_code ec2;
      if (!it->is_directory(ec2) || ec2) { ec.clear(); continue; }
      const std::string name = it->path().filename().string();
      if (!name.empty() && name[0] == '.') continue;
      stack.push_back({it->path(), node.rel.empty() ? name : node.rel + "/" + name, node.depth + 1});
    }
  }
}

// Keys that fully failed resolution are remembered under an extension- and
// size-independent form so every pixel size reuses the O(1) rejection.
static std::string negative_key_of(const std::string& key) {
  if (key.starts_with("tray:") || key.starts_with("app:") ||
      key.starts_with("exec:")) {
    auto at = key.rfind('@');
    if (at != std::string::npos) return key.substr(0, at);
  }
  return key;
}

IconCache::IconCache() : d_(shared_icon_cache_data()) {}

IconCache::~IconCache() = default;
// NOTE: instances share one IconCacheData (shared_icon_cache_data), so the
// destructor must not clear() or stop the worker — the data outlives every
// instance and the worker is torn down by ~IconCacheData.

// Locked-domain helpers (callers must hold IconCacheData::mtx).

static void touch_lru_locked(IconCacheData& d, const std::string& key) {
  auto it = d.cache.find(key);
  if (it == d.cache.end()) return;
  auto& entry = it->second;
  d.lru.erase(entry.lru_it);
  d.lru.push_front(key);
  entry.lru_it = d.lru.begin();
}

static void evict_excess_locked(IconCacheData& d) {
  while (d.totalBytes > icon_surface_cache_max_bytes() && !d.lru.empty()) {
    auto key = d.lru.back();
    d.lru.pop_back();
    auto it = d.cache.find(key);
    if (it != d.cache.end()) {
      if (it->second.surface) cairo_surface_destroy(it->second.surface);
      d.totalBytes -= it->second.bytes;
      d.cache.erase(it);
    }
  }
}

static void iconcache_rebuild_search_dirs_locked(IconCacheData& d);
static void iconcache_build_indexes_locked(IconCacheData& d);

// Quality-ranked lookup over the per-directory indexes.
//
//   0 ..            SVG / scalable
//   1'000'000+      raster >= px, ranked by smallest overshoot
//   2'000'000+      raster < px, ranked by largest undershoot
// Theme priority is applied by the caller so quality wins across the whole
// inheritance chain (a 16px PNG in the active theme must not beat a 256px
// one in an inherited/fallback theme).
static constexpr long kSvgRank = 0;
static constexpr long kOvershootBase = 1000000L;
static constexpr long kUndershootBase = 2000000L;

static std::pair<std::string, long> index_lookup(
    IconCacheData& d, std::size_t dir_idx, const std::string& name, int px) {
  d.stIndexLookups.fetch_add(1, std::memory_order_relaxed);
  auto& idx = d.dirIndexes[dir_idx];
  const IconCacheData::IconCandidate* best = nullptr;
  long best_rank = kUndershootBase + 4096;
  auto rank_of = [&](const IconCacheData::IconCandidate& c) -> long {
    // Only SVGs in resolution-independent dirs get the vector rank. Some DE
    // themes also ship SVGs inside fixed-size dirs — those are usually
    // monochrome placeholders that must not outrank real scalable artwork,
    // so rank them by nominal size instead.
    if (c.dirSize == 0)
      return c.svg ? kSvgRank : kUndershootBase + static_cast<long>(px);
    if (c.dirSize >= px)
      return kOvershootBase + static_cast<long>(c.dirSize - px);
    return kUndershootBase + static_cast<long>(px - c.dirSize);
  };
  auto consider = [&](const std::vector<IconCacheData::IconCandidate>& vec) {
    for (const auto& c : vec) {
      long r = rank_of(c);
      if (!best || r < best_rank) { best = &c; best_rank = r; }
    }
  };
  if (auto it = idx.find(name); it != idx.end()) consider(it->second);
  else if (auto sit = idx.find(name + "-symbolic"); sit != idx.end())
    consider(sit->second);
  return best ? std::make_pair(best->path, best_rank)
              : std::make_pair(std::string{}, best_rank);
}

void IconCache::clear_locked() {
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
  d_->negativeKeys.clear();
  d_->queue.clear();
  d_->queuedKeys.clear();
  d_->dirIndexes.clear();
  d_->indexesBuilt = false;
  d_->indexesBeingBuilt.store(false);
  d_->searchDirs.clear();
  d_->resolvedThemeId.clear();
  d_->searchDirsBuilt = false;
  // Re-align to the current global generation: jobs enqueued from now on
  // capture this value, and any in-flight job carrying an older one is
  // discarded by resolve_and_insert instead of poisoning the fresh cache.
  d_->generation = s_iconThemeGeneration.load(std::memory_order_relaxed);
  reset_wmclass_desktop_index();
  eh::theming::clear_gtk_theme_colors_cache();
  eh::theming::clear_gtk_theme_design_cache();
}

void IconCache::clear() {
  std::lock_guard<std::mutex> lk(d_->mtx);
  clear_locked();
}

void IconCache::set_icon_theme(std::string themeId) {
  // "" (or "auto") means follow the system theme; keep that intent in
  // pinnedTheme while themeOverride tracks the CONCRETE resolved id.
  const std::string pinned = trim(themeId);
  std::string effective = pinned;
  if (effective.empty() || effective == "auto") effective = detect_system_icon_theme();
  std::lock_guard<std::mutex> lk(d_->mtx);
  if (d_->themeOverride == effective && d_->pinnedTheme == pinned) return;
  // Always log: theme switches are rare and the #1 thing to verify when
  // diagnosing "icons didn't change" reports.
  debug_log("icons", "theme_switch old=\"%s\" new=\"%s\" pinned=\"%s\" cleared=%zu", d_->themeOverride.c_str(),
            effective.c_str(), pinned.c_str(), d_->cache.size());
  d_->pinnedTheme = pinned.empty() || pinned == "auto" ? std::string() : pinned;
  d_->themeOverride = effective;
  s_iconThemeOverride = effective;
  s_iconThemeGeneration.fetch_add(1, std::memory_order_relaxed);
  // Drop everything tied to the previous theme; indexes rebuild lazily.
  clear_locked();
}

static void iconcache_rebuild_search_dirs_locked(IconCacheData& d) {
  if (d.searchDirsBuilt) return;

  std::string theme = trim(d.themeOverride);
  if (theme.empty()) theme = detect_system_icon_theme();
  const std::vector<std::string> themeChain = build_theme_chain(theme);
  const std::string primaryThemeId = theme.empty() ? detect_system_icon_theme() : theme;
  d.resolvedThemeId = primaryThemeId;

  const std::vector<std::string> bases = icon_base_dirs();
  const std::vector<std::string> iconBases = icon_base_dirs();

  d.searchDirs.clear();
  d.searchDirs.reserve(512);

  auto append_theme = [&](const std::string& themeId) {
    for (const auto& base : bases) {

      if (base.ends_with("/pixmaps")) continue;
      for (const auto& root : resolve_theme_roots_for_base(base, themeId)) {
        append_theme_search_dirs(d.searchDirs, root, themeId, iconBases);
      }
    }
  };

  if (!primaryThemeId.empty()) append_theme(primaryThemeId);

  append_theme("hicolor");

  d.searchDirs.push_back("/usr/share/pixmaps/");
  d.searchDirs.push_back("/usr/local/share/pixmaps/");

  for (const auto& themeId : themeChain) {
    if (themeId == primaryThemeId) continue;
    if (themeId == "hicolor") continue;
    append_theme(themeId);
  }

  {
    std::vector<std::string> uniq;
    uniq.reserve(d.searchDirs.size());
    std::unordered_set<std::string> seen;
    seen.reserve(d.searchDirs.size());
    for (auto& s : d.searchDirs) {
      if (s.empty()) continue;
      if (seen.insert(s).second) uniq.push_back(std::move(s));
    }
    d.searchDirs = std::move(uniq);
  }

  d.searchDirsBuilt = true;
  if (eh_icon_debug()) {
    std::cerr << "[icons] search_dirs theme=\"" << (d.themeOverride.empty() ? "auto" : d.themeOverride)
              << "\" sys=\"" << detect_system_icon_theme() << "\" n=" << d.searchDirs.size() << "\n";
  }
}

// Build per-directory name->candidates indexes. Caller holds d.mtx.
// The first call scans every theme dir once (~5-20 ms); afterwards icon
// lookups are pure hash probing with zero filesystem syscalls.
static void iconcache_build_indexes_locked(IconCacheData& d) {
  if (d.indexesBuilt) return;
  iconcache_rebuild_search_dirs_locked(d);
  d.dirIndexes.assign(d.searchDirs.size(), {});
  for (std::size_t i = 0; i < d.searchDirs.size(); ++i)
    scan_theme_tree(fs::path(d.searchDirs[i]), d.dirIndexes[i]);
  d.indexesBuilt = true;
  d.stIndexBuilds.fetch_add(1, std::memory_order_relaxed);
  if (eh_icon_debug())
    std::cerr << "[icons] built indexes for " << d.searchDirs.size() << " theme dirs\n";
}

void IconCache::rebuild_search_dirs_if_needed() {
  std::lock_guard<std::mutex> lk(d_->mtx);
  iconcache_rebuild_search_dirs_locked(*d_);
}

void IconCache::build_indexes_if_needed() {
  std::lock_guard<std::mutex> lk(d_->mtx);
  if (!d_->indexesBeingBuilt.exchange(true)) {
    iconcache_build_indexes_locked(*d_);
    d_->indexesBeingBuilt.store(false);
  }
}

void IconCache::prewarm_search_dirs() {
  ensureFresh();
  build_indexes_if_needed();
}

bool IconCache::refresh_auto_theme_if_needed() {
  std::string cur;
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (!d_->pinnedTheme.empty()) return false; // user pinned a theme: never follow system
    iconcache_rebuild_search_dirs_locked(*d_);
    cur = detect_system_icon_theme();
    if (!d_->resolvedThemeId.empty() && cur == d_->resolvedThemeId) return false;
    debug_log("icons", "auto_theme_changed old=\"%s\" new=\"%s\" cleared=%zu", d_->resolvedThemeId.c_str(),
              cur.c_str(), d_->cache.size());
  }
  s_iconThemeOverride = cur;
  s_iconThemeGeneration.fetch_add(1, std::memory_order_relaxed);
  clear();
  return true;
}

void IconCache::ensureFresh() {
  // Auto mode: opportunistically follow the system theme so every shell
  // component reacts to system-wide switches even without its own watcher.
  // The probe is time-gated and runs OUTSIDE the mutex because
  // detect_system_icon_theme may spawn a subprocess.
  bool probe = false;
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    // EH_ICON_THEME pins the theme hermetically (also what unit tests use):
    // never second-guess it with system detection.
    if (d_->pinnedTheme.empty() && !std::getenv("EH_ICON_THEME")) {
      const auto nowSec =
          std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count();
      if (nowSec != d_->lastAutoThemeProbeSec) {
        d_->lastAutoThemeProbeSec = nowSec;
        probe = true;
      }
    }
  }
  std::string sys;
  if (probe) sys = detect_system_icon_theme();

  std::lock_guard<std::mutex> lk(d_->mtx);
  if (!sys.empty() && d_->pinnedTheme.empty() && sys != d_->themeOverride) {
    debug_log("icons", "auto_follow old=\"%s\" new=\"%s\" cleared=%zu", d_->themeOverride.c_str(), sys.c_str(),
              d_->cache.size());
    d_->themeOverride = sys;
    s_iconThemeOverride = sys;
    s_iconThemeGeneration.fetch_add(1, std::memory_order_relaxed);
    clear_locked();
    return;
  }
  if (d_->generation != s_iconThemeGeneration.load(std::memory_order_relaxed)) {
    debug_log("icons", "stale_generation_clear held_gen=%llu global_gen=%llu cleared=%zu",
              static_cast<unsigned long long>(d_->generation),
              static_cast<unsigned long long>(s_iconThemeGeneration.load(std::memory_order_relaxed)),
              d_->cache.size());
    if (d_->themeOverride != s_iconThemeOverride) {
      d_->themeOverride = s_iconThemeOverride;
      d_->searchDirsBuilt = false;
    }
    clear_locked();
    d_->generation = s_iconThemeGeneration.load(std::memory_order_relaxed);
  }
}

// Core resolution.
//
// Insert a finished surface into the cache under lock (callers hold mtx).
static void insert_surface_locked(IconCacheData& d, const std::string& key,
                                  cairo_surface_t* surf, double /*totalMs*/,
                                  const char* debugPath) {
  if (eh_icon_debug()) {
    std::cerr << "[icons] hit key=\"" << key << "\" path=\"" << (debugPath ? debugPath : "")
              << "\"\n";
  }
  const std::size_t sz =
      static_cast<std::size_t>(cairo_image_surface_get_stride(surf)) *
      static_cast<std::size_t>(cairo_image_surface_get_height(surf));
  auto [eit, inserted] = d.cache.try_emplace(key);
  if (!inserted) {
    if (eit->second.surface) cairo_surface_destroy(eit->second.surface);
    if (eit->second.bytes > 0) {
      if (eit->second.bytes > d.totalBytes) d.totalBytes = 0;
      else d.totalBytes -= eit->second.bytes;
    }
    d.lru.erase(eit->second.lru_it);
  } else {
    eit->second.surface = nullptr;
  }
  eit->second.surface = surf;
  eit->second.width = cairo_image_surface_get_width(surf);
  eit->second.height = cairo_image_surface_get_height(surf);
  eit->second.bytes = sz;
  d.totalBytes += sz;
  d.stRasters.fetch_add(1, std::memory_order_relaxed);
  d.lru.push_front(key);
  eit->second.lru_it = d.lru.begin();
  evict_excess_locked(d);
}

// Capped one-line-per-key miss logging (callers hold mtx).
static void log_miss_locked(IconCacheData& d, const std::string& key,
                            const std::string& resolvedName) {
  if (!d.missLogged.contains(key)) {
    d.missLogged.insert(key);
    if (d.missLogged.size() > kMaxIconMissLogged) d.missLogged.clear();
    std::cerr << "[icons] miss key=\"" << key << "\" name=\"" << resolvedName
              << "\" theme=\"" << (d.themeOverride.empty() ? "auto" : d.themeOverride)
              << "\" sys=\"" << detect_system_icon_theme() << "\"\n";
  }
}

//
// resolve_and_insert: the single funnel every sync and async lookup goes
// through. Locking discipline: d.mtx is only ever held for map operations —
// desktop-file mapping, index building and rasterization all happen with
// the lock released so paint threads never stall behind them.
static void resolve_and_insert(IconCacheData& d, const std::string& key,
                               const std::string& iconName, bool isTray,
                               int load_size, std::uint64_t expectGen) {
  const auto missT0 = std::chrono::steady_clock::now();

  // Shell-specific: appIds/exec names are resolved to themed icon keys via
  // .desktop files before theme lookup.
  std::string resolvedName = iconName;
  if (!isTray) {
    std::optional<std::string> desktop = find_desktop_file_for_appid(iconName);
    if (!desktop) desktop = find_desktop_file_by_wmclass(iconName);
    if (desktop) {
      if (auto iconKey = read_desktop_icon_key(*desktop)) resolvedName = *iconKey;
    }
  }

  cairo_surface_t* surf = nullptr;
  if (!resolvedName.empty() && resolvedName[0] == '/') {
    if (file_exists(resolvedName)) {
      surf = resolvedName.ends_with(".svg") ? load_svg(resolvedName, load_size)
                                            : load_png(resolvedName, load_size);
    }
    const double totalMs = mono_ms_since(missT0);
    std::lock_guard<std::mutex> lk(d.mtx);
    if (d.generation != expectGen) {
      // Theme switched while we resolved: drop the stale result so the next
      // lookup re-resolves against the new theme.
      if (surf) cairo_surface_destroy(surf);
      return;
    }
    if (surf) {
      insert_surface_locked(d, key, surf, totalMs, resolvedName.c_str());
      return;
    }
    log_miss_locked(d, key, resolvedName);
    d.negativeKeys.insert(negative_key_of(key));
    return;
  }

  // Candidate names: exact case/symbolic/alias forms first ("strict"), then
  // the broad variant list (lowercase, vendor-stripped …). Game-launcher icons
  // skip the broad pass until strict + generic fallbacks have failed.
  auto pushu = [](std::vector<std::string>& v, const std::string& s) {
    if (s.empty()) return;
    if (std::find(v.begin(), v.end(), s) != v.end()) return;
    v.push_back(s);
  };
  std::vector<std::string> strict_cands;
  std::vector<std::string> full_cands;
  {
    auto lower_of = [](const std::string& n) {
      std::string l = n;
      std::transform(l.begin(), l.end(), l.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return l;
    };
    const std::string lower = lower_of(resolvedName);
    pushu(strict_cands, resolvedName);
    pushu(strict_cands, resolvedName + "-symbolic");
    if (lower != resolvedName) {
      pushu(strict_cands, lower);
      pushu(strict_cands, lower + "-symbolic");
    }
    for (const auto& a : icon_aliases(resolvedName)) {
      pushu(strict_cands, a);
      pushu(strict_cands, a + "-symbolic");
    }
    for (const auto& a : icon_aliases(lower)) {
      pushu(strict_cands, a);
      pushu(strict_cands, a + "-symbolic");
    }
    for (const auto& v : icon_name_variants(resolvedName)) pushu(full_cands, v);
  }
  std::string resolvedLower = resolvedName;
  std::transform(resolvedLower.begin(), resolvedLower.end(), resolvedLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const bool isSteamGameIcon = (resolvedLower.rfind("steam_icon_", 0) == 0);

  static constexpr long kPrioSpan = 20000000L;
  std::string path;
  bool indexesReady = false;
  {
    std::lock_guard<std::mutex> lk(d.mtx);
    iconcache_rebuild_search_dirs_locked(d);
    if (!d.indexesBuilt && !d.indexesBeingBuilt.exchange(true)) {
      iconcache_build_indexes_locked(d);          // sync callers build inline
      d.indexesBeingBuilt.store(false);
    }
    indexesReady = d.indexesBuilt;

    // Direct filesystem probe used while another thread builds the indexes;
    // results found this way are never negative-cached (the index may have
    // contained them moments later).
    auto direct_probe = [&](std::size_t i, const std::string& cand) -> std::string {
      const std::string& dir = d.searchDirs[i];
      for (const char* ext : {".svg", ".png"}) {
        const std::string p = dir + cand + ext;
        if (file_exists(p)) return p;
      }
      return {};
    };

    auto search_pass = [&](const std::vector<std::string>& cands) {
      if (!path.empty()) return;
      long best_total = kUndershootBase + 4096 +
                        static_cast<long>(d.searchDirs.size()) * kPrioSpan;
      for (const auto& cand : cands) {
        bool found = false;
        for (std::size_t i = 0; i < d.searchDirs.size(); ++i) {
          std::string p;
          long rank = kSvgRank;
          if (indexesReady) {
            auto [pp, rr] = index_lookup(d, i, cand, load_size);
            p = pp;
            rank = rr;
            if (p.empty()) continue;
          } else {
            p = direct_probe(i, cand);
            if (p.empty()) continue;
          }
          // Earlier searchDirs are higher priority (active theme first, then
          // hicolor, then pixmaps). prio must therefore GROW with i: we
          // minimize rank+prio, so a shrinking term would invert theme
          // precedence and let pixmaps/hicolor beat the active theme.
          const long prio = static_cast<long>(i) * kPrioSpan;
          if (rank + prio < best_total) {
            best_total = rank + prio;
            path = p;
          }
          found = true;
        }
        if (found && !path.empty()) break;   // first candidate name wins
      }
    };
    auto search_generic_fallback = [&]() {
      if (!path.empty()) return;
      for (const char* fb : {"application-x-executable", "application-default-icon",
                             "system-run", "dialog-question"}) {
        for (std::size_t i = 0; i < d.searchDirs.size(); ++i) {
          std::string p;
          if (indexesReady) {
            auto [pp, rr] = index_lookup(d, i, fb, load_size);
            (void)rr;
            p = pp;
          } else {
            p = direct_probe(i, fb);
          }
          if (!p.empty()) { path = p; break; }
        }
        if (!path.empty()) break;
      }
    };

    search_pass(strict_cands);
    if (path.empty() && !isSteamGameIcon) search_pass(full_cands);
    if (path.empty()) search_generic_fallback();
    if (path.empty() && isSteamGameIcon) search_pass(full_cands);

    if (path.empty()) {
      if (d.generation != expectGen) return;   // stale: theme switched mid-flight
      log_miss_locked(d, key, resolvedName);
      if (indexesReady) d.negativeKeys.insert(negative_key_of(key));
      const double totalMs = mono_ms_since(missT0);
      IconPerf& perf = icon_perf();
      std::lock_guard<std::mutex> plk(perf.mtx);
      ++perf.misses;
      perf.addResolve(totalMs * 1000.0);
      return;
    }
  }

  const double resolveMs = mono_ms_since(missT0);
  const auto loadT0 = std::chrono::steady_clock::now();
  surf = path.ends_with(".svg") ? load_svg(path, load_size)
                                : load_png(path, load_size);
  const double loadMs = mono_ms_since(loadT0);
  const double totalMs = resolveMs + loadMs;

  {
    IconPerf& perf = icon_perf();
    std::lock_guard<std::mutex> plk(perf.mtx);
    ++perf.misses;
    perf.addResolve(resolveMs * 1000.0);
    perf.addLoad(loadMs * 1000.0);
    if (resolveMs * 1000.0 > perf.maxResolveUs) {
      perf.maxResolveUs = resolveMs * 1000.0;
      perf.worstKey = key;
    }
    if (loadMs * 1000.0 > perf.maxLoadUs) perf.maxLoadUs = loadMs * 1000.0;
    if (totalMs > 3.0) {
      ++perf.missesSlow;
      debug_log("icons", "slow key=\"%s\" name=\"%s\" path=\"%s\" resolve=%.2fms load=%.2fms",
                key.c_str(), resolvedName.c_str(), path.c_str(), resolveMs, loadMs);
    }
  }

  std::lock_guard<std::mutex> lk(d.mtx);
  if (d.generation != expectGen) {
    // Theme switched while we rasterized (the big SVGs take ~400ms): discard
    // so the new theme's lookup isn't poisoned by an old-theme surface.
    if (surf) cairo_surface_destroy(surf);
    return;
  }
  if (!surf) {
    log_miss_locked(d, key, resolvedName);
    d.negativeKeys.insert(negative_key_of(key));
    return;
  }
  insert_surface_locked(d, key, surf, totalMs, path.c_str());
}

// Async worker.

void IconCache::ensure_worker_started() {
  if (d_->workerRunning.exchange(true)) return;
  auto data = d_;   // shared_ptr copy keeps the data alive past instance death
  d_->worker = std::make_unique<std::thread>([data]() {
    pthread_setname_np(pthread_self(), "eh-icon-loader");
    std::unique_lock<std::mutex> lk(data->mtx);
    while (!data->quit.load()) {
      data->cv.wait(lk, [&] {
        return data->quit.load() || !data->queue.empty();
      });
      if (data->quit.load()) break;
      IconCacheData::PendingLoad job = std::move(data->queue.front());
      data->queue.pop_front();
      data->queuedKeys.erase(job.key);
      data->inFlight.fetch_add(1, std::memory_order_relaxed);
      lk.unlock();
      resolve_and_insert(*data, job.key, job.name, true, bucket_for(job.px), job.gen);
      data->inFlight.fetch_sub(1, std::memory_order_relaxed);
      data->cv.notify_all();
      lk.lock();
    }
    data->workerRunning.store(false);
  });
}

void IconCache::enqueue_async(const std::string& key, const std::string& name, int px) {
  // Caller must hold d_->mtx (see tray_icon(size) / worker contract).
  ensure_worker_started();
  if (d_->quit.load()) return;
  if (d_->cache.contains(key)) return;
  if (d_->negativeKeys.count(negative_key_of(key))) return;
  if (!d_->queuedKeys.insert(key).second) return;
  d_->queue.push_back(IconCacheData::PendingLoad{key, name, px, d_->generation});
  d_->stAsyncEnqueued.fetch_add(1, std::memory_order_relaxed);
  d_->cv.notify_one();
}

void IconCache::stop_worker() {
  std::unique_lock<std::mutex> lk(d_->mtx);
  d_->quit.store(true);
  d_->cv.notify_all();
  if (d_->worker && d_->worker->joinable()) {
    std::thread to_join = std::move(*d_->worker);
    d_->worker.reset();
    lk.unlock();
    to_join.join();
  } else {
    d_->worker.reset();
  }
  d_->quit.store(false);
}

// Public entry points.

const IconEntry* IconCache::tray_icon_sync(const std::string& iconName, int pixelSize) {
  ensureFresh();
  const int px = bucket_for(pixelSize <= 0 ? 256 : pixelSize);
  const std::string key = "tray:" + iconName + "@" + std::to_string(px);
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (auto it = d_->cache.find(key); it != d_->cache.end()) {
      touch_lru_locked(*d_, key);
      d_->stCacheHits.fetch_add(1, std::memory_order_relaxed);
      return &it->second;
    }
    if (d_->negativeKeys.count(negative_key_of(key))) {
      d_->stNegativeHits.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
  }
  resolve_and_insert(*d_, key, iconName, true, px, d_->generation);
  std::lock_guard<std::mutex> lk(d_->mtx);
  auto it = d_->cache.find(key);
  return it == d_->cache.end() ? nullptr : &it->second;
}

const IconEntry* IconCache::tray_icon(const std::string& iconName, int pixelSize) {
  static const bool async_enabled = [] {
    const char* e = std::getenv("EH_ICON_ASYNC");
    return !(e && std::string_view(e) == "0");
  }();
  if (!async_enabled) return tray_icon_sync(iconName, pixelSize);

  ensureFresh();
  const int px = bucket_for(pixelSize <= 0 ? 256 : pixelSize);
  const std::string key = "tray:" + iconName + "@" + std::to_string(px);
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (auto it = d_->cache.find(key); it != d_->cache.end()) {
      touch_lru_locked(*d_, key);
      d_->stCacheHits.fetch_add(1, std::memory_order_relaxed);
      return &it->second;
    }
    if (d_->negativeKeys.count(negative_key_of(key))) {
      d_->stNegativeHits.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    enqueue_async(key, iconName, px);   // mtx held by design
  }
  return nullptr;   // caller draws placeholder; retries next frame
}

const IconEntry* IconCache::resolve_and_cache(const std::string& key, const std::string& iconName, bool isTray,
                                              int px) {
  ensureFresh();
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (auto it = d_->cache.find(key); it != d_->cache.end()) {
      touch_lru_locked(*d_, key);
      d_->stCacheHits.fetch_add(1, std::memory_order_relaxed);
      return &it->second;
    }
    if (d_->negativeKeys.count(negative_key_of(key))) {
      d_->stNegativeHits.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
  }
  resolve_and_insert(*d_, key, iconName, isTray, px, d_->generation);
  std::lock_guard<std::mutex> lk(d_->mtx);
  auto it = d_->cache.find(key);
  return it == d_->cache.end() ? nullptr : &it->second;
}

const IconEntry* IconCache::app_icon_from_exec_basename(const std::string& execBasename) {
  ensureFresh();
  std::string binLower = path_basename(execBasename);
  std::transform(binLower.begin(), binLower.end(), binLower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (binLower.empty()) return nullptr;

  const std::string key = "execbin:" + binLower;
  {
    std::lock_guard<std::mutex> lk(d_->mtx);
    if (auto it = d_->cache.find(key); it != d_->cache.end()) {
      if (!it->second.surface) return nullptr;
      touch_lru_locked(*d_, key);
      d_->stCacheHits.fetch_add(1, std::memory_order_relaxed);
      return &it->second;
    }
    if (d_->execBasenameMiss.contains(binLower)) return nullptr;
  }

  auto iconKey = scan_desktop_icon_for_exec_basename(binLower);
  if (!iconKey || iconKey->empty()) {
    std::lock_guard<std::mutex> lk(d_->mtx);
    d_->execBasenameMiss.insert(binLower);
    if (d_->execBasenameMiss.size() > kMaxIconMissLogged) d_->execBasenameMiss.clear();
    return nullptr;
  }
  return resolve_and_cache(key, *iconKey, true, 256);
}

const IconEntry* IconCache::app_icon(const std::string& appId) {
  ensureFresh();
  d_->keyBuf.clear();
  d_->keyBuf.append("app:");
  d_->keyBuf.append(appId);
  return resolve_and_cache(d_->keyBuf, appId, false, 256);
}

const IconEntry* IconCache::app_icon(const std::string& appId, int pixelSize) {
  ensureFresh();
  const int px = bucket_for(pixelSize <= 0 ? 256 : pixelSize);
  const std::string key = "app:" + appId + "@" + std::to_string(px);
  return resolve_and_cache(key, appId, false, px);
}

const IconEntry* IconCache::tray_icon(const std::string& iconName) {
  ensureFresh();
  d_->keyBuf.clear();
  d_->keyBuf.append("tray:");
  d_->keyBuf.append(iconName);
  return resolve_and_cache(d_->keyBuf, iconName, true, 256);
}

// Test/bench hooks.

IconCacheStats IconCache::stats() const {
  IconCacheStats s;
  s.cacheHits = d_->stCacheHits.load(std::memory_order_relaxed);
  s.negativeHits = d_->stNegativeHits.load(std::memory_order_relaxed);
  s.indexLookups = d_->stIndexLookups.load(std::memory_order_relaxed);
  s.rasters = d_->stRasters.load(std::memory_order_relaxed);
  s.indexBuilds = d_->stIndexBuilds.load(std::memory_order_relaxed);
  s.asyncEnqueued = d_->stAsyncEnqueued.load(std::memory_order_relaxed);
  return s;
}

bool IconCache::is_negative_cached(const std::string& key) const {
  std::lock_guard<std::mutex> lk(d_->mtx);
  return d_->negativeKeys.count(negative_key_of(key)) > 0;
}

std::size_t IconCache::pending_count() const {
  std::lock_guard<std::mutex> lk(d_->mtx);
  return d_->queue.size();
}

void IconCache::wait_for_pending() {
  // Block until the queue is empty AND no job is mid-raster on the worker.
  std::unique_lock<std::mutex> lk(d_->mtx);
  d_->cv.wait(lk, [&] {
    return d_->queue.empty() && d_->inFlight.load(std::memory_order_relaxed) == 0;
  });
}

std::string theme_example_icon_name(const std::string& themeDir) {
   
  const std::string idx = themeDir + "/index.theme";
  return read_ini_kv(idx, "[Icon Theme]", "Example");
}

std::vector<std::string> theme_find_any_icons(const std::string& themeDir, int maxCount) {
   
  std::vector<std::string> out;

  const std::string idx = themeDir + "/index.theme";
  if (!file_exists(idx)) return out;

  const std::string dirsAll = read_ini_kv(idx, "[Icon Theme]", "Directories");
  std::vector<std::string> scanDirs;
  if (!dirsAll.empty()) {
    for (const auto& d : split_csv(dirsAll)) {
      if (!d.empty()) scanDirs.push_back(themeDir + "/" + d + "/");
    }
  }

  static const char* kScan[] = {
    "scalable/apps/", "scalable/places/", "scalable/devices/", "scalable/actions/",
    "scalable/mimetypes/", "scalable/status/",
    "48x48/apps/", "48x48/places/", "48x48/devices/",
    "32x32/apps/", "32x32/places/", "32x32/devices/",
    "16x16/apps/", "16x16/places/", "16x16/devices/",
    "places/scalable/", "places/48/", "places/32/", "places/16/",
    "devices/scalable/", "devices/48/", "devices/32/", "devices/16/",
    "mimes/scalable/", "mimes/16/"
  };
  for (const char* p : kScan) scanDirs.push_back(themeDir + "/" + p);

  std::unordered_set<std::string> seen;
  for (const auto& sd : scanDirs) {
    DIR* d = opendir(sd.c_str());
    if (!d) continue;
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      if (out.size() >= static_cast<size_t>(maxCount)) break;
      const std::string nm(ent->d_name);
      if (nm == "." || nm == "..") continue;
      const auto dot = nm.rfind('.');
      if (dot == std::string::npos) continue;
      const std::string ext = nm.substr(dot);
      if (ext != ".svg" && ext != ".png" && ext != ".svgz") continue;
      std::string base = nm.substr(0, dot);
      if (base.empty()) continue;
      if (!seen.insert(base).second) continue;
      out.push_back(base);
    }
    closedir(d);
    if (out.size() >= static_cast<size_t>(maxCount)) break;
  }
  return out;
}

cairo_surface_t* load_theme_preview_icon(const std::string& themeDir, const std::string& iconName, int targetPx) {
   
  std::vector<std::string> searchDirs;
  searchDirs.reserve(128);

  const std::string themeId = themeDir.substr(themeDir.rfind('/') + 1);
  append_theme_search_dirs(searchDirs, themeDir, themeId, icon_base_dirs());

  {
    std::vector<std::string> uniq;
    uniq.reserve(searchDirs.size());
    std::unordered_set<std::string> seen;
    for (auto& d : searchDirs) {
      if (d.empty()) continue;
      if (seen.insert(d).second) uniq.push_back(std::move(d));
    }
    searchDirs = std::move(uniq);
  }

  for (const auto& sd : searchDirs) {
    for (const char* ext : {".svg", ".png", ".svgz"}) {
      const std::string fp = sd + iconName + ext;
      if (file_exists(fp)) {
        const int wantPx = targetPx > 0 ? targetPx : bucket_for(256);
        cairo_surface_t* surf = (ext == std::string(".svg"))
                                    ? load_svg(fp, wantPx)
                                    : load_png(fp, wantPx);
        if (surf) {
          if (targetPx > 0) {
            const int w = cairo_image_surface_get_width(surf);
            const int h = cairo_image_surface_get_height(surf);
            if (w > targetPx || h > targetPx) {
              const double sc = static_cast<double>(targetPx) / static_cast<double>(std::max(w, h));
              const int dw = std::max(1, static_cast<int>(std::lround(static_cast<double>(w) * sc)));
              const int dh = std::max(1, static_cast<int>(std::lround(static_cast<double>(h) * sc)));
              cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
              if (scaled && cairo_surface_status(scaled) == CAIRO_STATUS_SUCCESS) {
                cairo_t* cr = cairo_create(scaled);
                cairo_scale(cr, sc, sc);
                cairo_set_source_surface(cr, surf, 0, 0);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_destroy(cr);
                cairo_surface_destroy(surf);
                return scaled;
              }
              if (scaled) cairo_surface_destroy(scaled);
            }
          }
          return surf;
        }
      }
    }
  }
  return nullptr;
}

bool apply_icon_theme(const std::string& themeId) {
   
  if (themeId.empty()) return false;
  const std::string cmd = "gsettings set org.gnome.desktop.interface icon-theme '" + themeId + "' 2>/dev/null";
  (void)std::system(cmd.c_str());
  const char* home = std::getenv("HOME");
  if (!home) return true;
  // Write gtk-icon-theme-name to the toolkit settings.ini, preserving existing gtk-theme-name
  for (const char* rel : {"/.config/gtk-4.0/settings.ini", "/.config/gtk-3.0/settings.ini"}) {
    update_ini_key(std::string(home) + rel, "[Settings]", "gtk-icon-theme-name", themeId);
  }
  // Write to the Qt platform-theme config
  {
    update_ini_key(std::string(home) + "/.config/qt6ct/qt6ct.conf", "[Appearance]", "icon_theme", themeId);
  }
  {
    update_ini_key(std::string(home) + "/.config/qt5ct/qt5ct.conf", "[Appearance]", "icon_theme", themeId);
  }
  // Write to the shared globals config for the platform's apps
  {
    update_ini_key(std::string(home) + "/.config/kdeglobals", "[Icons]", "Theme", themeId);
  }
  return true;
}

}
