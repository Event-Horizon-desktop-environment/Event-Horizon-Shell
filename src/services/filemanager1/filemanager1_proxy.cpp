// Event Horizon FileManager1 proxy.
//
// Browsers ("Open containing folder" / "Show in folder") and the shell's own
// reveal actions speak org.freedesktop.FileManager1 (ShowItems/ShowFolders),
// NOT the inode/directory MIME handler. Whichever app owns that bus name wins,
// regardless of Settings > Default Apps. This proxy owns the name on behalf of
// the configured default file manager and forwards each request to it, so the
// Default Apps choice actually takes effect system-wide.
//
// Resolution is done per call: Settings stores the choice in
// <state>/default_apps/default_apps.toml ([default_apps] file_manager) and
// mirrors it with `xdg-mime default <id> inode/directory`. The proxy reads
// the TOML first, falls back to `xdg-mime query`, locates the .desktop entry,
// expands its Exec field codes, and spawns it detached. Final fallback is
// `xdg-open`/`gio open`, which honor the MIME default.

#include <sdbus-c++/Error.h>
#include <sdbus-c++/IConnection.h>
#include <sdbus-c++/IObject.h>
#include <sdbus-c++/VTableItems.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr const char* kBusName = "org.freedesktop.FileManager1";
constexpr const char* kObjectPath = "/org/freedesktop/FileManager1";
constexpr const char* kInterface = "org.freedesktop.FileManager1";

std::string bus_name_override() {
  // Test hook: run the proxy on a throwaway bus name so end-to-end behavior
  // can be exercised without disturbing the real FileManager1 owner.
  if (const char* e = std::getenv("EH_FILEMANAGER1_BUS_NAME"); e && e[0]) return e;
  return kBusName;
}

const char* xdg_env(const char* name) {
  const char* v = std::getenv(name);
  return (v && v[0]) ? v : nullptr;
}

std::string home_dir() {
  if (const char* h = xdg_env("HOME")) return h;
  return "/tmp";
}

std::string state_home() {
  if (const char* x = xdg_env("XDG_STATE_HOME")) return x;
  return home_dir() + "/.local/state";
}

