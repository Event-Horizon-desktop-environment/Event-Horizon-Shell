#define _GNU_SOURCE 1
#include "notifications/standalone/notifications_standalone.hpp"

#include "bootstrap/thread/thread_dispatch.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/notifications/core/notifications.hpp"
#include "desktop_shell/notifications/host/notification_toast_host.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "services/notifications/notification_dbus_service.hpp"
#include "services/process/parent_death_guard.hpp"
#include "wl/core/connection.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::notifications {

namespace {

volatile sig_atomic_t g_notif_signal = 0;

void notif_signal_handler(int) { g_notif_signal = 1; }

void arm_notification_expiry_timer(int timer_fd, const NotificationManager& mgr) {
  if (timer_fd < 0) return;
  const int ms = mgr.nextExpiryTimeoutMs();
  itimerspec its{};
  its.it_interval.tv_sec = 0;
  its.it_interval.tv_nsec = 0;
  if (ms < 0) {
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
  } else {
    const int clamped = std::max(ms, 1);
    its.it_value.tv_sec = static_cast<time_t>(clamped / 1000);
    its.it_value.tv_nsec = static_cast<long>((clamped % 1000) * 1000000LL);
  }
  (void)timerfd_settime(timer_fd, 0, &its, nullptr);
}

struct MprisEntry {
  std::uint32_t id = 0;
  std::string summary;
  std::string body;
};

std::vector<std::string> split_text_fields(const std::string& payload, char sep) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t pos = payload.find(sep, start);
    if (pos == std::string::npos) {
      parts.push_back(payload.substr(start));
      break;
    }
    parts.push_back(payload.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

// Dispatch one `notify.push` payload. GOTCHA: the `mpris-art` tail is a raw
// RGBA blob and CAN contain kFieldSep bytes, so it is parsed by position, never
// tokenized. Only `internal` / `mpris` are safe to tokenize (text fields).
void handle_notify_push(NotificationManager& mgr, const std::string& payload,
                        std::unordered_map<std::string, MprisEntry>& mpris_entries) {
  constexpr std::size_t kKindArtLen = sizeof(eh::notify::kKindMprisArt) - 1;
  if (payload.size() >= kKindArtLen &&
      payload.compare(0, kKindArtLen, eh::notify::kKindMprisArt) == 0) {
    const std::size_t sep1 = payload.find(eh::notify::kFieldSep, kKindArtLen);
    if (sep1 == std::string::npos) return;
    const std::size_t sep2 = payload.find(eh::notify::kFieldSep, sep1 + 1);
    if (sep2 == std::string::npos) return;
    const std::string key = payload.substr(sep1 + 1, sep2 - sep1 - 1);
    const std::string_view blob(payload.data() + sep2 + 1, payload.size() - sep2 - 1);
    auto img = eh::notify::deserialize_image_blob(blob);
    if (!img) return;
    const auto it = mpris_entries.find(key);
    if (it == mpris_entries.end() || it->second.id == 0) return;  // art before the notification
    it->second.id = mgr.addOrReplace(it->second.id, "Media", it->second.summary, it->second.body,
                                     Urgency::Normal, kDefaultNotificationTimeoutMs,
                                     NotificationOrigin::Internal, {}, std::nullopt, std::move(*img));
    return;
  }

  constexpr std::size_t kKindMprisLen = sizeof(eh::notify::kKindMpris) - 1;
  if (payload.size() >= kKindMprisLen &&
      payload.compare(0, kKindMprisLen, eh::notify::kKindMpris) == 0) {
    const auto parts = split_text_fields(payload, eh::notify::kFieldSep);
    if (parts.size() < 4) return;
    const std::string key = parts[1];
    MprisEntry e;
    if (const auto it = mpris_entries.find(key); it != mpris_entries.end()) e = it->second;
    e.summary = parts[2];
    e.body = parts[3];
    e.id = mgr.addOrReplace(e.id, "Media", e.summary, e.body, Urgency::Normal,
                            kDefaultNotificationTimeoutMs, NotificationOrigin::Internal);
    mpris_entries[key] = std::move(e);
    return;
  }

  constexpr std::size_t kKindInternalLen = sizeof(eh::notify::kKindInternal) - 1;
  if (payload.size() >= kKindInternalLen &&
      payload.compare(0, kKindInternalLen, eh::notify::kKindInternal) == 0) {
    const auto parts = split_text_fields(payload, eh::notify::kFieldSep);
    if (parts.size() < 5) return;
    int urgency = static_cast<int>(Urgency::Normal);
    try {
      urgency = std::stoi(parts[4]);
    } catch (...) {
    }
    if (urgency < static_cast<int>(Urgency::Low)) urgency = static_cast<int>(Urgency::Low);
    if (urgency > static_cast<int>(Urgency::Critical)) urgency = static_cast<int>(Urgency::Critical);
    (void)mgr.addOrReplace(0, parts[1], parts[2], parts[3], static_cast<Urgency>(urgency),
                           mgr.serverDefaultTimeoutMs(), NotificationOrigin::Internal);
  }
}

// (Re)apply the toast host to the config + the connection's first output. The
// child owns the layer-shell surface, so this is the port of the supervisor's
// old sync_notification_toast_host.
void sync_toast_host(eh::wayland::WaylandConnection& conn, NotificationToastHost& toast,
                     const eh::config::ShellNotificationsSettings& ncfg, bool& toast_active) {
  if (!ncfg.toast.layerShellEnabled || !conn.compositor() || !conn.shm() || !conn.layer_shell()) {
    toast.shutdown();
    toast_active = false;
    return;
  }
  wl_output* output = nullptr;
  const auto outs = conn.outputs();
  if (!outs.empty()) output = outs.front();
  if (!output) {
    toast.shutdown();
    toast_active = false;
    return;
  }
  if (!toast_active) {
    toast.initialize(conn.compositor(), conn.shm(), conn.layer_shell(), conn.seat(), output);
    toast_active = true;
  }
  toast.apply_config(ncfg.toast);
}

}  // namespace

int run_notifications_standalone() {
  eh::proc::install_parent_death_guard();

  eh::wayland::WaylandConnection conn;
  if (!conn.connect(false)) {
    std::cerr << "[horizon-notifications] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  NotificationManager mgr;
  NotificationToastHost toast_host(mgr);
  eh::notify::setInstance(&mgr);

  eh::config::shell_config_reload_from_disk_now(true);
  const eh::config::ShellNotificationsSettings ncfg = eh::config::shell_config_snapshot().notifications;
  mgr.setServerDefaultTimeoutMs(ncfg.defaultTimeoutMs);
  mgr.setDoNotDisturb(ncfg.doNotDisturb);

  bool toast_active = false;
  sync_toast_host(conn, toast_host, ncfg, toast_active);

  const int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timer_fd < 0) {
    std::cerr << "[horizon-notifications] timerfd_create failed errno=" << errno << " ("
              << std::strerror(errno) << ")\n";
  }

  int event_cb_token = -1;
  event_cb_token = mgr.addEventCallback(
      [&](const Notification&, NotificationEvent) {
        arm_notification_expiry_timer(timer_fd, mgr);
        DeferredCall::callLater([&]() {
          toast_host.on_notifications_changed();
        });
      });

  mgr.setStateCallback([&]() {
    if (mgr.doNotDisturb()) {
      const auto active = mgr.all();
      for (const auto& n : active) {
        (void)mgr.close(n.id, CloseReason::Dismissed);
      }
    }
    arm_notification_expiry_timer(timer_fd, mgr);
    DeferredCall::callLater([&]() {
      toast_host.on_notifications_changed();
    });
  });

  std::unique_ptr<NotificationDbusService> dbus;
  if (ncfg.dbusEnabled) {
    try {
      dbus = std::make_unique<NotificationDbusService>(mgr);
      std::cerr << "[horizon-notifications] listening on org.freedesktop.Notifications\n";
    } catch (const std::exception& e) {
      std::cerr << "[horizon-notifications] dbus unavailable: " << e.what() << '\n';
      dbus.reset();
    }
  } else {
    std::cerr << "[horizon-notifications] dbus server disabled in config ([notifications] dbus = false)\n";
  }

  std::unordered_map<std::string, MprisEntry> mpris_entries;
  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&](std::string topic, std::string payload, std::vector<int> /*fds*/) {
        if (topic == eh::notify::kPushTopic) {
          handle_notify_push(mgr, payload, mpris_entries);
          arm_notification_expiry_timer(timer_fd, mgr);
        } else if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          const auto& sc = eh::config::shell_config_snapshot();
          mgr.setServerDefaultTimeoutMs(sc.notifications.defaultTimeoutMs);
          mgr.setDoNotDisturb(sc.notifications.doNotDisturb);
          sync_toast_host(conn, toast_host, sc.notifications, toast_active);
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe(eh::notify::kPushTopic);
    }
  }

  std::cout << "[horizon-notifications] running pid=" << ::getpid()
            << " dbus=" << (dbus ? 1 : 0) << " toast=" << (toast_active ? 1 : 0)
            << " ipc=" << (ipc_ok ? 1 : 0) << "\n";

  g_notif_signal = 0;
  {
    struct sigaction sa {};
    sa.sa_handler = notif_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
  }

  const int dpy_fd = wl_display_get_fd(conn.display());
  constexpr int kAnimationTickMs = 50;
  constexpr int kIdlePollMs = 30'000;
  while (g_notif_signal == 0) {
    // While a countdown is running, poll at a short tick so the toast progress
    // bar animates in real time even if the compositor stops delivering frame
    // callbacks (e.g. the toast output is not being presented).
    const int poll_timeout_ms = toast_host.tick_animations() ? kAnimationTickMs : kIdlePollMs;
    pollfd pf[3]{};
    int n = 0;
    int timer_idx = -1;
    int ipc_idx = -1;
    pf[n].fd = dpy_fd;
    pf[n].events = POLLIN | POLLERR | POLLHUP;
    ++n;
    if (timer_fd >= 0) {
      pf[n].fd = timer_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      timer_idx = n;
      ++n;
    }
    if (ipc_ok) {
      // Re-resolve the fd every pass: a dropped connection closes it inside
      // IpcClient, and polling a stale captured number would spin on POLLNVAL.
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
        pf[n].events = POLLIN | POLLERR | POLLHUP;
        ipc_idx = n;
        ++n;
      }
    }
    const int pr = poll(pf, static_cast<nfds_t>(n), poll_timeout_ms);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_notif_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_notif_signal != 0) break;
    if ((pf[0].revents & (POLLERR | POLLHUP)) != 0) break;
    if (timer_idx >= 0 && (pf[timer_idx].revents & (POLLERR | POLLHUP)) != 0) break;
    if (ipc_idx >= 0 && (pf[ipc_idx].revents & (POLLERR | POLLHUP)) != 0) {
      ipc_ok = false;
      ipc_idx = -1;
    }
    if (timer_idx >= 0 && (pf[timer_idx].revents & POLLIN)) {
      std::uint64_t expirations = 0;
      (void)read(timer_fd, &expirations, sizeof(expirations));
      if (dbus) {
        dbus->processExpired();
      } else {
        mgr.processExpired();
      }
      arm_notification_expiry_timer(timer_fd, mgr);
    }
    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(conn.display()) < 0) break;
    } else {
      (void)wl_display_flush(conn.display());
    }
    if (ipc_idx >= 0 && (pf[ipc_idx].revents & POLLIN)) {
      ipc.on_fd_ready(pf[ipc_idx].fd);
    }
    // Return freed heap pages (pixbufs, toast layouts) to the OS.
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
    DeferredCall::drain();
  }

  toast_host.shutdown();
  dbus.reset();
  if (event_cb_token >= 0) mgr.removeEventCallback(event_cb_token);
  if (timer_fd >= 0) (void)close(timer_fd);
  eh::notify::setInstance(nullptr);
  DeferredCall::drain();
  std::cout << "[horizon-notifications] exit\n";
  return 0;
}

}
