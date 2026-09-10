#pragma once

#include <array>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"

namespace eh::shell::shared {

inline int open_state_inotify() {
  const std::string stateDir = eh::shell::paths::state_home_dir() + "/event-horizon";
  (void)mkdir(stateDir.c_str(), 0755);

  int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (fd < 0) return -1;

  inotify_add_watch(fd, stateDir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);

  // Watch all per-component config directories.
  const std::string_view comps[] = {
    "general", "shell", "desktop", "autostart", "dock", "taskbar",
    "appearance", "wallpaper", "notifications", "keyboard", "audio",
    "power", "nightlight", "time", "default_apps", "mpris",
    "widgets", "idle", "file_browser", "live_wallpaper", "launchpad"
  };
  for (auto c : comps) {
    const std::string d = stateDir + "/" + std::string(c);
    (void)mkdir(d.c_str(), 0755);
    inotify_add_watch(fd, d.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
  }

  const std::string declarativeDir = eh::config::declarative_config_dir();
  if (!declarativeDir.empty())
    inotify_add_watch(fd, declarativeDir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);

  if (const char* home = std::getenv("HOME")) {
    inotify_add_watch(fd, (std::string(home) + "/.config").c_str(), IN_CLOSE_WRITE);
    inotify_add_watch(fd, (std::string(home) + "/.config/dconf").c_str(), IN_CLOSE_WRITE);
    inotify_add_watch(fd, (std::string(home) + "/.config/gtk-4.0").c_str(), IN_CLOSE_WRITE);
    inotify_add_watch(fd, (std::string(home) + "/.config/gtk-3.0").c_str(), IN_CLOSE_WRITE);
  }

  return fd;
}

template<typename Fn>
requires std::invocable<Fn&>
inline void drain_inotify(int fd, Fn&& on_event) {
  std::array<char, 4096> buf;
  while (true) {
    const ssize_t n = read(fd, buf.data(), buf.size());
    if (n <= 0) break;
    size_t off = 0;
    while (off < static_cast<size_t>(n)) {
      auto* ev = reinterpret_cast<const struct inotify_event*>(buf.data() + off);
      off += sizeof(struct inotify_event) + ev->len;

      const std::string_view name(ev->name, ev->len > 0 ? ev->len - 1 : 0);
      const bool isToml = name.ends_with(".toml");
      const bool isExtConfig = name == "kdeglobals" || name == "user";
      if (!isToml && !isExtConfig) continue;

      on_event();
      break;
    }
  }
}

}
