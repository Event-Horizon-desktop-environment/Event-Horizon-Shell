#pragma once

#include <wayland-client-core.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>

namespace eh::app::wl_integrated {

// One-shot diagnostic for the case that used to be invisible: before_ppoll
// failing while wl_display_get_error() still reports 0 (libwayland only sets
// last_error on the read path, so a flush-side failure looks like "no error").
// Reports the errno that actually killed the loop.
inline void log_before_ppoll_failure(const char* where, wl_display* display, int err,
                                     int proto_err, const char* iface, uint32_t id) {
  char link[256] = "<none>";
  const int fd = display ? wl_display_get_fd(display) : -1;
  if (fd >= 0) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    const ssize_t n = ::readlink(path, link, sizeof(link) - 1);
    if (n > 0) link[n] = '\0';
  }
  std::cerr << "[wl-poll] before_ppoll failed at " << where << " errno=" << err << " ("
            << std::strerror(err) << ") display_error="
            << (display ? wl_display_get_error(display) : -1) << " display_fd=" << fd
            << " fd_now=" << link;
  if (proto_err > 0) {
    std::cerr << " PROTOCOL_ERROR=" << proto_err << " interface=" << (iface ? iface : "<null>")
              << " object_id=" << id;
  }
  std::cerr << "\n";
}

// Report a fatal before_ppoll failure after draining the socket: the
// compositor may have sent a wl_display.error event just before it closed us,
// and those bytes are still unread — without this the failure looks like a
// bare EPIPE with no cause.
inline bool report_fatal_flush(wl_display* display, const char* where, int err) {
  if (display) {
    if (wl_display_prepare_read(display) == 0) {
      if (wl_display_read_events(display) == 0) (void)wl_display_dispatch_pending(display);
    } else {
      (void)wl_display_dispatch_pending(display);
    }
    const wl_interface* iface = nullptr;
    uint32_t id = 0;
    const int proto_err = wl_display_get_protocol_error(display, &iface, &id);
    log_before_ppoll_failure(where, display, err, proto_err, iface ? iface->name : nullptr, id);
  } else {
    log_before_ppoll_failure(where, display, err, 0, nullptr, 0);
  }
  return false;
}

struct DisplayPollState {
  short display_events = POLLIN;

  bool read_armed = false;
};

inline bool before_ppoll(wl_display* display, DisplayPollState* st) {
  st->display_events = POLLIN;
  st->read_armed = false;
  if (!display) return true;

  while (wl_display_prepare_read(display) != 0) {
    if (wl_display_dispatch_pending(display) < 0) {
      return report_fatal_flush(display, "dispatch_pending", errno);
    }
  }
  st->read_armed = true;

  int flush_ret = 0;
  do {
    flush_ret = wl_display_flush(display);
  } while (flush_ret < 0 && errno == EINTR);

  if (flush_ret < 0) {
    if (errno != EAGAIN) {
      const int saved = errno;
      wl_display_cancel_read(display);
      st->read_armed = false;
      return report_fatal_flush(display, "flush", saved);
    }
    st->display_events |= POLLOUT;
  }
  return true;
}

inline bool after_ppoll(wl_display* display, short revents, DisplayPollState* st, bool* out_did_read) {
  *out_did_read = false;
  if (!display || !st->read_armed) return true;

  if ((st->display_events & POLLOUT) != 0 && (revents & POLLOUT) != 0) {
    int flush_ret = 0;
    do {
      flush_ret = wl_display_flush(display);
    } while (flush_ret < 0 && errno == EINTR);
    if (flush_ret < 0 && errno != EAGAIN) {
      wl_display_cancel_read(display);
      st->read_armed = false;
      return false;
    }
  }

  const bool readable = (revents & (POLLIN | POLLERR | POLLHUP)) != 0;
  if (readable) {
    if (wl_display_read_events(display) < 0) {
      wl_display_cancel_read(display);
      st->read_armed = false;
      return false;
    }
    st->read_armed = false;
    *out_did_read = (revents & POLLIN) != 0;
  } else {
    wl_display_cancel_read(display);
    st->read_armed = false;
  }

  if (wl_display_dispatch_pending(display) < 0) return false;
  return true;
}

}
