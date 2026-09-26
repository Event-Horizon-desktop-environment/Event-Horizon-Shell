#include "services/autostart/autostart_service.hpp"

#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"  // expand_desktop_exec_tokens, spawn_sh_lc_detached
#include "desktop_shell/desktop/entries/desktop_entries.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace eh::autostart {
namespace {

using eh::shell::str::trim;

static bool g_debug = false;

bool env_true(const char* name) {
  if (const char* v = std::getenv(name)) {
    std::string s = v;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }
  return false;
}

FILE* autostart_log_file() {
  const char* home = std::getenv("HOME");
  const std::string dir = std::string(home && *home ? home : "/tmp") + "/EH-logs";
  ::mkdir(dir.c_str(), 0755);
  return fopen((dir + "/autostart.log").c_str(), "a");
}

void autostart_log(const char* fmt, ...) {
  if (!g_debug && !env_true("EH_AUTOSTART_DEBUG")) return;
  char line[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  std::size_t len = std::strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
  if (FILE* f = autostart_log_file()) {
    std::fwrite(line, 1, len, f);
    std::fwrite("\n", 1, 1, f);
    std::fflush(f);
    std::fclose(f);
  }
}

std::vector<std::string> xdg_autostart_dirs() {
  std::vector<std::string> dirs;

  // User dir has highest precedence
  if (const char* cfg = std::getenv("XDG_CONFIG_HOME")) {
    if (*cfg) dirs.push_back(std::string(cfg) + "/autostart");
  } else if (const char* home = std::getenv("HOME")) {
    dirs.push_back(std::string(home) + "/.config/autostart");
  }

  // System dirs (lower precedence)
  if (const char* cfgDirs = std::getenv("XDG_CONFIG_DIRS")) {
    std::string s(cfgDirs);
    size_t pos = 0;
    while (true) {
      size_t next = s.find(':', pos);
      std::string d = (next == std::string::npos) ? s.substr(pos) : s.substr(pos, next - pos);
      d = trim(d);
      if (!d.empty()) dirs.push_back(d + "/autostart");
      if (next == std::string::npos) break;
      pos = next + 1;
    }
  } else {
    dirs.push_back("/etc/xdg/autostart");
  }

  return dirs;
}

std::string current_desktop_env() {
  // Values from the desktop-environment session variable.
  // We also accept "EventHorizon" if someone sets it.
  if (const char* v = std::getenv("XDG_CURRENT_DESKTOP")) {
    if (*v) return v;
  }
  if (const char* v = std::getenv("DESKTOP_SESSION")) {
    if (*v) return v;
  }
  return {};
}

bool matches_desktop(const std::vector<std::string>& list, const std::string& de) {
  if (list.empty()) return false;
  if (de.empty()) return false;
  std::string deLower = de;
  for (auto& c : deLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  for (const auto& item : list) {
    std::string i = item;
    for (auto& c : i) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (i == deLower) return true;
  }
  return false;
}

bool matches_any_desktop(const std::vector<std::string>& list, const std::string& desktops) {
  std::string::size_type pos = 0;
  while (pos <= desktops.size()) {
    const auto next = desktops.find(':', pos);
    const std::string part =
        (next == std::string::npos) ? desktops.substr(pos) : desktops.substr(pos, next - pos);
    if (matches_desktop(list, part)) return true;
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return false;
}

bool file_executable(const std::string& path) {
  return access(path.c_str(), X_OK) == 0;
}

std::string basename_no_ext(const std::string& path) {
  fs::path p(path);
  return p.stem().string();
}

bool valid_stem(const std::string& stem) {
  if (stem.empty() || stem.size() > 128) return false;
  if (stem == "." || stem == "..") return false;
  return stem.find('/') == std::string::npos && stem.find('\\') == std::string::npos;
}

std::string flat_desktop_value(std::string v) {
  for (auto& c : v) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return v;
}

} // anonymous namespace

std::vector<AutostartEntry> scan_autostart_entries() {
  g_debug = env_true("EH_AUTOSTART_DEBUG");

  std::vector<AutostartEntry> out;
  std::unordered_map<std::string, size_t> seen; // basename -> index in out (for shadowing)

  auto dirs = xdg_autostart_dirs();

  autostart_log("scanning %zu autostart dirs", dirs.size());

  for (const auto& dir : dirs) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;

    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      auto p = entry.path();
      if (p.extension() != ".desktop") continue;

      const std::string stem = basename_no_ext(p.string());
      const std::string stemLower = [&] {
        std::string l = stem;
        for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return l;
      }();

      auto infoOpt = read_desktop_entry_info(p.string());
      if (!infoOpt) continue;

      AutostartEntry ae;
      ae.desktopPath = p.string();
      ae.info = std::move(*infoOpt);
      ae.delaySec = ae.info.autostartDelaySec;

      // Apply basic enabled logic from the file itself
      bool enabled = !ae.info.hidden && ae.info.autostartEnabled;
      ae.effectiveEnabled = enabled;

      auto it = seen.find(stemLower);
      if (it == seen.end()) {
        seen[stemLower] = out.size();
        out.push_back(std::move(ae));
      }
    }
  }

  autostart_log("found %zu autostart .desktop candidates", out.size());
  return out;
}

bool should_show_in_current_desktop(const AutostartEntry& e) {
  const std::string de = current_desktop_env();

  if (matches_any_desktop(e.info.notShowIn, de)) return false;

  if (!e.info.onlyShowIn.empty()) {
    return matches_any_desktop(e.info.onlyShowIn, de);
  }
  return true;
}

bool try_exec_ok(const AutostartEntry& e) {
  if (e.info.tryExec.empty()) return true;

  // TryExec can be a bare name or absolute path
  const std::string& t = e.info.tryExec;
  if (t.find('/') != std::string::npos) {
    return file_executable(t);
  }
  // Search PATH
  if (const char* path = std::getenv("PATH")) {
    std::string p(path);
    size_t pos = 0;
    while (true) {
      size_t next = p.find(':', pos);
      std::string d = (next == std::string::npos) ? p.substr(pos) : p.substr(pos, next - pos);
      if (!d.empty()) {
        std::string full = d + "/" + t;
        if (file_executable(full)) return true;
      }
      if (next == std::string::npos) break;
      pos = next + 1;
    }
  }
  return false;
}

void launch_autostart_entry(const AutostartEntry& e) {
  if (e.desktopPath.empty()) return;

  const fs::path p(e.desktopPath);
  const std::string stem = p.stem().string();
  const std::string abs = e.desktopPath;

  auto shq = [](const std::string& s) -> std::string {
    std::string o = "'";
    for (char c : s) {
      if (c == '\'') o += "'\\''";
      else o += c;
    }
    o += '\'';
    return o;
  };

  // Prefer the desktop file launchers — they handle TryExec, Terminal, etc. better
  std::string script =
      "(command -v gio >/dev/null 2>&1 && gio launch " + shq(abs) + ") || "
      "(command -v gtk-launch >/dev/null 2>&1 && gtk-launch " + shq(stem) + ")";

  // Fallback using our expanded exec (in case the launcher utility isn't present)
  std::string execLine = e.info.exec;
  if (!execLine.empty()) {
    std::string name = e.info.name.empty() ? stem : e.info.name;
    const std::string expanded = eh::shell::desktop::xdg::expand_desktop_exec_tokens(execLine, abs, name);
    if (!expanded.empty()) {
      std::string inner = expanded;
      if (e.info.terminal) {
        inner = "x-terminal-emulator -e /bin/sh -c " + shq(expanded);
      }
      script += " || /bin/sh -c " + shq(inner);
    }
  }

  autostart_log("launching: %s (delay=%ds)", stem.c_str(), e.delaySec);

  // Fire and forget (uses the project's standard detached launcher)
  eh::shell::desktop::xdg::spawn_sh_lc_detached(script);
}

void launch_all_autostart(const std::vector<AutostartEntry>& entries) {
  const std::string de = current_desktop_env();
  autostart_log("launch_all_autostart: de='%s', total candidates=%zu", de.c_str(), entries.size());

  for (const auto& e : entries) {
    if (!e.effectiveEnabled) {
      autostart_log("  skip (disabled): %s", basename_no_ext(e.desktopPath).c_str());
      continue;
    }
    if (!should_show_in_current_desktop(e)) {
      autostart_log("  skip (OnlyShowIn/NotShowIn): %s", basename_no_ext(e.desktopPath).c_str());
      continue;
    }
    if (!try_exec_ok(e)) {
      autostart_log("  skip (TryExec failed): %s", basename_no_ext(e.desktopPath).c_str());
      continue;
    }

    if (e.delaySec > 0) {
      // Best effort non-blocking delay
      std::thread([e]() {
        std::this_thread::sleep_for(std::chrono::seconds(e.delaySec));
        launch_autostart_entry(e);
      }).detach();
      autostart_log("  queued with delay %ds: %s", e.delaySec, basename_no_ext(e.desktopPath).c_str());
    } else {
      launch_autostart_entry(e);
    }
  }
}

void invalidate_autostart_cache() {
  // Currently no persistent cache in this implementation.
  // Hook point for future optimization (e.g. mtime based).
}

// UI helpers.

namespace {

std::string user_autostart_dir() {
  if (const char* cfg = std::getenv("XDG_CONFIG_HOME")) {
    if (*cfg) return std::string(cfg) + "/autostart";
  }
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/.config/autostart";
  }
  return {};
}

bool ensure_dir(const std::string& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  return !ec;
}

std::string stem_from_path(const std::string& p) {
  fs::path fp(p);
  return fp.stem().string();
}

} // anon

