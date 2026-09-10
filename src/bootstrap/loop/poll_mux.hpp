#pragma once

#include "bootstrap/loop/wayland_integrated_poll.hpp"

#include <functional>
#include <vector>

#include <poll.h>

struct wl_display;

namespace eh::app {

struct FdHandler {
  int fd = -1;
  short events = 0;
  std::function<void(short revents)> on_ready;
};

int run_poll_mux(bool* running,
                 wl_display* wayland_display,
                 int display_fd,
                 const std::function<bool()>& on_display,
                 const std::function<void(bool did_display_event)>& on_idle_flush,
                 const std::function<std::vector<FdHandler>()>& get_handlers);

struct PollMuxDisplay {
  wl_display* display = nullptr;
  int fd = -1;
  // Returning false from on_dispatch asks the loop to exit.
  std::function<bool()> on_dispatch;
  std::function<void()> on_error;
};

struct PollMuxLoop {
  wl_display* wayland_display = nullptr;
  int display_fd = -1;
  std::function<bool()> on_display;
  std::function<void(bool did_display_event)> on_idle_flush;
  std::function<std::vector<FdHandler>()> get_handlers;

  // GLib drives the polkit agent; its fds get folded into the poll set.
  std::function<void(std::vector<pollfd>&)> on_gather_fds;
  std::function<int()> on_poll_timeout;
  std::function<void(const pollfd* fds, int nfds)> on_post_poll;

  // Extra, isolated Wayland connections. A protocol error here routes to
  // on_error() rather than taking down the whole loop.
  std::vector<PollMuxDisplay> extra_displays;

  int run(bool* running) const;
};

}
