#define _GNU_SOURCE 1
#include "desktop_shell/desktop/standalone/desktop_standalone.hpp"

#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/spawn/desktop_spawn.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widgets_preferences.hpp"
#include "desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.hpp"

#include "services/process/parent_death_guard.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "desktop_shell/shared/core/config_watch.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "services/mpris/mpris_player.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_serialize.hpp"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::desktop {

namespace {

volatile sig_atomic_t g_desktop_signal = 0;

void desktop_standalone_signal_handler(int) {
  g_desktop_signal = 1;
}

// Load the desktop widget configs from the prefs file, falling back to the
// settings store when empty — parity with the supervisor's old in-process load
// (pre-split unified_shell_session.cpp).
std::vector<DesktopWidgetConfig> load_widget_configs() {
  auto wc = desktop_widgets_prefs_load();
  if (wc.empty()) {
    const auto s = ::load_settings();
    wc = s.desktopWidgets;
    if (!wc.empty()) desktop_widgets_prefs_save(wc);
  }
  return wc;
}

// The widget layout prefs file lives in the per-user config dir, outside the
// aggregated state mtime — fold its mtime in so add/remove/drag changes to
// desktop_widgets_layout.txt are detected by the reload guard. Same for the
// toggled-off slot ids in the state-dir desktop_widgets.toml.
std::optional<timespec> desktop_config_mtime() {
  auto best = eh::config::aggregate_config_source_mtime();
  const auto bump = [&best](const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
      const timespec ts{st.st_mtim.tv_sec, st.st_mtim.tv_nsec};
      if (!best || ts.tv_sec > best->tv_sec || (ts.tv_sec == best->tv_sec && ts.tv_nsec > best->tv_nsec))
        best = ts;
    }
  };
  bump(eh::config::state_event_horizon_dir() + "/desktop_widgets.toml");
  std::string dir;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0])
    dir = std::string(xdg) + "/event-horizon";
  else if (const char* home = std::getenv("HOME"))
    dir = std::string(home) + "/.config/event-horizon";
  if (!dir.empty()) bump(dir + "/desktop_widgets_layout.txt");
  return best;
}

std::optional<timespec> g_desktop_config_mtime;

bool desktop_timespec_equal(const timespec& a, const timespec& b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

std::string desktop_widgets_config_dir() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0])
    return std::string(xdg) + "/event-horizon";
  if (const char* home = std::getenv("HOME")) return std::string(home) + "/.config/event-horizon";
  return {};
}

// Shared re-apply path used by both the config.applied bus broadcast (full
// reload — theme/color events) and the inotify watcher (light reload for
// settings + widget-layout edits). Always repaints: desktop_create_layers
// no-ops when output targets are unchanged, so the repaint must not be skipped.
void desktop_reapply_config(DesktopApp& app, bool full_reload) {
  if (!full_reload) {
    const auto mt = desktop_config_mtime();
    if (!mt) return;
    if (g_desktop_config_mtime && desktop_timespec_equal(*mt, *g_desktop_config_mtime)) return;
    g_desktop_config_mtime = mt;
    eh::config::shell_config_invalidate_light();
  } else {
    eh::config::shell_config_reload_from_disk_now();
    if (const auto mt = desktop_config_mtime()) g_desktop_config_mtime = mt;
  }
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  app.outputName = eh::shell::trim_output_assign(sc.desktopOutputName);
  app.iconCache.set_icon_theme(sc.dock.iconTheme);
  app.widgetHost.set_configs(load_widget_configs());
  app.widgetHost.set_disabled_slots(load_desktop_widget_disabled_slots());
  (void)desktop_create_layers(app);
  paint_all_layers(app);
  wl_display_flush(app.display);
}

// Drain the shared inotify fd. Unlike drain_inotify (which filters to .toml),
// the desktop widget layout prefs file is plain text, so every event is passed
// to the callback and the mtime guard decides whether a reload is needed.
void drain_desktop_inotify(int fd, const std::function<void()>& on_event) {
  std::array<char, 4096> buf;
  while (true) {
    const ssize_t n = read(fd, buf.data(), buf.size());
    if (n <= 0) break;
    size_t off = 0;
    while (off < static_cast<size_t>(n)) {
      auto* ev = reinterpret_cast<const struct inotify_event*>(buf.data() + off);
      off += sizeof(struct inotify_event) + ev->len;
      (void)ev;
      on_event();
      break;
    }
  }
}

}  // namespace