std::vector<std::string> data_dirs() {
  std::vector<std::string> out;
  if (const char* h = xdg_env("XDG_DATA_HOME")) {
    if (h[0]) out.emplace_back(h);
  } else {
    out.push_back(home_dir() + "/.local/share");
  }
  if (const char* d = xdg_env("XDG_DATA_DIRS")) {
    std::string_view sv(d);
    size_t start = 0;
    while (start <= sv.size()) {
      size_t end = sv.find(':', start);
      if (end == std::string_view::npos) end = sv.size();
      if (end > start) out.emplace_back(sv.substr(start, end - start));
      start = end + 1;
    }
  } else {
    out.emplace_back("/usr/local/share");
    out.emplace_back("/usr/share");
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

[[nodiscard]] std::string trim_ws(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  if (a > 0) s.erase(0, a);
  return s;
}

[[nodiscard]] std::string unquote_toml(std::string s) {
  s = trim_ws(std::move(s));
  if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"')))
    return s.substr(1, s.size() - 2);
  return s;
}

// Reads [default_apps] file_manager from the Event Horizon component TOML.
// Minimal parser: section-aware single-key lookup, no full TOML needed.
[[nodiscard]] std::string read_configured_file_manager() {
  const std::string path = state_home() + "/event-horizon/default_apps/default_apps.toml";
  std::ifstream f(path);
  if (!f) return {};
  std::string line;
  bool inSection = false;
  while (std::getline(f, line)) {
    std::string t = trim_ws(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.front() == '[') {
      inSection = (t == "[default_apps]");
      continue;
    }
    if (!inSection) continue;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    if (trim_ws(t.substr(0, eq)) != "file_manager") continue;
    std::string v = unquote_toml(t.substr(eq + 1));
    // Strip any trailing comment left outside quotes (values here are simple).
    const size_t hash = v.find('#');
    if (hash != std::string::npos) v = trim_ws(v.substr(0, hash));
    return v;
  }
  return {};
}

[[nodiscard]] std::string popen_first_line(const char* cmd) {
  FILE* p = popen(cmd, "r");
  if (!p) return {};
  char buf[512] = {0};
  std::string out;
  if (fgets(buf, sizeof(buf), p)) out = trim_ws(buf);
  pclose(p);
  return out;
}

[[nodiscard]] std::string normalize_desktop_id(std::string_view stored) {
  std::string s(stored);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  if (a > 0) s.erase(0, a);
  if (const size_t sl = s.rfind('/'); sl != std::string::npos) s.erase(0, sl + 1);
  constexpr std::string_view suf = ".desktop";
  if (s.size() <= suf.size() || s.compare(s.size() - suf.size(), suf.size(), suf) != 0) s += suf;
  return s;
}

[[nodiscard]] std::optional<std::string> find_desktop_file(const std::string& id) {
  for (const std::string& dir : data_dirs()) {
    fs::path p = fs::path(dir) / "applications" / id;
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) return p.string();
  }
  return std::nullopt;
}

struct DesktopLaunch {
  std::string exec;
  std::string name;
  std::string icon;
  std::string path;
};

[[nodiscard]] std::optional<DesktopLaunch> parse_desktop_launch(const std::string& path) {
  std::ifstream f(path);
  if (!f) return std::nullopt;
  DesktopLaunch out;
  out.path = path;
  std::string line;
  bool inEntry = false;
  while (std::getline(f, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (!line.empty() && line.front() == '[') {
      inEntry = (line == "[Desktop Entry]");
      continue;
    }
    if (!inEntry) continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim_ws(line.substr(0, eq));
    if (key == "Exec" || key == "Name" || key == "Icon") {
      // Keys may carry locale suffixes (Name[en]); only take the base key.
      if (key.find('[') != std::string::npos) continue;
    } else {
      continue;
    }
    std::string val = line.substr(eq + 1);
    while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) val.erase(val.begin());
    if (key == "Exec" && out.exec.empty()) out.exec = val;
    if (key == "Name" && out.name.empty()) out.name = val;
    if (key == "Icon" && out.icon.empty()) out.icon = val;
  }
  if (out.exec.empty()) return std::nullopt;
  return out;
}

// Split an Exec line into argv tokens honoring desktop-entry quoting
// (double quotes, single quotes, backslash escapes outside single quotes).
[[nodiscard]] std::vector<std::string> split_exec(const std::string& exec) {
  std::vector<std::string> argv;
  std::string cur;
  bool inSingle = false;
  bool inDouble = false;
  for (size_t i = 0; i < exec.size(); ++i) {
    const char c = exec[i];
    if (inSingle) {
      if (c == '\'') inSingle = false;
      else cur += c;
    } else if (inDouble) {
      if (c == '"') inDouble = false;
      else if (c == '\\' && i + 1 < exec.size()) cur += exec[++i];
      else cur += c;
    } else {
      if (c == ' ' || c == '\t') {
        if (!cur.empty()) {
          argv.push_back(cur);
          cur.clear();
        }
      } else if (c == '\'') {
        inSingle = true;
      } else if (c == '"') {
        inDouble = true;
      } else if (c == '\\' && i + 1 < exec.size()) {
        cur += exec[++i];
      } else {
        cur += c;
      }
    }
  }
  if (!cur.empty()) argv.push_back(cur);
  return argv;
}

[[nodiscard]] int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

// file:///tmp/a%20b -> /tmp/a b. Returns {} for non-file URIs.
[[nodiscard]] std::string path_from_file_uri(const std::string& uri) {
  if (uri.rfind("file://", 0) != 0) return {};
  std::string p = uri.substr(7);
  if (p.empty()) return {};
  if (p[0] != '/') {
    const size_t slash = p.find('/');
    if (slash == std::string::npos) return {};
    p = p.substr(slash);
  }
  std::string out;
  out.reserve(p.size());
  for (size_t i = 0; i < p.size(); ++i) {
    if (p[i] == '%' && i + 2 < p.size()) {
      const int hi = hex_nibble(p[i + 1]);
      const int lo = hex_nibble(p[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out += p[i];
  }
  return out;
}

[[nodiscard]] bool is_dir_path(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

[[nodiscard]] std::string parent_of(const std::string& path) {
  fs::path p(path);
  if (p.has_parent_path()) {
    std::string s = p.parent_path().string();
    if (!s.empty()) return s;
  }
  return "/";
}

// Expand one Exec token's field codes. `uris`/`paths` are parallel lists for
// the targets being opened; returns the expanded argv fragments for this token
// (empty when the token is a code with nothing to expand to).
[[nodiscard]] std::vector<std::string> expand_token(const std::string& tok, const std::vector<std::string>& uris,
                                                    const std::vector<std::string>& paths,
                                                    const DesktopLaunch& de) {
  if (tok.empty() || tok[0] != '%' || tok.size() != 2) return {tok};
  const char code = tok[1];
  switch (code) {
    case '%': return {"%"};
    case 'U':
    case 'F': {
      if (code == 'U') return uris;
      return paths;
    }
    case 'u':
    case 'f': {
      if (!uris.empty()) return {code == 'u' ? uris[0] : (paths.empty() ? uris[0] : paths[0])};
      return {};
    }
    case 'd':
    case 'D': {
      // Working directory of the first target (deprecated codes, best effort).
      if (!paths.empty()) return {parent_of(paths[0])};
      return {};
    }
    case 'n':
    case 'N': {
      if (!paths.empty()) return {fs::path(paths[0]).filename().string()};
      if (!uris.empty()) return {uris[0]};
      return {};
    }
    case 'c': return {de.name};
    case 'i':
      if (!de.icon.empty()) return {"--icon", de.icon};
      return {};
    case 'k': return {de.path};
    case 'v':
    case 'm': return {};
    default: return {};  // Reserved/unknown codes are dropped.
  }
}

// Double-fork so the launched manager is reparented and never zombies.
void spawn_argv_detached(const std::vector<std::string>& argv) {
  if (argv.empty()) return;
  const pid_t first = fork();
  if (first < 0) {
    std::cerr << "[filemanager1] fork failed\n";
    return;
  }
  if (first > 0) {
    int st = 0;
    while (waitpid(first, &st, 0) < 0) {
    }
    return;
  }
  // Intermediate child: detach, fork again, exit.
  if (setsid() < 0) _exit(127);
  const pid_t second = fork();
  if (second < 0) _exit(127);
  if (second > 0) _exit(0);
  // Grandchild: drop stdio, exec.
  int devNull = open("/dev/null", O_RDWR);
  if (devNull >= 0) {
    dup2(devNull, STDIN_FILENO);
    dup2(devNull, STDOUT_FILENO);
    dup2(devNull, STDERR_FILENO);
    if (devNull > 2) close(devNull);
  }
  std::vector<char*> args;
  args.reserve(argv.size() + 1);
  for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
  args.push_back(nullptr);
  execvp(args[0], args.data());
  // Fall through only on exec failure.
  _exit(127);
}

void spawn_fallback_open(const std::string& uri) {
  // Honor the MIME default directly. Try xdg-open first, then gio.
  pid_t first = fork();
  if (first < 0) return;
  if (first > 0) {
    int st = 0;
    while (waitpid(first, &st, 0) < 0) {
    }
    return;
  }
  if (setsid() < 0) _exit(127);
  const pid_t second = fork();
  if (second < 0) _exit(127);
  if (second > 0) _exit(0);
  int devNull = open("/dev/null", O_RDWR);
  if (devNull >= 0) {
    dup2(devNull, STDIN_FILENO);
    dup2(devNull, STDOUT_FILENO);
    dup2(devNull, STDERR_FILENO);
    if (devNull > 2) close(devNull);
  }
  execlp("xdg-open", "xdg-open", uri.c_str(), static_cast<char*>(nullptr));
  execlp("gio", "gio", "open", uri.c_str(), static_cast<char*>(nullptr));
  _exit(127);
}

struct ResolvedManager {
  DesktopLaunch desktop;
  bool ok = false;
};

[[nodiscard]] ResolvedManager resolve_default_manager() {
  ResolvedManager out;
  std::string id = read_configured_file_manager();
  if (id.empty()) {
    id = popen_first_line("command -v xdg-mime >/dev/null 2>&1 && xdg-mime query default inode/directory");
  }
  if (id.empty()) return out;
  id = normalize_desktop_id(id);
  auto found = find_desktop_file(id);
  if (!found) {
    std::cerr << "[filemanager1] desktop file not found: " << id << '\n';
    return out;
  }
  auto de = parse_desktop_launch(*found);
  if (!de) {
    std::cerr << "[filemanager1] no Exec in: " << *found << '\n';
    return out;
  }
  out.desktop = std::move(*de);
  out.ok = true;
  return out;
}

void open_uris_in_manager(const std::vector<std::string>& uris) {
  if (uris.empty()) return;
  // Build a parallel path list; non-file URIs keep an empty path slot so the
  // manager still receives the original URI for %U.
  std::vector<std::string> paths;
  paths.reserve(uris.size());
  for (const auto& u : uris) paths.push_back(path_from_file_uri(u));

  ResolvedManager mgr = resolve_default_manager();
  if (!mgr.ok) {
    for (const auto& u : uris) spawn_fallback_open(u);
    return;
  }
  const std::vector<std::string> tokens = split_exec(mgr.desktop.exec);
  if (tokens.empty()) {
    for (const auto& u : uris) spawn_fallback_open(u);
    return;
  }
  std::vector<std::string> argv;
  argv.push_back(tokens[0]);
  bool consumedTargets = false;
  for (size_t i = 1; i < tokens.size(); ++i) {
    std::vector<std::string> frag = expand_token(tokens[i], uris, paths, mgr.desktop);
    if (tokens[i].size() == 2 && tokens[i][0] == '%' &&
        (tokens[i][1] == 'U' || tokens[i][1] == 'F' || tokens[i][1] == 'u' || tokens[i][1] == 'f')) {
      if (!frag.empty()) consumedTargets = true;
    }
    argv.insert(argv.end(), frag.begin(), frag.end());
  }
  if (!consumedTargets) {
    // Manager takes no file arguments: fall back to the MIME opener so the
    // requested folder still opens instead of a bare default window.
    for (const auto& u : uris) spawn_fallback_open(u);
    return;
  }
  spawn_argv_detached(argv);
}

// ShowItems URIs point at files to reveal: open each parent folder (grouped,
// deduplicated). Managers without --select support still land in the right
// place; exact file highlighting is best-effort via the manager itself.
void handle_show_items(const std::vector<std::string>& uris) {
  std::vector<std::string> folders;
  for (const auto& u : uris) {
    const std::string p = path_from_file_uri(u);
    if (p.empty()) {
      folders.push_back(u);  // Non-file URI: hand through untouched.
      continue;
    }
    folders.push_back(is_dir_path(p) ? p : parent_of(p));
  }
  std::sort(folders.begin(), folders.end());
  folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
  if (folders.empty()) return;
  // Convert local paths back to file:// URIs for %U managers.
  std::vector<std::string> asUris;
  asUris.reserve(folders.size());
  for (const auto& f : folders) {
    if (!f.empty() && f[0] == '/') asUris.push_back("file://" + f);
    else asUris.push_back(f);
  }
  open_uris_in_manager(asUris);
}

void handle_show_folders(const std::vector<std::string>& uris) {
  std::vector<std::string> dirs;
  for (const auto& u : uris) {
    const std::string p = path_from_file_uri(u);
    if (p.empty()) {
      dirs.push_back(u);
      continue;
    }
    dirs.push_back(is_dir_path(p) ? p : parent_of(p));
  }
  std::sort(dirs.begin(), dirs.end());
  dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());
  if (dirs.empty()) return;
  std::vector<std::string> asUris;
  asUris.reserve(dirs.size());
  for (const auto& d : dirs) {
    if (!d.empty() && d[0] == '/') asUris.push_back("file://" + d);
    else asUris.push_back(d);
  }
  open_uris_in_manager(asUris);
}

}  // namespace

int main(int argc, char** argv) {
  bool replace = false;
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "--replace") replace = true;
  }
  const std::string busName = bus_name_override();
  std::unique_ptr<sdbus::IConnection> bus;
  try {
    bus = sdbus::createSessionBusConnection();
  } catch (const std::exception& e) {
    std::cerr << "[filemanager1] session bus connect failed: " << e.what() << '\n';
    return 1;
  }
  try {
    auto daemon = sdbus::createProxy(*bus, sdbus::ServiceName{"org.freedesktop.DBus"},
                                     sdbus::ObjectPath{"/org/freedesktop/DBus"});
    if (replace) {
      // Immediate takeover after Settings routed the name to us:
      // REPLACE_EXISTING without ALLOW_REPLACEMENT preempts the previous
      // owner but cannot itself be preempted back.
      constexpr std::uint32_t kReplaceExisting = 2;
      std::uint32_t result = 0;
      daemon->callMethod("RequestName")
          .onInterface("org.freedesktop.DBus")
          .withArguments(busName, kReplaceExisting)
          .storeResultsTo(result);
      if (result != 1 && result != 4) return 0;  // queued/failed: activation retries later
    } else {
      bool owned = false;
      daemon->callMethod("NameHasOwner")
          .onInterface("org.freedesktop.DBus")
          .withArguments(busName)
          .storeResultsTo(owned);
      if (owned) return 0;  // Current default already serves the name.
      bus->requestName(sdbus::ServiceName{busName});
    }
  } catch (const std::exception& e) {
    // Name owned or request failed: exit quietly; D-Bus activates us again
    // once the owner releases the name.
    std::cerr << "[filemanager1] name unavailable, exiting: " << e.what() << '\n';
    return 0;
  }
  auto object = sdbus::createObject(*bus, sdbus::ObjectPath{kObjectPath});
  object
      ->addVTable(sdbus::registerMethod("ShowFolders")
                      .withInputParamNames("URIs", "StartupId")
                      .implementedAs([](const std::vector<std::string>& uris, const std::string& /*startupId*/) {
                        try {
                          handle_show_folders(uris);
                        } catch (const std::exception& e) {
                          std::cerr << "[filemanager1] ShowFolders failed: " << e.what() << '\n';
                        }
                      }),
                  sdbus::registerMethod("ShowItems")
                      .withInputParamNames("URIs", "StartupId")
                      .implementedAs([](const std::vector<std::string>& uris, const std::string& /*startupId*/) {
                        try {
                          handle_show_items(uris);
                        } catch (const std::exception& e) {
                          std::cerr << "[filemanager1] ShowItems failed: " << e.what() << '\n';
                        }
                      }),
                  sdbus::registerMethod("ShowItemProperties")
                      .withInputParamNames("URIs", "StartupId")
                      .implementedAs([](const std::vector<std::string>& uris, const std::string& /*startupId*/) {
                        try {
                          // No CLI properties dialog exists across managers;
                          // revealing the parent folder is the sane fallback.
                          handle_show_items(uris);
                        } catch (const std::exception& e) {
                          std::cerr << "[filemanager1] ShowItemProperties failed: " << e.what() << '\n';
                        }
                      }))
      .forInterface(kInterface);
  bus->enterEventLoop();
  return 0;
}
