#pragma once

#include <wayland-client-core.h>

#include <cerrno>
#include <poll.h>

namespace eh::app::wl_integrated {

struct DisplayPollState {
  short display_events = POLLIN;

  bool read_armed = false;
};

inline bool before_ppoll(wl_display* display, DisplayPollState* st) {
  st->display_events = POLLIN;
  st->read_armed = false;
  if (!display) return true;

  while (wl_display_prepare_read(display) != 0) {
    if (wl_display_dispatch_pending(display) < 0) return false;
  }
  st->read_armed = true;

  int flush_ret = 0;
  do {
    flush_ret = wl_display_flush(display);
  } while (flush_ret < 0 && errno == EINTR);

  if (flush_ret < 0) {
    if (errno != EAGAIN) {
      wl_display_cancel_read(display);
      st->read_armed = false;
      return false;
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