std::vector<AutostartUiEntry> get_autostart_ui_entries() {
  auto raw = scan_autostart_entries();
  std::vector<AutostartUiEntry> ui;
  ui.reserve(raw.size());

  const std::string userDir = user_autostart_dir();

  for (auto& e : raw) {
    if (!should_show_in_current_desktop(e)) continue;
    // We still show disabled ones so user can re-enable them.

    AutostartUiEntry u;
    u.stem = stem_from_path(e.desktopPath);
    u.name = e.info.name.empty() ? u.stem : e.info.name;
    u.icon = e.info.icon;
    u.exec = e.info.exec;
    u.runInTerminal = e.info.terminal;
    u.desktopPath = e.desktopPath;
    u.enabled = e.effectiveEnabled;
    u.delaySec = e.delaySec;
    u.hidden = e.info.hidden;

    // Is this (or its override) in the user dir?
    std::string userPath = userDir + "/" + u.stem + ".desktop";
    if (fs::exists(userPath)) {
      u.isUserOverride = true;
      // Re-read the user file to get the actual current enabled state for display
      if (auto userInfo = read_desktop_entry_info(userPath)) {
        bool userEnabled = !userInfo->hidden && userInfo->autostartEnabled;
        u.enabled = userEnabled;
        if (!userInfo->name.empty()) u.name = userInfo->name;
        if (!userInfo->icon.empty()) u.icon = userInfo->icon;
        if (!userInfo->exec.empty()) u.exec = userInfo->exec;
        u.runInTerminal = userInfo->terminal;
        u.delaySec = userInfo->autostartDelaySec;
      }
    } else {
      u.isUserOverride = false;
    }

    ui.push_back(std::move(u));
  }

  // Sort by name
  std::sort(ui.begin(), ui.end(), [](const auto& a, const auto& b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });

  return ui;
}

