#include "bootstrap/loop/poll_mux.hpp"

#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"
#include "bootstrap/loop/wl_loop_diag.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <poll.h>
#include <csignal>
#include <signal.h>
#include <unordered_set>
#include <vector>

namespace eh::app {

namespace {
using steady_clock = std::chrono::steady_clock;
}

static volatile sig_atomic_t g_poll_mux_signal{0};

static void poll_mux_signal_handler(int) {
   
  g_poll_mux_signal = 1;
}

struct PollSlot {
  int fd = -1;
  wl_display* display = nullptr;
  wl_integrated::DisplayPollState state{};
  short revents = 0;
  bool did_read = false;
  bool ok = true;
  // Returning false from on_dispatch requests a loop exit.
  std::function<bool()> on_dispatch;
  std::function<void()> on_error;
};

static bool prepare_slot(PollSlot& slot) {
   
  if (!slot.display) return true;
  const bool ok = wl_integrated::before_ppoll(slot.display, &slot.state);
  if (!ok) {
    slot.ok = false;
    if (slot.on_error) slot.on_error();
  }
  return ok;
}

// Returns false when the display has hit a fatal error.
static bool finish_slot(PollSlot& slot, short revents) {
   
  slot.revents = revents;
  if (!slot.display || !slot.state.read_armed) return true;
  bool did_read = false;
  const bool ok = wl_integrated::after_ppoll(slot.display, revents, &slot.state, &did_read);
  slot.did_read = did_read;
  if (!ok) {
    slot.ok = false;
    if (slot.on_error) slot.on_error();
  }
  if (ok && did_read && slot.on_dispatch) {
    if (!slot.on_dispatch()) {
      slot.ok = false;
    }
  }
  return slot.ok;
}

int PollMuxLoop::run(bool* running) const {
   
  MANGOWM_DEBUG("PollMuxLoop::run display_fd=%d", display_fd);
  if (!running) return 1;
  if (display_fd < 0 && extra_displays.empty()) return 1;

  g_poll_mux_signal = 0;
  struct sigaction sa{};
  sa.sa_handler = poll_mux_signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  (void)sigaction(SIGTERM, &sa, nullptr);
  (void)sigaction(SIGINT, &sa, nullptr);

  sigset_t poll_mask, old_mask;
  sigemptyset(&poll_mask);
  sigaddset(&poll_mask, SIGTERM);
  sigaddset(&poll_mask, SIGINT);
  (void)sigprocmask(SIG_BLOCK, &poll_mask, &old_mask);

  wl_loop_diag::log_diag_banner_once("poll_mux");

  // The slot set is fixed for the loop's lifetime (primary display + extras), so
  // build it once and just reset per-iteration state.
  const std::size_t n_extra = extra_displays.size();
  std::vector<PollSlot> slots;
  slots.reserve(1 + n_extra);

  // Slot 0 is the primary display.
  {
    PollSlot ps;
    ps.fd = display_fd;
    ps.display = wayland_display;
    if (on_display) {
      ps.on_dispatch = [this] { return on_display(); };
    }
    ps.on_error = [running] { *running = false; };
    slots.push_back(std::move(ps));
  }

  for (const auto& ed : extra_displays) {
    PollSlot ps;
    ps.fd = ed.fd;
    ps.display = ed.display;
    ps.on_dispatch = ed.on_dispatch;
    ps.on_error = ed.on_error;
    slots.push_back(std::move(ps));
  }

  const int n_slots = static_cast<int>(slots.size());

  // Reusable scratch vectors; clear+refill keeps the hot path off the heap.
  std::vector<FdHandler> handlers;
  handlers.reserve(8);
  std::vector<pollfd> fds;
  fds.reserve(16 + n_extra);
  std::vector<std::function<void(short)>> ready_callbacks;
  ready_callbacks.reserve(16);

  while (*running && !g_poll_mux_signal) {
    wl_loop_diag::maybe_heartbeat("poll_mux");
    const steady_clock::time_point t_iter_start = steady_clock::now();
    long ms_prep = 0;

    // Reset per-iteration slot state.
    for (auto& slot : slots) {
      slot.revents = 0;
      slot.did_read = false;
      slot.ok = true;
    }

    // Run before_ppoll on every display.
    {
      const steady_clock::time_point t0 = steady_clock::now();
      for (auto& slot : slots) {
        if (!prepare_slot(slot)) {
          if (slot.fd == display_fd) {
            *running = false;
            break;
          }
          // A dead extra display keeps a pending error and would fail before_ppoll on
          // every iteration (per-frame on_error spam plus a busy spin), so disable
          // its slot after the first error; on_error already ran to detach the
          // host. Only the primary display should take the whole loop down.
          slot.fd = -1;
          slot.display = nullptr;
        }
      }
      ms_prep = ms_between(t0, steady_clock::now());
      wl_loop_diag::log_stall_if_slow("poll_mux", "before_ppoll", ms_prep);
      if (!*running) break;
    }

    // Build the poll fd list from the pre-allocated vectors.
    handlers.clear();
    if (get_handlers) {
      auto h = get_handlers();
      handlers = std::move(h);
    }
    fds.clear();
    ready_callbacks.clear();

    for (const auto& h : handlers) {
      if (h.fd < 0) continue;
      pollfd p{};
      p.fd = h.fd;
      p.events = h.events;
      fds.push_back(p);
      ready_callbacks.push_back(h.on_ready);
    }

    // Fold in the GLib fds (polkit agent).
    if (on_gather_fds) {
      on_gather_fds(fds);
    }

    // Append every display fd.
    const int handler_count = static_cast<int>(fds.size());
    // Track where each slot landed in the fd list (-1 if it wasn't added).
    int slot_fds_idx[16] = {};
    for (int i = 0; i < 16; ++i) slot_fds_idx[i] = -1;
    for (int si = 0; si < n_slots; ++si) {
      const auto& slot = slots[si];
      if (slot.fd < 0) continue;
      slot_fds_idx[si] = static_cast<int>(fds.size());
      pollfd p{};
      p.fd = slot.fd;
      p.events = slot.display ? slot.state.display_events : POLLIN;
      fds.push_back(p);
    }

    // Compute the poll timeout.
    constexpr long kSecToMs = 1000;
    long timeout_ms = static_cast<long>(wl_loop_diag::merged_ppoll_timeout_sec()) * kSecToMs;
    if (on_poll_timeout) {
      const int glib_ms = on_poll_timeout();
      if (glib_ms >= 0 && static_cast<long>(glib_ms) < timeout_ms) {
        timeout_ms = static_cast<long>(glib_ms);
      }
    }
    timespec timeout{};
    timeout.tv_sec = timeout_ms / kSecToMs;
    timeout.tv_nsec = (timeout_ms % kSecToMs) * 1000000L;

    // Block in ppoll.
    const steady_clock::time_point t_ppoll = steady_clock::now();
    sigset_t ppoll_mask = old_mask;
    sigdelset(&ppoll_mask, SIGTERM);
    sigdelset(&ppoll_mask, SIGINT);
    const int r = ppoll(fds.data(), static_cast<nfds_t>(fds.size()), &timeout, &ppoll_mask);
    if (r < 0 || g_poll_mux_signal) break;
    const long ms_ppoll = ms_between(t_ppoll, steady_clock::now());
    wl_loop_diag::log_ppoll_wait("poll_mux", r, ms_ppoll);

    // Run after_ppoll on every display.
    long ms_after = 0;
    long ms_on_disp = 0;
    bool any_did_read = false;
    {
      const steady_clock::time_point ta = steady_clock::now();
      // Handle extras first so wl_buffer.release lands before the main display's
      // paint tick.
      for (int i = 1; i < n_slots; ++i) {
        const int fd_idx = slot_fds_idx[i];
        const short rev = (fd_idx >= 0 && fd_idx < static_cast<int>(fds.size())) ? fds[fd_idx].revents : 0;
        finish_slot(slots[i], rev);
        if (slots[i].did_read) any_did_read = true;
        if (!slots[i].ok) {
          slots[i].fd = -1;
          slots[i].display = nullptr;
        }
      }
      // The main display (slot 0) is processed last.
      if (n_slots > 0) {
        const int fd_idx = slot_fds_idx[0];
        const short rev = (fd_idx >= 0 && fd_idx < static_cast<int>(fds.size())) ? fds[fd_idx].revents : 0;
        if (!finish_slot(slots[0], rev)) {
          *running = false;
        }
        if (slots[0].did_read) any_did_read = true;
      }
      ms_after = ms_between(ta, steady_clock::now());
      wl_loop_diag::log_stall_if_slow("poll_mux", "after_ppoll", ms_after);
      if (!*running) break;
    }

    // With no Wayland display, handle the display fd directly (displayless fallback).
    if (!wayland_display && n_slots > 0 && slots[0].fd >= 0) {
      const int fd_idx = slot_fds_idx[0];
      if (fd_idx >= 0 && fd_idx < static_cast<int>(fds.size()) && fds[fd_idx].revents & (POLLERR | POLLHUP)) {
        *running = false;
        break;
      }
    }

    // Let GLib drain its pending events now (polkit agent).
    if (on_post_poll) {
      on_post_poll(fds.data(), static_cast<int>(fds.size()));
    }

    // Dispatch fd handlers.
    // NOTE: a handler whose fd got closed elsewhere reports POLLNVAL on every
    // ppoll iteration; firing it unchanged turns one stale registration into a
    // 100% CPU busy-loop. Skip POLLNVAL entries and log once per fd so the
    // owner can be identified without spinning the loop.
    for (int i = 0; i < handler_count; ++i) {
      const short revents = fds[i].revents;
      if (revents == 0) continue;
      if (revents & POLLNVAL) {
        static thread_local std::unordered_set<int> nval_reported;
        if (nval_reported.insert(fds[i].fd).second) {
          std::fprintf(stderr, "[poll-mux] ignoring closed fd=%d (POLLNVAL)\n", fds[i].fd);
        }
        continue;
      }
      if (i < static_cast<int>(ready_callbacks.size()) && ready_callbacks[i]) {
        const steady_clock::time_point tcb = steady_clock::now();
        ready_callbacks[i](revents);
        wl_loop_diag::log_stall_handler_fd("poll_mux", fds[i].fd, ms_between(tcb, steady_clock::now()));
      }
    }

    const long ms_iter_total = ms_between(t_iter_start, steady_clock::now());
    wl_loop_diag::log_iter_verbose("poll_mux", r, 0, ms_prep, ms_after, ms_on_disp, ms_iter_total);

    // Idle flush.
    {
      const steady_clock::time_point tidle = steady_clock::now();
      DeferredCall::drain();
      if (on_idle_flush) {
        on_idle_flush(any_did_read);
      }
      wl_loop_diag::log_stall_if_slow("poll_mux", "on_idle_flush_total", ms_between(tidle, steady_clock::now()));
    }
  }

  (void)sigprocmask(SIG_SETMASK, &old_mask, nullptr);
  return 0;
}

} // namespace eh::app
