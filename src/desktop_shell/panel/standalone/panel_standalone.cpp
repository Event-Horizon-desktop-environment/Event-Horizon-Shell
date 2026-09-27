// Panel standalone child — brand-new event loop for `horizon-panel`.
// Own Wayland connection, own toplevel tracking, 1s poll timer, settings
// inotify, IPC config.applied/command.request, MPRIS/tray fds, periodic
// heap trim, parent-death guard. Mirrors the dock/taskbar child contract
// so the supervisor can treat it identically.

#define _GNU_SOURCE 1
#include "desktop_shell/panel/standalone/panel_standalone.hpp"

#include "desktop_shell/panel/core/panel.hpp"
#include "desktop_shell/panel/spawn/panel_spawn.hpp"

#include "services/process/parent_death_guard.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/shared/core/config_watch.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "services/ipc/client.hpp"
#include "desktop_shell/notifications/types/notifications_ipc.hpp"
#include "services/ipc/ipc_server.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wl/core/connection.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::panel {

namespace {

struct ToplevelBindCtx {
  wl_display* display = nullptr;
  eh::wayland::ForeignToplevels* toplevels = nullptr;
  zwlr_foreign_toplevel_manager_v1* manager = nullptr;
};

void toplevel_bind_global(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                          std::uint32_t version) {
  auto* ctx = static_cast<ToplevelBindCtx*>(data);
  if (ctx->manager) return;
  if (std::string_view(interface) != zwlr_foreign_toplevel_manager_v1_interface.name) return;
  ctx->manager = static_cast<zwlr_foreign_toplevel_manager_v1*>(
      wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min<std::uint32_t>(version, 3)));
  if (ctx->manager) ctx->toplevels->attach(ctx->manager, ctx->display);
}

void toplevel_bind_global_remove(void*, wl_registry*, std::uint32_t) {}

const wl_registry_listener kToplevelBindListener = {
    .global = toplevel_bind_global,
    .global_remove = toplevel_bind_global_remove,
};

zwlr_foreign_toplevel_manager_v1* bind_toplevel_manager_for_tracking(wl_display* display,
                                                                    eh::wayland::ForeignToplevels& toplevels) {
  if (!display) return nullptr;
  wl_registry* registry = wl_display_get_registry(display);
  if (!registry) return nullptr;
  ToplevelBindCtx ctx{display, &toplevels, nullptr};
  wl_registry_add_listener(registry, &kToplevelBindListener, &ctx);
  wl_display_flush(display);
  (void)wl_display_roundtrip(display);
  wl_registry_destroy(registry);
  return ctx.manager;
}

void create_panel_layers(PanelApp& app) {
  if (!app.wl || app.wlError) return;
  const std::string out = eh::shell::trim_output_assign(eh::config::shell_config_snapshot().panel.outputName);
  if (eh::shell::output_assign_is_all_displays(out)) {
    for (const auto& o : app.wl->logical_output_bounds())
      if (o.output) panel_add_layer(app, o.output);
  } else {
    wl_output* target = nullptr;
    if (!eh::shell::output_assign_is_auto(out) && !out.empty()) target = app.wl->output_by_name(out);
    if (!target) {
      for (const auto& o : app.wl->logical_output_bounds()) {
        if (o.output) {
          target = o.output;
          if (o.global_x <= 0 && o.global_y <= 0) break;
        }
      }
    }
    if (target) panel_add_layer(app, target);
  }
}

}  // namespace