void emit_preserved_autostart_keys(std::ofstream& f, const DesktopEntryInfo& base) {
  if (!base.tryExec.empty()) f << "TryExec=" << flat_desktop_value(base.tryExec) << "\n";
  if (!base.onlyShowIn.empty()) {
    f << "OnlyShowIn=";
    for (const auto& v : base.onlyShowIn) f << flat_desktop_value(v) << ";";
    f << "\n";
  }
  if (!base.notShowIn.empty()) {
    f << "NotShowIn=";
    for (const auto& v : base.notShowIn) f << flat_desktop_value(v) << ";";
    f << "\n";
  }
  if (!base.startup_wm_class.empty()) f << "StartupWMClass=" << flat_desktop_value(base.startup_wm_class) << "\n";
  if (base.dbus_activatable) f << "DBusActivatable=true\n";
  if (base.noDisplay) f << "NoDisplay=true\n";
}

bool write_autostart_override(const std::string& stem, const DesktopEntryInfo& baseInfo, bool enabled) {
  if (!valid_stem(stem)) return false;
  const std::string dir = user_autostart_dir();
  if (!ensure_dir(dir)) return false;

  const std::string path = dir + "/" + stem + ".desktop";

  std::ofstream f(path);
  if (!f.is_open()) return false;

  f << "[Desktop Entry]\n";
  f << "Type=Application\n";
  f << "Name=" << flat_desktop_value(baseInfo.name.empty() ? stem : baseInfo.name) << "\n";
  if (!baseInfo.icon.empty()) f << "Icon=" << flat_desktop_value(baseInfo.icon) << "\n";
  if (!baseInfo.exec.empty()) f << "Exec=" << flat_desktop_value(baseInfo.exec) << "\n";
  if (baseInfo.terminal) f << "Terminal=true\n";
  emit_preserved_autostart_keys(f, baseInfo);

  f << "Hidden=" << (enabled ? "false" : "true") << "\n";
  f << "X-GNOME-Autostart-enabled=" << (enabled ? "true" : "false") << "\n";
  if (baseInfo.autostartDelaySec > 0) {
    f << "X-GNOME-Autostart-Delay=" << baseInfo.autostartDelaySec << "\n";
  }

  f.close();
  return true;
}

bool set_autostart_enabled(const std::string& stem, bool enabled) {
  if (!valid_stem(stem)) return false;

  // Find a base entry (prefer system or any existing scan)
  auto all = scan_autostart_entries();
  DesktopEntryInfo base{};
  bool found = false;
  for (const auto& e : all) {
    if (stem_from_path(e.desktopPath) == stem) {
      base = e.info;
      found = true;
      break;
    }
  }
  if (!found) {
    // Still allow creating a new one? For now require a base .desktop exists somewhere.
    // User can use "Add" later for custom commands.
    return false;
  }

  return write_autostart_override(stem, base, enabled);
}

