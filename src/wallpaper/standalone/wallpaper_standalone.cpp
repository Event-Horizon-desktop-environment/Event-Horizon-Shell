#define _GNU_SOURCE 1
#include "wallpaper/standalone/wallpaper_standalone.hpp"

#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/core/wallpaper.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "services/process/parent_death_guard.hpp"
#include "wl/core/connection.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include <poll.h>
#include <unistd.h>

namespace eh::wallpaper {

namespace {

volatile sig_atomic_t g_wallpaper_signal = 0;

void wallpaper_standalone_signal_handler(int) {
  g_wallpaper_signal = 1;
}

void wallpaper_apply_from_snapshot(WallpaperRenderer& r) {
  const auto sc = eh::config::shell_config_snapshot_skip_matugen();
  if (sc.wallpaperImage.empty()) return;
  if (r.enabled == sc.wallpaperEnabled && r.currentImage == sc.wallpaperImage &&
      r.currentMode == sc.wallpaperMode)
    return;
  wallpaper_renderer_apply(r, sc.wallpaperEnabled, sc.wallpaperImage, sc.wallpaperMode);
  wallpaper_renderer_poll_decode(r);
}

}  // namespace

int run_wallpaper_standalone() {
  eh::proc::install_parent_death_guard();

  WallpaperRenderer r;
  if (!wallpaper_renderer_init(r)) {
    std::cerr << "[horizon-wallpaper] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  for (wl_output* out : r.conn->outputs()) wallpaper_renderer_add_output(r, out);

  eh::config::shell_config_reload_from_disk_now(true);
  wallpaper_apply_from_snapshot(r);

  write_pid_file(::getpid());

  g_wallpaper_signal = 0;
  {
    struct sigaction sa {};
    sa.sa_handler = wallpaper_standalone_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
  }

  const int dpyFd = wl_display_get_fd(r.display);

  eh::ipc::IpcClient ipc;
  bool ipcOk = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&r](std::string topic, std::string /*payload*/, std::vector<int> /*fds*/) {
        if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          wallpaper_apply_from_snapshot(r);
        }
      });
      ipcOk = ipc.subscribe("config.applied");
    }
  }

  std::cout << "[horizon-wallpaper] running pid=" << ::getpid()
            << " outputs=" << r.layers.size() << " ipc=" << (ipcOk ? 1 : 0) << "\n";

  constexpr int kWallpaperPollMs = 30'000;
  while (g_wallpaper_signal == 0) {
    pollfd pf[2]{};
    int n = 0;
    int ipcIdx = -1;
    pf[n].fd = dpyFd;
    pf[n].events = POLLIN | POLLERR | POLLHUP;
    ++n;
    if (ipcOk) {
      // Re-resolve the fd each pass: when the connection drops, IpcClient closes
      // its fd internally, and polling a stale captured number would spin on
      // POLLNVAL.
      int cfd = ipc.fd();
      if (cfd < 0) {
        static unsigned reconnect_wait = 0;
        if (++reconnect_wait >= 2048) {
          reconnect_wait = 0;
          cfd = ipc.connect(eh::ipc::default_socket_path(), 1);
        }
      }
      if (cfd >= 0) {
        pf[n].fd = cfd;
        pf[n].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
        ipcIdx = n;
        ++n;
      }
    }
    const int pr = poll(pf, static_cast<nfds_t>(n), kWallpaperPollMs);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_wallpaper_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_wallpaper_signal != 0) break;
    if ((pf[0].revents & (POLLERR | POLLHUP)) != 0) break;
    if (ipcIdx >= 0 && (pf[ipcIdx].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      ipcOk = false;
      ipcIdx = -1;
    }
    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(r.display) < 0) break;
    } else {
      (void)wl_display_flush(r.display);
    }
    // Decode/decompress spikes leave dirty heap behind; hand it back to the OS.
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
    if (ipcIdx >= 0 && (pf[ipcIdx].revents & POLLIN)) {
      ipc.on_fd_ready(pf[ipcIdx].fd);
    }
  }

  wallpaper_renderer_clear(r);
  unlink_pid_file();
  std::cout << "[horizon-wallpaper] exit\n";
  return 0;
}

}
