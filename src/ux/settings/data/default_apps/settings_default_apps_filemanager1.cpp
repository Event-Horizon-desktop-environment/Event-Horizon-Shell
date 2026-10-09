#include "ux/settings/data/default_apps/settings_default_apps_filemanager1.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace eh::settings::default_apps::filemanager1 {
namespace {

constexpr const char* kBusName = "org.freedesktop.FileManager1";
constexpr const char* kProxyBinary = "horizon-filemanager1";
constexpr const char* kManagedMarker = "# Event Horizon managed";

const char* xdg_env(const char* name) {
  const char* v = std::getenv(name);
  return (v && v[0]) ? v : nullptr;
}

std::string home_dir() {
  if (const char* h = xdg_env("HOME")) return h;
  return "/tmp";
}

std::string data_home() {
  if (const char* x = xdg_env("XDG_DATA_HOME")) return x;
  return home_dir() + "/.local/share";
}

std::vector<std::string> data_dirs() {
  std::vector<std::string> out;
  out.push_back(data_home());
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
  out.emplace_back("/usr/share");
  out.emplace_back("/usr/local/share");
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

std::string user_override_path() {
  return data_home() + "/dbus-1/services/" + kBusName + ".service";
}

[[nodiscard]] std::string trim_ws(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;
  if (a > 0) s.erase(0, a);
  return s;
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

[[nodiscard]] std::string exec_basename(const std::string& execLine) {
  // First token of Exec, honoring simple quoting, then basename.
  std::string tok;
  bool inSingle = false;
  bool inDouble = false;
  for (size_t i = 0; i < execLine.size(); ++i) {
    const char c = execLine[i];
    if (inSingle) {
      if (c == '\'') inSingle = false;
      else tok += c;
    } else if (inDouble) {
      if (c == '"') inDouble = false;
      else if (c == '\\' && i + 1 < execLine.size()) tok += execLine[++i];
      else tok += c;
    } else {
      if (c == ' ' || c == '\t') break;
      if (c == '\'') inSingle = true;
      else if (c == '"') inDouble = true;
      else if (c == '\\' && i + 1 < execLine.size()) tok += execLine[++i];
      else tok += c;
    }
  }
  if (const size_t sl = tok.rfind('/'); sl != std::string::npos) tok.erase(0, sl + 1);
  return tok;
}

// Exec= first-token basename of a .desktop id, or {} when not found.
[[nodiscard]] std::string desktop_exec_program(const std::string& desktop_id) {
  for (const std::string& dir : data_dirs()) {
    const fs::path p = fs::path(dir) / "applications" / desktop_id;
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) continue;
    std::ifstream f(p);
    if (!f) continue;
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
      if (line.rfind("Exec=", 0) == 0) {
        std::string v = line.substr(5);
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(v.begin());
        if (!v.empty()) return exec_basename(v);
      }
    }
  }
  return {};
}

// True when some installed (non-override) D-Bus service already activates the
// chosen manager for org.freedesktop.FileManager1 — native handling wins and
// no proxy override is needed.
[[nodiscard]] bool provider_is_native(const std::string& program) {
  if (program.empty()) return false;
  const std::string ours = user_override_path();
  for (const std::string& dir : data_dirs()) {
    const fs::path services = fs::path(dir) / "dbus-1" / "services";
    std::error_code ec;
    if (!fs::is_directory(services, ec)) continue;
    for (const auto& rec : fs::directory_iterator(services, ec)) {
      if (rec.path().extension() != ".service") continue;
      if (rec.path().string() == ours) continue;
      std::ifstream f(rec.path());
      if (!f) continue;
      std::string line;
      bool isOurs = false;
      std::string exec;
      bool inService = false;
      while (std::getline(f, line)) {
        std::string t = trim_ws(line);
        if (t.empty() || t[0] == '#') continue;
        if (!t.empty() && t.front() == '[') {
          inService = (t == "[D-BUS Service]");
          continue;
        }
        if (!inService) continue;
        if (t.rfind("Name=", 0) == 0) {
          if (trim_ws(t.substr(5)) == kBusName) isOurs = true;
        } else if (t.rfind("Exec=", 0) == 0 && exec.empty()) {
          exec = trim_ws(t.substr(5));
        }
      }
      if (isOurs && !exec.empty() && exec_basename(exec) == program) return true;
    }
  }
  return false;
}

[[nodiscard]] std::string proxy_binary_path() {
  // Test hook: point at a fake proxy binary without touching the real one.
  if (const char* e = xdg_env("EH_FILEMANAGER1_PROXY_BIN"); e && e[0]) return e;
  char self[4096] = {0};
  const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n > 0) {
    fs::path p(std::string(self, static_cast<size_t>(n)));
    p = p.parent_path() / kProxyBinary;
    std::error_code ec;
    if (fs::is_regular_file(p, ec) && ::access(p.c_str(), X_OK) == 0) return p.string();
  }
  // Installed-layout fallback: resolve via PATH.
  FILE* q = popen(("command -v " + std::string(kProxyBinary) + " 2>/dev/null").c_str(), "r");
  if (q) {
    char buf[1024] = {0};
    std::string out;
    if (fgets(buf, sizeof(buf), q)) out = trim_ws(buf);
    pclose(q);
    if (!out.empty() && ::access(out.c_str(), X_OK) == 0) return out;
  }
  return {};
}