bool remove_autostart_override(const std::string& stem) {
  if (!valid_stem(stem)) return true;
  const std::string dir = user_autostart_dir();
  const std::string path = dir + "/" + stem + ".desktop";
  std::error_code ec;
  if (fs::exists(path)) {
    return fs::remove(path, ec) && !ec;
  }
  return true; // nothing to remove is success
}

bool create_autostart_entry(const std::string& stem, const std::string& name,
                            const std::string& exec, const std::string& icon,
                            int delaySec, bool runInTerminal) {
  if (!valid_stem(stem) || name.empty() || exec.empty()) return false;

  const std::string dir = user_autostart_dir();
  if (!ensure_dir(dir)) return false;

  const std::string path = dir + "/" + stem + ".desktop";

  std::ofstream f(path);
  if (!f.is_open()) return false;

  f << "[Desktop Entry]\n";
  f << "Type=Application\n";
  f << "Name=" << flat_desktop_value(name) << "\n";
  if (!icon.empty()) f << "Icon=" << flat_desktop_value(icon) << "\n";
  f << "Exec=" << flat_desktop_value(exec) << "\n";
  if (runInTerminal) f << "Terminal=true\n";
  f << "Hidden=false\n";
  f << "X-GNOME-Autostart-enabled=true\n";
  if (delaySec > 0) {
    f << "X-GNOME-Autostart-Delay=" << delaySec << "\n";
  }
  f << "X-EventHorizon-Custom=true\n";

  f.close();
  return true;
}

bool edit_autostart_entry(const std::string& stem, const std::string& name,
                          const std::string& exec, const std::string& icon,
                          int delaySec, bool runInTerminal) {
  if (!valid_stem(stem)) return false;

  const std::string dir = user_autostart_dir();
  const std::string path = dir + "/" + stem + ".desktop";

  std::error_code ec;
  if (!fs::exists(path)) return false;
  DesktopEntryInfo preserved{};
  if (auto cur = read_desktop_entry_info(path)) preserved = std::move(*cur);

  std::ofstream f(path);
  if (!f.is_open()) return false;

  f << "[Desktop Entry]\n";
  f << "Type=Application\n";
  f << "Name=" << flat_desktop_value(name.empty() ? stem : name) << "\n";
  if (!icon.empty()) f << "Icon=" << flat_desktop_value(icon) << "\n";
  if (!exec.empty()) f << "Exec=" << flat_desktop_value(exec) << "\n";
  if (runInTerminal) f << "Terminal=true\n";
  emit_preserved_autostart_keys(f, preserved);
  f << "Hidden=false\n";
  f << "X-GNOME-Autostart-enabled=true\n";
  if (delaySec > 0) {
    f << "X-GNOME-Autostart-Delay=" << delaySec << "\n";
  }
  f << "X-EventHorizon-Custom=true\n";

  f.close();
  return true;
}

std::vector<InstalledApp> scan_installed_apps() {
  std::vector<InstalledApp> out;
  std::unordered_set<std::string> seen;

  auto scan_dir = [&](const std::string& dirPath) {
    std::error_code ec;
    if (!fs::is_directory(dirPath, ec)) return;
    for (const auto& entry : fs::directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec)) {
      if (!entry.is_regular_file(ec)) continue;
      auto p = entry.path();
      if (p.extension() != ".desktop") continue;

      const std::string path = p.string();
      auto info = read_desktop_entry_info(path);
      if (!info) continue;
      if (info->type != "Application" && info->type != "application") continue;
      if (info->noDisplay) continue;
      if (info->name.empty()) continue;
      if (info->exec.empty() && !info->dbus_activatable) continue;

      const std::string fileId = p.filename().string();
      if (seen.count(fileId)) continue;
      seen.insert(fileId);

      InstalledApp a;
      a.name = info->name;
      a.icon = info->icon;
      a.exec = info->exec;
      a.desktopPath = path;
      out.push_back(std::move(a));
    }
  };

  // User local apps
  if (const char* home = std::getenv("HOME")) {
    scan_dir(std::string(home) + "/.local/share/applications");
  }
  // System apps
  scan_dir("/usr/share/applications");
  scan_dir("/usr/local/share/applications");

  // Also scan XDG_DATA_DIRS
  if (const char* dataDirs = std::getenv("XDG_DATA_DIRS")) {
    std::string s(dataDirs);
    size_t pos = 0;
    while (true) {
      size_t next = s.find(':', pos);
      std::string d = (next == std::string::npos) ? s.substr(pos) : s.substr(pos, next - pos);
      d = trim(d);
      if (!d.empty()) scan_dir(d + "/applications");
      if (next == std::string::npos) break;
      pos = next + 1;
    }
  }

  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });

  return out;
}

} // namespace eh::autostart
