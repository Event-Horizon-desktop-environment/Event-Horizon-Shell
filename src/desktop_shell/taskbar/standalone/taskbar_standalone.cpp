#define _GNU_SOURCE 1
#include "desktop_shell/taskbar/standalone/taskbar_standalone.hpp"

#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/taskbar/spawn/taskbar_spawn.hpp"

#include "services/process/parent_death_guard.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "desktop_shell/shared/core/config_watch.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wl/core/connection.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::taskbar {

namespace {

bool is_settings_command(const std::string& payload) {
  return payload == "settings.toggle";
}

// NEW command (distinct from the dock's `menu.toggle`): only the taskbar
// child toggles its start-menu/app-drawer. The supervisor forwards the
// `menu-toggle` IPC command here so a single press cannot toggle both bars.
bool is_menu_command(const std::string& payload) {
  return payload == "taskbar.menu.toggle";
}

// Add taskbar layer surfaces for the configured output (mirrors the
// supervisor's old create_taskbar_layers, which used the taskbar's own
// wl_output objects).
void create_taskbar_layers(TaskbarApp& app) {
  if (!app.wl || app.wlError) return;
  const std::string tbOut =
      eh::shell::trim_output_assign(eh::config::shell_config_snapshot().taskbar.outputName);

  if (eh::shell::output_assign_is_all_displays(tbOut)) {
    auto outputs = app.wl->logical_output_bounds();
    for (const auto& out : outputs) {
      if (out.output) eh::shell::taskbar::taskbar_add_layer(app, out.output);
    }
  } else {
    wl_output* target = nullptr;
    if (!eh::shell::output_assign_is_auto(tbOut) && !tbOut.empty()) {
      target = app.wl->output_by_name(tbOut);
    }
    if (!target) {
      auto outputs = app.wl->logical_output_bounds();
      for (const auto& out : outputs) {
        if (out.output) {
          target = out.output;
          if (out.global_x <= 0 && out.global_y <= 0) break;
        }
      }
    }
    if (target) eh::shell::taskbar::taskbar_add_layer(app, target);
  }
}

}  // namespace

