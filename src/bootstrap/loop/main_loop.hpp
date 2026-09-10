#pragma once

#include <functional>
#include <poll.h>
#include <vector>

struct wl_display;

namespace eh::app {

struct MainLoopOps {

  wl_display* wayland_display = nullptr;

  int display_fd = -1;
  int inotify_fd = -1;
  int timer_fd = -1;
  int tray_fd = -1;

  int* tray_fd_live = nullptr;

  int weather_fd = -1;

  int notification_timer_fd = -1;

  std::function<void()> on_timer;
  std::function<void()> on_inotify;
  std::function<void()> on_tray;
  std::function<void()> on_weather;
  std::function<void()> on_notification_timer;

  // GLib-based subsystems (the polkit agent) feed poll fds into the loop.
  std::function<void(std::vector<pollfd>&)> on_gather_fds;
  std::function<int()> on_poll_timeout;
  std::function<void(const pollfd* fds, int nfds)> on_post_poll;

  std::function<bool()> on_display;
  std::function<void(bool did_display_event)> on_idle_flush;
};

int run_main_loop(bool* running, const MainLoopOps& ops);

}
