#include "bootstrap/loop/main_loop.hpp"

#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"
#include "bootstrap/loop/wayland_integrated_poll.hpp"
#include "bootstrap/loop/wl_loop_diag.hpp"

#include <chrono>
#include <poll.h>
#include <csignal>
#include <signal.h>
#include <vector>

namespace eh::app {

namespace {
using steady_clock = std::chrono::steady_clock;

static volatile sig_atomic_t g_signal_received{0};

void signal_handler(int) {
   
  g_signal_received = 1;
}
}

int run_main_loop(bool* running, const MainLoopOps& ops) {
   
  MANGOWM_DEBUG("run_main_loop display_fd=%d", ops.display_fd);
  if (!running) return 1;
  if (ops.display_fd < 0) return 1;

  g_signal_received = 0;
  struct sigaction sa{};
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  (void)sigaction(SIGTERM, &sa, nullptr);
  (void)sigaction(SIGINT, &sa, nullptr);

  sigset_t poll_mask, old_mask;
  sigemptyset(&poll_mask);
  sigaddset(&poll_mask, SIGTERM);
  sigaddset(&poll_mask, SIGINT);
  (void)sigprocmask(SIG_BLOCK, &poll_mask, &old_mask);

  wl_loop_diag::log_diag_banner_once("main_loop");

  std::vector<pollfd> fds;
  fds.reserve(16);

  while (*running && !g_signal_received) {
    wl_loop_diag::maybe_heartbeat("main_loop");
    const steady_clock::time_point t_iter_start = steady_clock::now();
    long ms_prep = 0;

    wl_integrated::DisplayPollState wl_st{};
    if (ops.wayland_display) {
      const steady_clock::time_point t0 = steady_clock::now();
      if (!wl_integrated::before_ppoll(ops.wayland_display, &wl_st)) {
        *running = false;
        break;
      }
      ms_prep = ms_between(t0, steady_clock::now());
      wl_loop_diag::log_stall_if_slow("main_loop", "before_ppoll", ms_prep);
    }

    fds.clear();

    const int tray_poll_fd = ops.tray_fd_live ? *ops.tray_fd_live : ops.tray_fd;

    if (ops.timer_fd >= 0) fds.emplace_back(ops.timer_fd, POLLIN, 0);
    if (ops.notification_timer_fd >= 0) fds.emplace_back(ops.notification_timer_fd, POLLIN, 0);
    if (tray_poll_fd >= 0) fds.emplace_back(tray_poll_fd, POLLIN, 0);
    if (ops.inotify_fd >= 0) fds.emplace_back(ops.inotify_fd, POLLIN, 0);
    if (ops.weather_fd >= 0) fds.emplace_back(ops.weather_fd, POLLIN, 0);

    if (ops.on_gather_fds) ops.on_gather_fds(fds);

    int poll_timeout = 5000;
    if (ops.on_poll_timeout) {
      const int t = ops.on_poll_timeout();
      if (t >= 0) poll_timeout = std::min(poll_timeout, t);
    }

    fds.emplace_back(ops.display_fd, static_cast<short>(ops.wayland_display ? wl_st.display_events : POLLIN), 0);
    const int display_idx = static_cast<int>(fds.size()) - 1;

    timespec ts{poll_timeout / 1000, static_cast<long>(poll_timeout % 1000) * 1000000L};

    const steady_clock::time_point t_ppoll = steady_clock::now();
    const int r = ppoll(fds.data(), static_cast<nfds_t>(fds.size()), &ts, &old_mask);
    if (r < 0) break;
    wl_loop_diag::log_ppoll_wait("main_loop", r, ms_between(t_ppoll, steady_clock::now()));

    const short drevents = fds[display_idx].revents;
    bool did_display = false;
    long ms_after = 0;
    long ms_on_disp = 0;

    if (ops.wayland_display) {
      bool did_read = false;
      const steady_clock::time_point ta = steady_clock::now();
      if (!wl_integrated::after_ppoll(ops.wayland_display, fds[display_idx].revents, &wl_st, &did_read)) {
        *running = false;
      } else {
        ms_after = ms_between(ta, steady_clock::now());
        wl_loop_diag::log_stall_if_slow("main_loop", "after_ppoll", ms_after);
        did_display = did_read;
        if (ops.on_display) {
          const steady_clock::time_point tb = steady_clock::now();
          if (!ops.on_display()) *running = false;
          ms_on_disp = ms_between(tb, steady_clock::now());
          wl_loop_diag::log_stall_if_slow("main_loop", "on_display", ms_on_disp);
        }
      }
    } else {
      if (fds[display_idx].revents & (POLLIN | POLLERR | POLLHUP)) {
        if (fds[display_idx].revents & (POLLERR | POLLHUP)) {
          *running = false;
        } else {
          did_display = true;
          if (ops.on_display) {
            const steady_clock::time_point tb = steady_clock::now();
            if (!ops.on_display()) *running = false;
            ms_on_disp = ms_between(tb, steady_clock::now());
            wl_loop_diag::log_stall_if_slow("main_loop", "on_display(legacy_dispatch)", ms_on_disp);
          }
        }
      }
    }

    {
      const steady_clock::time_point taux = steady_clock::now();
      for (int i = 0; i < display_idx; ++i) {
        if (!(fds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;
        if (fds[i].revents & (POLLERR | POLLHUP)) {
          continue;
        }
        const steady_clock::time_point tcb = steady_clock::now();
        if (ops.timer_fd >= 0 && fds[i].fd == ops.timer_fd) {
          if (ops.on_timer) ops.on_timer();
        } else if (ops.notification_timer_fd >= 0 && fds[i].fd == ops.notification_timer_fd) {
          if (ops.on_notification_timer) ops.on_notification_timer();
        } else if (ops.inotify_fd >= 0 && fds[i].fd == ops.inotify_fd) {
          if (ops.on_inotify) ops.on_inotify();
        } else if (tray_poll_fd >= 0 && fds[i].fd == tray_poll_fd) {
          if (ops.on_tray) ops.on_tray();
        } else if (ops.weather_fd >= 0 && fds[i].fd == ops.weather_fd) {
          if (ops.on_weather) ops.on_weather();
        }
        wl_loop_diag::log_stall_handler_fd("main_loop", fds[i].fd, ms_between(tcb, steady_clock::now()));
      }
      wl_loop_diag::log_stall_if_slow("main_loop", "aux_fd_handlers_total", ms_between(taux, steady_clock::now()));
    }

    if (ops.on_post_poll) ops.on_post_poll(fds.data(), static_cast<int>(fds.size()));

    const long ms_iter_total = ms_between(t_iter_start, steady_clock::now());
    wl_loop_diag::log_iter_verbose("main_loop", r, drevents, ms_prep, ms_after, ms_on_disp, ms_iter_total);

    {
      const steady_clock::time_point tidle = steady_clock::now();
      DeferredCall::drain();
      if (ops.on_idle_flush) {
        ops.on_idle_flush(did_display);
      }
      wl_loop_diag::log_stall_if_slow("main_loop", "on_idle_flush_total", ms_between(tidle, steady_clock::now()));
    }
  }

  (void)sigprocmask(SIG_SETMASK, &old_mask, nullptr);
  return 0;
}

}