int run_taskbar_standalone() {
  // Die with the supervisor even on hard kills (PDEATHSIG fires on parent exit,
  // so `pkill -9 EventHorizon` reaps us too).
  eh::proc::install_parent_death_guard();

  // Load config first: taskbar_init_on_display → taskbar_maybe_reload_settings
  // reads the config snapshot to resolve enabled / output / pin settings. Skip
  // palette generation here — it runs async in the supervisor and arrives via the
  // config.applied broadcast; blocking on it delays Wayland connect at login.
  eh::config::shell_config_reload_from_disk_now(true);
  const auto sc0 = eh::config::shell_config_snapshot();

  TaskbarApp app;
  app.settings = sc0.taskbar;

  if (!taskbar_init_on_display(app)) {
    std::cerr << "[horizon-taskbar] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  // Own toplevel tracking on this connection (mirrors horizon-dock, which is
  // real-session verified): the taskbar binds its own
  // zwlr_foreign_toplevel_manager_v1 (+ the extended toplevel list) so the running-app
  // snapshot stays live without the supervisor's ToplevelTracker seam.
  eh::wayland::ForeignToplevels toplevels;
  if (app.wl && app.wl->foreign_toplevel_manager()) {
    toplevels.attach(app.wl->foreign_toplevel_manager(), app.display);
    toplevels.set_visual_dirty_hook([&app]() {
      app.frameRedrawPending = true;
      taskbar_schedule_frame(app);
    });
    app.toplevels = &toplevels;
  } else {
    std::cerr << "[horizon-taskbar] no zwlr_foreign_toplevel_manager_v1; running apps will not show\n";
  }
  if (app.wl && app.wl->ext_foreign_toplevel_list()) {
    app.extToplevels = &app.wl->ext_foreign_toplevels();
  }
  app.compositorKind = detect_compositor_kind();

  // Deferred startup: tray bus + MPRIS listener + bluetooth slot hooks.
  taskbar_init_deferred_startup(app);

  create_taskbar_layers(app);

  // 1 s poll timer — the taskbar repaints / re-polls on it (mirrors the
  // supervisor's old taskbar_poll_timer_fd_).
  const int poll_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (poll_timer_fd >= 0) {
    itimerspec its{};
    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 0;
    its.it_value.tv_sec = 1;
    its.it_value.tv_nsec = 0;
    (void)timerfd_settime(poll_timer_fd, 0, &its, nullptr);
  }

  // IPC client: config.applied → reload + redraw (settings saves, palette,
  // drag-preview merge — the snapshot carries the overlay); command.request →
  // commands forwarded from the supervisor (taskbar.menu.toggle, settings.toggle).
  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app](std::string topic, std::string payload, std::vector<int> /*fds*/) {
        if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          const auto& sc = eh::config::shell_config_snapshot();
          // Nightlight state is supervisor-owned; mirror it into the paint
          // state so the app-drawer button reflects the true display state.
          eh::appdrawer::set_nightlight_active(sc.nightLight.enabled);
          taskbar_maybe_reload_settings(app, sc);
          taskbar_schedule_frame(app);
        } else if (topic == "command.request") {
          if (is_settings_command(payload)) {
            eh::settings::request_launch_settings();
          } else if (is_menu_command(payload)) {
            taskbar_toggle_menu(app);
          }
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
    }
  }

  // Nightlight button: the taskbar has no gamma control (the supervisor owns
  // the single gamma client); publish `nightlight.toggle` on the bus so the
  // supervisor flips the display, and the click handler flips the paint state
  // locally (existing null-gamma fallback).
  taskbar_set_nightlight_toggle_fn([&ipc]() { (void)ipc.publish("command.request", "nightlight.toggle"); });

  // Own settings watcher: raw file edits are not broadcast on the bus; each
  // child watches its own config (mirrors horizon-dock / horizon-desktop).
  const int settings_fd = eh::shell::shared::open_state_inotify();

  taskbar_write_pid_file(::getpid());

  bool running = true;
  eh::app::PollMuxLoop mux{};
  mux.wayland_display = app.display;
  mux.display_fd = wl_display_get_fd(app.display);
  mux.on_display = [&running]() { return running; };
  mux.on_idle_flush = [&app](bool) {
    // Honor output rebinds requested by taskbar_maybe_reload_settings.
    if (app.pendingOutputRebind) {
      app.pendingOutputRebind = false;
      create_taskbar_layers(app);
    }
    // Return freed heap pages to the OS so idle RSS does not creep.
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
  };
  mux.get_handlers = [&]() {
    std::vector<eh::app::FdHandler> handlers;
    handlers.reserve(8);
    if (app.trayEventFd >= 0) {
      handlers.emplace_back(app.trayEventFd, POLLIN, [&app](short) { taskbar_handle_tray(app); });
    }
    if (poll_timer_fd >= 0) {
      handlers.emplace_back(poll_timer_fd, POLLIN, [&app, poll_timer_fd](short) {
        uint64_t exp = 0;
        (void)read(poll_timer_fd, &exp, sizeof(exp));
        if (!app.enabled) return;
        app.frameRedrawPending = true;
        taskbar_schedule_frame(app);
      });
    }
    if (settings_fd >= 0) {
      handlers.emplace_back(settings_fd, POLLIN, [&app, settings_fd](short) {
        eh::shell::shared::drain_inotify(settings_fd, [&app]() {
          eh::config::shell_config_invalidate_light();
          const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
          taskbar_maybe_reload_settings(app, sc);
          taskbar_schedule_frame(app);
        });
      });
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
        handlers.emplace_back(cfd, POLLIN, [&ipc, cfd](short) { ipc.on_fd_ready(cfd); });
      }
    }
    if (app.wl) {
      auto& hyprRuntime = app.wl->runtime_registry().hyprland();
      const int hyprFd = hyprRuntime.pollHandle();
      if (hyprFd >= 0) {
        handlers.emplace_back(hyprFd, POLLIN, [&hyprRuntime](short revents) {
          hyprRuntime.handlePoll(revents);
        });
      }
    }
    if (app.mpris) {
      const int bfd = app.mpris->poll_fd();
      if (bfd >= 0) {
        handlers.emplace_back(bfd, POLLIN, [&app](short) {
          if (app.mpris) app.mpris->process_pending_events();
        });
      }
      const int efd = app.mpris->poll_event_fd();
      if (efd >= 0) {
        handlers.emplace_back(efd, POLLIN, [&app](short) {
          if (app.mpris) app.mpris->process_pending_events();
        });
      }
    }
    return handlers;
  };

  std::cout << "[horizon-taskbar] running pid=" << ::getpid() << " ipc=" << (ipc_ok ? 1 : 0)
            << " trayEventFd=" << app.trayEventFd << " pollTimerFd=" << poll_timer_fd
            << " settingsFd=" << settings_fd << "\n";

  (void)mux.run(&running);

  // Own tracking is shut down before the connection teardown in
  // taskbar_cleanup (the toplevel handles bind proxies on app.wl).
  app.toplevels = nullptr;
  app.extToplevels = nullptr;
  toplevels.shutdown();
  taskbar_cleanup(app);
  taskbar_unlink_pid_file();
  if (poll_timer_fd >= 0) (void)close(poll_timer_fd);
  if (settings_fd >= 0) (void)close(settings_fd);
  std::cout << "[horizon-taskbar] exit\n";
  return 0;
}

}