int run_panel_standalone() {
  eh::proc::install_parent_death_guard();
  eh::config::shell_config_reload_from_disk_now(true);
  const auto sc0 = eh::config::shell_config_snapshot();

  PanelApp app;
  app.settings.enabled = sc0.panel.enabled;
  // Full settings arrive via maybe_reload below.
  panel_maybe_reload_settings(app, sc0);

  if (!panel_init_on_display(app)) {
    std::cerr << "[horizon-panel] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  eh::wayland::ForeignToplevels toplevels;
  if (bind_toplevel_manager_for_tracking(app.display, toplevels)) {
    toplevels.set_visual_dirty_hook([&app]() {
      app.frameRedrawPending = true;
      panel_schedule_frame(app);
    });
    app.toplevels = &toplevels;
  } else {
    std::cerr << "[horizon-panel] no zwlr_foreign_toplevel_manager_v1; running apps will not show\n";
  }
  if (app.wl && app.wl->ext_foreign_toplevel_list()) app.extToplevels = &app.wl->ext_foreign_toplevels();
  app.compositorKind = detect_compositor_kind();

  panel_init_deferred_startup(app);
  create_panel_layers(app);

  int poll_timer_fd = -1;
  int tooltip_timer_fd = -1;
  int settings_fd = -1;
  auto install_loop_fds = [&]() {
    if (poll_timer_fd < 0) {
      poll_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
      if (poll_timer_fd < 0) {
        std::cerr << "[horizon-panel] poll timer unavailable (" << std::strerror(errno) << ")\n";
      } else {
        itimerspec its{};
        its.it_interval.tv_sec = 1;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 1;
        its.it_value.tv_nsec = 0;
        (void)timerfd_settime(poll_timer_fd, 0, &its, nullptr);
      }
    }
    if (tooltip_timer_fd < 0) {
      // Tooltip hover delay wants sub-second granularity; the 1s poll above
      // would add up to a second of lag. Dedicated 200ms tick, tooltip only.
      tooltip_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
      if (tooltip_timer_fd >= 0) {
        itimerspec its{};
        its.it_interval.tv_nsec = 200 * 1000 * 1000;
        its.it_value.tv_nsec = 200 * 1000 * 1000;
        (void)timerfd_settime(tooltip_timer_fd, 0, &its, nullptr);
      }
    }
    if (settings_fd < 0) {
      settings_fd = eh::shell::shared::open_state_inotify();
      if (settings_fd < 0) std::cerr << "[horizon-panel] settings watch unavailable\n";
    }
  };
  install_loop_fds();

  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app](std::string topic, std::string payload, std::vector<int>) {
        if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          const auto& sc = eh::config::shell_config_snapshot();
          panel_maybe_reload_settings(app, sc);
          panel_schedule_frame(app);
        } else if (topic == "command.request") {
          if (payload == "settings.toggle") eh::settings::request_launch_settings();
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
      // This child runs its own DockMpris: forward notify.push frames so
      // now-playing toasts (and album art) reach horizon-notifications.
      eh::notify::install_ipc_sender(ipc);
  // Middle-click on the workspaces widget publishes `overview.toggle` so the
  // horizon-stage child flips the overview.
  panel_set_overview_toggle_fn([&ipc]() { (void)ipc.publish("command.request", "overview.toggle"); });
    }
  }

  panel_write_pid_file(::getpid());

  eh::shell::dock_slot_hooks::control_center_weather_async_init();
  const int weather_wake_fd = eh::shell::dock_slot_hooks::control_center_weather_async_wake_fd();

  bool running = true;
  eh::app::PollMuxLoop mux{};
  mux.wayland_display = app.display;
  mux.display_fd = wl_display_get_fd(app.display);
  mux.on_display = [&running]() { return running; };
  mux.on_idle_flush = [&app, &install_loop_fds, &poll_timer_fd, &tooltip_timer_fd, &settings_fd](bool) {
    eh::shell::dock_slot_hooks::control_center_weather_drive_curl_multi();
    if (poll_timer_fd < 0 || tooltip_timer_fd < 0 || settings_fd < 0) {
      static std::uint64_t lastHealMs = 0;
      const std::uint64_t nowMs = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count());
      if (nowMs - lastHealMs > 5000) {
        lastHealMs = nowMs;
        install_loop_fds();
      }
    }
    if (app.pendingOutputRebind) {
      app.pendingOutputRebind = false;
      create_panel_layers(app);
    }
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
  };
  mux.get_handlers = [&]() {
    std::vector<eh::app::FdHandler> handlers;
    handlers.reserve(8);
    if (app.trayEventFd >= 0)
      handlers.emplace_back(app.trayEventFd, POLLIN, [&app](short) { panel_handle_tray(app); });
    if (weather_wake_fd >= 0)
      handlers.emplace_back(weather_wake_fd, POLLIN, [](short) {
        eh::shell::dock_slot_hooks::control_center_weather_async_handle_wake();
      });
    if (poll_timer_fd >= 0)
      handlers.emplace_back(poll_timer_fd, POLLIN, [&app, &poll_timer_fd](short) {
        std::uint64_t exp = 0;
        (void)read(poll_timer_fd, &exp, sizeof(exp));
        if (!app.enabled) return;
        eh::shell::dock_slot_hooks::battery_widget_poll();
        eh::shell::dock_slot_hooks::bluetooth_widget_poll();
        if (app.mpris) {
          try {
            (void)app.mpris->poll_refresh();
          } catch (...) {
          }
        }
        app.frameRedrawPending = true;
        panel_schedule_frame(app);
      });
    if (tooltip_timer_fd >= 0)
      handlers.emplace_back(tooltip_timer_fd, POLLIN, [&app, &tooltip_timer_fd](short) {
        std::uint64_t exp = 0;
        (void)read(tooltip_timer_fd, &exp, sizeof(exp));
        if (!app.enabled) return;
        panel_tooltip_tick(app);
      });
    if (settings_fd >= 0)
      handlers.emplace_back(settings_fd, POLLIN, [&app, &settings_fd](short) {
        eh::shell::shared::drain_inotify(settings_fd, [&app]() {
          eh::config::shell_config_invalidate_light();
          const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
          panel_maybe_reload_settings(app, sc);
          panel_schedule_frame(app);
        });
      });
    if (ipc_ok) {
      int cfd = ipc.fd();
      if (cfd < 0) {
        static unsigned wait = 0;
        if (++wait >= 2048) {
          wait = 0;
          cfd = ipc.connect(eh::ipc::default_socket_path(), 1);
        }
      }
      if (cfd >= 0) handlers.emplace_back(cfd, POLLIN, [&ipc, cfd](short) { ipc.on_fd_ready(cfd); });
    }
    if (app.wl) {
      auto& hypr = app.wl->runtime_registry().hyprland();
      const int hfd = hypr.pollHandle();
      if (hfd >= 0) handlers.emplace_back(hfd, POLLIN, [&hypr](short r) { hypr.handlePoll(r); });
    }
    if (app.mpris) {
      const int bfd = app.mpris->poll_fd();
      if (bfd >= 0)
        handlers.emplace_back(bfd, POLLIN, [&app](short) {
          if (app.mpris) app.mpris->process_pending_events();
        });
      const int efd = app.mpris->poll_event_fd();
      if (efd >= 0)
        handlers.emplace_back(efd, POLLIN, [&app](short) {
          if (app.mpris) app.mpris->process_pending_events();
        });
    }
    return handlers;
  };

  std::cout << "[horizon-panel] running pid=" << ::getpid() << " ipc=" << (ipc_ok ? 1 : 0) << "\n";
  (void)mux.run(&running);

  app.toplevels = nullptr;
  app.extToplevels = nullptr;
  toplevels.shutdown();
  panel_cleanup(app);
  panel_unlink_pid_file();
  if (poll_timer_fd >= 0) (void)close(poll_timer_fd);
  if (tooltip_timer_fd >= 0) (void)close(tooltip_timer_fd);
  if (settings_fd >= 0) (void)close(settings_fd);
  std::cout << "[horizon-panel] exit\n";
  return 0;
}

}  // namespace eh::shell::panel