[[nodiscard]] std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void remove_if_managed(const std::string& path, Transition* out) {
  const std::string cur = read_file(path);
  if (cur.empty()) return;
  if (cur.find(kManagedMarker) == std::string::npos) {
    std::cerr << "[default-apps-apply] FileManager1 override is foreign, leaving it alone: " << path << '\n';
    return;
  }
  std::error_code ec;
  fs::remove(path, ec);
  if (!ec) {
    std::cerr << "[default-apps-apply] FileManager1 override removed (system provider restored)\n";
    if (out) *out = Transition::kRemoved;
  }
}

// Fire-and-forget detached spawn (double-fork, stdio on /dev/null).
void spawn_detached_argv(const std::vector<std::string>& argv) {
  if (argv.empty()) return;
  const pid_t first = fork();
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
  const int devNull = open("/dev/null", O_RDWR);
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
  _exit(127);
}

}  // namespace

Transition sync_filemanager1_override(const std::string& desktop_id) {
  Transition transition = Transition::kUnchanged;
  const std::string path = user_override_path();
  if (trim_ws(desktop_id).empty()) {
    remove_if_managed(path, &transition);
    return transition;
  }
  const std::string id = normalize_desktop_id(desktop_id);
  const std::string program = desktop_exec_program(id);
  if (!program.empty() && provider_is_native(program)) {
    // The chosen manager owns FileManager1 itself (e.g. otter-files):
    // direct activation is native and keeps --select/property support.
    remove_if_managed(path, &transition);
    return transition;
  }
  const std::string proxy = proxy_binary_path();
  if (proxy.empty()) {
    std::cerr << "[default-apps-apply] " << kProxyBinary
              << " not found next to settings or on PATH; FileManager1 routing left unchanged\n";
    return transition;
  }
  const std::string content = std::string(kManagedMarker) + " — do not edit; managed by Default Apps.\n"
                              "[D-BUS Service]\n"
                              "Name=" +
                              kBusName + "\nExec=" + proxy + "\n";
  if (read_file(path) == content) return transition;
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  if (ec) {
    std::cerr << "[default-apps-apply] cannot create dbus services dir: " << ec.message() << '\n';
    return transition;
  }
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      std::cerr << "[default-apps-apply] cannot write FileManager1 override\n";
      return transition;
    }
    f << content;
    f.flush();
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    std::cerr << "[default-apps-apply] cannot install FileManager1 override: " << ec.message() << '\n';
    return transition;
  }
  std::cerr << "[default-apps-apply] FileManager1 now routes to " << id << " via " << proxy << '\n';
  return Transition::kInstalled;
}

void apply_filemanager1_transition(Transition t) {
  if (t == Transition::kInstalled) {
    // Take over the bus name right away so the new default applies to this
    // session (no logout needed). The proxy replaces any current owner and
    // exits quietly if the name cannot be taken.
    const std::string proxy = proxy_binary_path();
    if (!proxy.empty()) spawn_detached_argv({proxy, "--replace"});
  } else if (t == Transition::kRemoved) {
    // Our proxy is headless and stateless: stop it so the native/system
    // provider activates on the next request. Best effort; ignore failures.
    spawn_detached_argv({"pkill", "-x", kProxyBinary});
  }
}

}