int run_desktop_standalone() {
  // Die with the supervisor even on hard kills (PDEATHSIG fires on parent exit,
  // so `pkill -9 EventHorizon` reaps us too).
  eh::proc::install_parent_death_guard();

  // Keep this process's own DockMpris a pure listener: the supervisor's dock
  // instance stays the single now-playing-notification emitter. Two emitters
  // would duplicate toasts for the same track.
  (void)::setenv("EH_MPRIS_NOTIFY", "0", 1);

  DesktopApp app;
  // Load config first: desktop_init_on_display → create_layers reads the config
  // snapshot to resolve the dock output (dockShowDock / dockHeight reserves).
  // Skip palette generation here — it runs async in the supervisor and arrives via the
  // config.applied broadcast; blocking on it delays Wayland connect at login.
  eh::config::shell_config_reload_from_disk_now(true);
  const auto sc0 = eh::config::shell_config_snapshot();
  app.outputName = eh::shell::trim_output_assign(sc0.desktopOutputName);
  app.iconCache.set_icon_theme(sc0.dock.iconTheme);
  if (!desktop_init_on_display(app)) {
    std::cerr << "[horizon-desktop] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  app.widgetHost.set_configs(load_widget_configs());
  app.widgetHost.set_disabled_slots(load_desktop_widget_disabled_slots());
  if (const auto mt = desktop_config_mtime()) g_desktop_config_mtime = mt;

  eh::mpris::DockMpris mpris;
  app.widgetHost.set_mpris(&mpris);

  const int tick_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (tick_fd >= 0) {
    itimerspec its{};
    its.it_interval.tv_sec = 1;
    its.it_value.tv_sec = 1;
    (void)timerfd_settime(tick_fd, 0, &its, nullptr);
  }
  const int anim_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (anim_fd >= 0) {
    itimerspec its{};
    its.it_interval.tv_nsec = 33'000'000;  // ~30 Hz
    its.it_value.tv_nsec = 33'000'000;
    (void)timerfd_settime(anim_fd, 0, &its, nullptr);
  }

  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app](std::string topic, std::string /*payload*/, std::vector<int> /*fds*/) {
        if (topic == "worldclock.settings") {
          world_clock_settings_open(app);
          return;
        }
        if (topic != "config.applied") return;
        desktop_reapply_config(app, true);
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("worldclock.settings");
    }
  }

  g_desktop_signal = 0;
  {
    struct sigaction sa {};
    sa.sa_handler = desktop_standalone_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
  }
  desktop_write_pid_file(::getpid());

  const int dpy_fd = wl_display_get_fd(app.display);
  const int mpris_bus_fd = mpris.poll_fd();
  const int mpris_event_fd = mpris.poll_event_fd();

  // Watch the settings state dirs (component tomls) plus the per-user config
  // dir where the desktop widget layout prefs file lives — the §0.10 design has
  // each child watch its own config, the supervisor does not broadcast edits.
  int settings_fd = eh::shell::shared::open_state_inotify();
  if (settings_fd >= 0) {
    const std::string cfgDir = desktop_widgets_config_dir();
    if (!cfgDir.empty())
      (void)inotify_add_watch(settings_fd, cfgDir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
  }

  std::cout << "[horizon-desktop] running pid=" << ::getpid() << " ipc=" << (ipc_ok ? 1 : 0)
            << " tick_fd=" << tick_fd << " anim_fd=" << anim_fd << " settings_fd=" << settings_fd << "\n";

  while (g_desktop_signal == 0) {
    pollfd pf[7]{};
    int n = 0;
    int tick_idx = -1;
    int anim_idx = -1;
    int ipc_idx = -1;
    int settings_idx = -1;
    int mb_idx = -1;
    int me_idx = -1;
    pf[n].fd = dpy_fd;
    pf[n].events = POLLIN | POLLERR | POLLHUP;
    ++n;
    if (tick_fd >= 0) {
      pf[n].fd = tick_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      tick_idx = n;
      ++n;
    }
    if (anim_fd >= 0) {
      pf[n].fd = anim_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      anim_idx = n;
      ++n;
    }
    int cfd = -1;
    if (ipc_ok) {
      // Re-resolve the fd every pass: a dropped connection closes it inside
      // IpcClient, and polling a stale captured number would spin on POLLNVAL.
      cfd = ipc.fd();
      if (cfd < 0) {
        static unsigned reconnect_wait = 0;
        if (++reconnect_wait >= 2048) {
          reconnect_wait = 0;
          cfd = ipc.connect(eh::ipc::default_socket_path(), 1);
        }
      }
      if (cfd >= 0) {
        pf[n].fd = cfd;
        pf[n].events = POLLIN | POLLERR | POLLHUP;
        ipc_idx = n;
        ++n;
      }
    }
    if (settings_fd >= 0) {
      pf[n].fd = settings_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      settings_idx = n;
      ++n;
    }
    if (mpris_bus_fd >= 0) {
      pf[n].fd = mpris_bus_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      mb_idx = n;
      ++n;
    }
    if (mpris_event_fd >= 0) {
      pf[n].fd = mpris_event_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      me_idx = n;
      ++n;
    }

    const int pr = poll(pf, static_cast<nfds_t>(n), -1);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_desktop_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_desktop_signal != 0) break;
    if ((pf[0].revents & (POLLERR | POLLHUP)) != 0) break;
    if (ipc_idx >= 0 && (pf[ipc_idx].revents & (POLLERR | POLLHUP)) != 0) {
      ipc_ok = false;
      ipc_idx = -1;
    }

    // Re-arm the animation timer on demand: widgets report the cadence they
    // need, so an idle desktop stops repainting at 30 Hz entirely.
    const auto apply_anim_interval = [&app, af = anim_fd]() {
      if (af < 0) return;
      itimerspec its{};
      if (const uint32_t ms = app.widgetHost.animIntervalMs(); ms > 0) {
        its.it_value.tv_nsec = static_cast<int64_t>(ms) * 1'000'000;
        its.it_interval = its.it_value;
      }
      (void)timerfd_settime(af, 0, &its, nullptr);
    };

    if (tick_idx >= 0 && (pf[tick_idx].revents & POLLIN)) {
      std::uint64_t exp = 0;
      (void)read(tick_fd, &exp, sizeof(exp));
      app.widgetHost.on_second_tick();
      paint_all_layers(app);
      wl_display_flush(app.display);
      apply_anim_interval();
    }
    if (anim_idx >= 0 && (pf[anim_idx].revents & POLLIN)) {
      std::uint64_t exp = 0;
      (void)read(anim_fd, &exp, sizeof(exp));
      paint_all_layers(app);
      wl_display_flush(app.display);
      apply_anim_interval();
    }

    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(app.display) < 0) break;
      desktop_icons_poll_deferred_rescan(app);
      desktop_mount_dialog_process_deferred_action(app);
    } else {
      (void)wl_display_flush(app.display);
    }

    if (mb_idx >= 0 && (pf[mb_idx].revents & POLLIN)) mpris.process_pending_events();
    if (me_idx >= 0 && (pf[me_idx].revents & POLLIN)) mpris.process_pending_events();
    if (ipc_idx >= 0 && (pf[ipc_idx].revents & POLLIN)) ipc.on_fd_ready(pf[ipc_idx].fd);
    // Return freed heap pages (icon pixbufs, widget layouts) to the OS.
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
    if (settings_idx >= 0 && (pf[settings_idx].revents & (POLLIN | POLLERR | POLLHUP)))
      drain_desktop_inotify(settings_fd, [&app]() { desktop_reapply_config(app, false); });
  }

  desktop_cleanup(app);
  desktop_unlink_pid_file();
  if (tick_fd >= 0) (void)close(tick_fd);
  if (anim_fd >= 0) (void)close(anim_fd);
  if (settings_fd >= 0) (void)close(settings_fd);
  std::cout << "[horizon-desktop] exit\n";
  return 0;
}

}
