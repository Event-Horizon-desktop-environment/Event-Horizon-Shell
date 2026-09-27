// horizon-stage standalone child — brand-new event loop.
// Own Wayland connection, own toplevel tracking, overview Host on its own
// second connection, Super-key ownership (overview toggle, GNOME parity),
// 1s poll timer, settings inotify, IPC config.applied/command.request.

#define _GNU_SOURCE 1
#include "desktop_shell/stage/standalone/stage_standalone.hpp"

#include "desktop_shell/stage/core/stage_app.hpp"
#include "desktop_shell/stage/spawn/stage_spawn.hpp"
#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/dashboard/dashboard_dispatch.hpp"
#include "desktop_shell/dashboard/dashboard_surface.hpp"
#include "desktop_shell/osd/host/osd_host.hpp"
#include "desktop_shell/osd/audio/osd_audio.hpp"
#include "desktop_shell/osd/brightness/osd_brightness.hpp" 
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "services/network/core/network_manager_service.hpp"

#include "services/process/parent_death_guard.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/mem/periodic_trim.hpp"
#include "desktop_shell/shared/core/config_watch.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "services/global_keyboard/global_keyboard_handler.hpp"
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

#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::stage {

namespace {

bool is_overview_command(const std::string& payload) {
  return payload == "overview.toggle" || payload == "overview.open" || payload == "overview.close";
}

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

bool bind_toplevel_tracking(StageApp& app) {
  if (!app.display) return false;
  wl_registry* registry = wl_display_get_registry(app.display);
  if (!registry) return false;
  ToplevelBindCtx ctx{app.display, &app.toplevels, nullptr};
  wl_registry_add_listener(registry, &kToplevelBindListener, &ctx);
  wl_display_flush(app.display);
  (void)wl_display_roundtrip(app.display);
  wl_registry_destroy(registry);
  app.toplevelsBound = (ctx.manager != nullptr);
  return app.toplevelsBound;
}

}  // namespace

namespace {

void stage_pointer_enter(void* data, wl_pointer*, std::uint32_t, wl_surface* surface, wl_fixed_t sx,
                         wl_fixed_t sy) {
  auto& app = *static_cast<StageApp*>(data);
  app.pointerSurface = surface;
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  if (surface == app.dash.trigSurface) {
    eh::shell::dashboard::dashboard_trigger_enter(app);
    return;
  }
  if (surface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_enter(app);
    return;
  }
}

void stage_pointer_leave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
  auto& app = *static_cast<StageApp*>(data);
  if (surface && app.pointerSurface == surface) app.pointerSurface = nullptr;
  if (surface == app.dash.trigSurface) return;  // strip toggles on enter; leaves never act
  if (surface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_leave(app);
    return;
  }
}

void stage_pointer_motion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
  auto& app = *static_cast<StageApp*>(data);
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  if (app.pointerSurface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_motion(app);
    return;
  }
}

void stage_pointer_button(void* data, wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button,
                          std::uint32_t state) {
  auto& app = *static_cast<StageApp*>(data);
  const bool left = (button == 0x110);
  if (!left) return;
  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    // Dashboard slider drag end (checks internal drag state itself).
    (void)eh::shell::dashboard::dashboard_button_release(app);
    return;
  }
  if (app.pointerSurface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_press(app, serial);
    return;
  }
}

#ifdef EH_HAVE_POINTER_WARP
void stage_pointer_warp(void* data, wl_pointer*, wl_fixed_t sx, wl_fixed_t sy) {
  stage_pointer_motion(data, nullptr, 0, sx, sy);
}
#endif

void stage_pointer_frame(void*, wl_pointer*) {}
void stage_pointer_axis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
  auto& app = *static_cast<StageApp*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  const double dv = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dv * 20.0));
  (void)eh::shell::dashboard::dashboard_axis(app, deltaPx);
}
void stage_pointer_axis_source(void*, wl_pointer*, std::uint32_t) {}
void stage_pointer_axis_stop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void stage_pointer_axis_discrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void stage_pointer_axis_value120(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void stage_pointer_axis_relative_direction(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}

const wl_pointer_listener kStagePointerListener = {
    .enter = stage_pointer_enter,
    .leave = stage_pointer_leave,
    .motion = stage_pointer_motion,
    .button = stage_pointer_button,
    .axis = stage_pointer_axis,
    .frame = stage_pointer_frame,
    .axis_source = stage_pointer_axis_source,
    .axis_stop = stage_pointer_axis_stop,
    .axis_discrete = stage_pointer_axis_discrete,
    .axis_value120 = stage_pointer_axis_value120,
    .axis_relative_direction = stage_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
    .warp = stage_pointer_warp,
#endif
};

void stage_keyboard_keymap(void*, wl_keyboard*, std::uint32_t, int fd, std::uint32_t) {
  if (fd >= 0) close(fd);
}
void stage_keyboard_enter(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface, wl_array*) {
  auto& app = *static_cast<StageApp*>(data);
  // Escape-to-close only applies while the dashboard panel holds keyboard
  // focus (the layer surface requests focus on demand).
  app.dash.kbdFocus = (surface != nullptr && surface == app.dash.panelSurface);
}
void stage_keyboard_leave(void* data, wl_keyboard*, std::uint32_t, wl_surface*) {
  auto& app = *static_cast<StageApp*>(data);
  app.dash.kbdFocus = false;
}
void stage_keyboard_key(void* data, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t keycode,
                          std::uint32_t state) {
  auto& app = *static_cast<StageApp*>(data);
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;
  // Raw evdev keycode for Escape is 1 (Wayland reports pre-xkb codes, which
  // is why the dock adds +8 for xkb translation). Layout-independent.
  if (keycode == 1 && app.dash.kbdFocus) eh::shell::dashboard::dashboard_close(app);
}
void stage_keyboard_modifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t) {}
void stage_keyboard_repeat_info(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

const wl_keyboard_listener kStageKeyboardListener = {
    .keymap = stage_keyboard_keymap,
    .enter = stage_keyboard_enter,
    .leave = stage_keyboard_leave,
    .key = stage_keyboard_key,
    .modifiers = stage_keyboard_modifiers,
    .repeat_info = stage_keyboard_repeat_info,
};

}  // namespace

int run_stage_standalone() {
  eh::proc::install_parent_death_guard();
  eh::config::shell_config_reload_from_disk_now(true);

  StageApp app;
  if (!stage_init_on_display(app)) {
    std::cerr << "[horizon-stage] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  if (!bind_toplevel_tracking(app))
    std::cerr << "[horizon-stage] no zwlr_foreign_toplevel_manager_v1; overview windows will not list\n";
  if (app.wl && app.wl->ext_foreign_toplevel_list()) app.extToplevels = &app.wl->ext_foreign_toplevels();
  app.compositorKind = detect_compositor_kind();

  stage_build_overview_ctx(app);
  {
    auto ovConn = std::make_unique<eh::wayland::WaylandConnection>();
    if (!ovConn->connect(false)) {
      std::cerr << "[horizon-stage] overview own connection unavailable\n";
      ovConn.reset();
    }
    app.overview = std::make_unique<eh::shell::overview::Host>(app.overviewCtx, std::move(ovConn));
  }

  if (app.seat) {
    if (wl_pointer* pointer = wl_seat_get_pointer(app.seat))
      wl_pointer_add_listener(pointer, &kStagePointerListener, &app);
    if (wl_keyboard* keyboard = wl_seat_get_keyboard(app.seat))
      wl_keyboard_add_listener(keyboard, &kStageKeyboardListener, &app);
  }

  // Dashboard services: async weather engine, network + bluetooth state, and
  // MPRIS for the media card (mirrors what the dock used to host).
  eh::shell::dock_slot_hooks::control_center_weather_async_init();
  const int weather_wake_fd = eh::shell::dock_slot_hooks::control_center_weather_async_wake_fd();
  eh::net::NetworkManagerService::instance().start();
  eh::shell::dock_slot_hooks::bluetooth_widget_init();
  try {
    app.mpris = std::make_unique<eh::mpris::DockMpris>();
    (void)app.mpris->poll_refresh();
  } catch (...) {
    app.mpris.reset();
  }
  if (!eh::shell::osd::osd_env_disabled()) {
    app.osd = std::make_unique<eh::shell::osd::OsdHost>();
    app.osd->init(app);
    eh::shell::osd::osd_audio_bind(app);
    eh::shell::osd::osd_audio_apply_saved_defaults();
    eh::shell::osd::osd_audio_poll_pending(app);
  }
  {
    const auto& sc0 = eh::config::shell_config_snapshot();
    eh::shell::dashboard::dashboard_on_config_changed(app, sc0.dashboard);
    eh::shell::dashboard::dashboard_ensure_trigger(app);
  }

  int poll_timer_fd = -1;
  int settings_fd = -1;
  auto install_loop_fds = [&]() {
    if (poll_timer_fd < 0) {
      poll_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
      if (poll_timer_fd < 0) {
        std::cerr << "[horizon-stage] poll timer unavailable (" << std::strerror(errno) << ")\n";
      } else {
        itimerspec its{};
        its.it_interval.tv_sec = 1;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 1;
        its.it_value.tv_nsec = 0;
        (void)timerfd_settime(poll_timer_fd, 0, &its, nullptr);
      }
    }
    if (settings_fd < 0) {
      settings_fd = eh::shell::shared::open_state_inotify();
      if (settings_fd < 0) std::cerr << "[horizon-stage] settings watch unavailable\n";
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
          app.icons.set_icon_theme(sc.dock.iconTheme);
          stage_refresh_outputs(app);
          eh::shell::dashboard::dashboard_on_config_changed(app, sc.dashboard);
        } else if (topic == "command.request") {
          if (payload == "settings.toggle") {
            eh::settings::request_launch_settings();
          } else if (is_overview_command(payload) && app.overview) {
            if (payload == "overview.toggle") {
              app.overview->toggle();
            } else if (payload == "overview.open") {
              if (!app.overview->is_open()) app.overview->toggle();
            } else if (payload == "overview.close") {
              if (app.overview->is_open()) app.overview->close();
            }
          } else if (payload == "dashboard.toggle") {
            if (app.dash.open) eh::shell::dashboard::dashboard_close(app);
            else eh::shell::dashboard::dashboard_open(app);
          } else if (payload == "dashboard.open") {
            if (!app.dash.open) eh::shell::dashboard::dashboard_open(app);
          } else if (payload == "dashboard.close") {
            if (app.dash.open) eh::shell::dashboard::dashboard_close(app);
          }
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
      // This child runs its own DockMpris: forward notify.push frames so
      // now-playing toasts (and album art) reach horizon-notifications.
      eh::notify::install_ipc_sender(ipc);
    }
  }

  // Super-key ownership lives here now. Windows parity (and the user's
  // expectation): Super opens the start menu when a start-menu widget
  // exists, and only falls back to the overview otherwise — so the key
  // keeps working with the dock turned off. The dock no longer grabs
  // Super — see the removal in dock_standalone.
  eh::service::GlobalKeyboardHandler global_keyboard;
  global_keyboard.init([&app, &ipc]() {
    const auto has_type = [](const std::vector<std::string>& list, const char* type) {
      for (const auto& w : list) {
        if (eh::config::widget_implementation_type(w) == type) return true;
      }
      return false;
    };
    const auto& sc = eh::config::shell_config_snapshot();
    const bool dockMenu = has_type(sc.dock.leftWidgets, "smenu") ||
                          has_type(sc.dock.centerWidgets, "smenu") ||
                          has_type(sc.dock.rightWidgets, "smenu") ||
                          has_type(sc.dock.leftWidgets, "app_drawer") ||
                          has_type(sc.dock.centerWidgets, "app_drawer") ||
                          has_type(sc.dock.rightWidgets, "app_drawer");
    const bool taskbarMenu = has_type(sc.taskbar.leftWidgets, "smenu") ||
                             has_type(sc.taskbar.centerWidgets, "smenu") ||
                             has_type(sc.taskbar.rightWidgets, "smenu") ||
                             has_type(sc.taskbar.leftWidgets, "app_menu") ||
                             has_type(sc.taskbar.centerWidgets, "app_menu") ||
                             has_type(sc.taskbar.rightWidgets, "app_menu") ||
                             has_type(sc.taskbar.leftWidgets, "app_drawer") ||
                             has_type(sc.taskbar.centerWidgets, "app_drawer") ||
                             has_type(sc.taskbar.rightWidgets, "app_drawer");
    if (dockMenu) {
      (void)ipc.publish("command.request", "menu.toggle");
    } else if (taskbarMenu) {
      (void)ipc.publish("command.request", "taskbar.menu.toggle");
    } else if (app.overview) {
      app.overview->toggle();
    }
  });

  stage_write_pid_file(::getpid());

  bool running = true;
  eh::app::PollMuxLoop mux{};
  mux.wayland_display = app.display;
  mux.display_fd = wl_display_get_fd(app.display);
  mux.on_display = [&running]() { return running; };
  mux.on_idle_flush = [&app, &install_loop_fds, &poll_timer_fd, &settings_fd](bool did_display_event) {
    if (poll_timer_fd < 0 || settings_fd < 0) {
      static std::uint64_t lastHealMs = 0;
      const std::uint64_t nowMs = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count());
      if (nowMs - lastHealMs > 5000) {
        lastHealMs = nowMs;
        install_loop_fds();
      }
    }
    if (!did_display_event && app.overview && app.overview->display())
      (void)wl_display_flush(app.overview->display());
    static eh::shell::shared::PeriodicTrim trim;
    trim.tick();
  };
  mux.get_handlers = [&]() {
    std::vector<eh::app::FdHandler> handlers;
    handlers.reserve(6);
    if (poll_timer_fd >= 0)
      handlers.emplace_back(poll_timer_fd, POLLIN, [&app, &poll_timer_fd](short) {
        std::uint64_t exp = 0;
        (void)read(poll_timer_fd, &exp, sizeof(exp));
        stage_refresh_outputs(app);
        eh::shell::dashboard::dashboard_timer_tick(app);
        // Revive a stalled reveal animation (e.g. frames never flowed).
        if (app.shellAnim.has_active()) eh::shell::stage::stage_dashboard_kick_frames(app);
        eh::shell::osd::osd_brightness_poll(app);
        eh::shell::osd::osd_audio_poll_pending(app);
        if (app.mpris) {
          try {
            (void)app.mpris->poll_refresh();
          } catch (...) {
          }
        }
      });
    if (weather_wake_fd >= 0)
      handlers.emplace_back(weather_wake_fd, POLLIN, [](short) {
        eh::shell::dock_slot_hooks::control_center_weather_async_handle_wake();
      });
    if (settings_fd >= 0)
      handlers.emplace_back(settings_fd, POLLIN, [&app, &settings_fd](short) {
        eh::shell::shared::drain_inotify(settings_fd, [&app]() {
          eh::config::shell_config_invalidate_light();
          const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
          stage_refresh_outputs(app);
          eh::shell::dashboard::dashboard_on_config_changed(app, sc.dashboard);
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
    if (app.overview && app.overview->display()) {
      const int evFd = app.overview->hyprland_event_fd();
      if (evFd >= 0) {
        auto* ovRaw = app.overview.get();
        handlers.emplace_back(evFd, POLLIN, [ovRaw](short) { ovRaw->drain_hyprland_events(); });
      }
    }
    return handlers;
  };

  // Isolated Wayland connection for overview event dispatch.
  if (app.overview && app.overview->display()) {
    auto* ovDisplay = app.overview->display();
    eh::app::PollMuxDisplay ov{};
    ov.display = ovDisplay;
    ov.fd = wl_display_get_fd(ovDisplay);
    ov.on_dispatch = []() { return true; };
    ov.on_error = [&mux, ovDisplay]() {
      std::cerr << "[overview] Wayland protocol error on its own display\n";
      for (auto& ed : mux.extra_displays) {
        if (ed.display == ovDisplay) {
          ed.fd = -1;
          ed.display = nullptr;
          break;
        }
      }
    };
    mux.extra_displays.push_back(std::move(ov));
  }

  std::cout << "[horizon-stage] running pid=" << ::getpid() << " ipc=" << (ipc_ok ? 1 : 0) << "\n";
  (void)mux.run(&running);

  app.overview.reset();
  eh::shell::dashboard::dashboard_shutdown(app);
  eh::shell::osd::osd_audio_shutdown();
  if (app.osd) {
    app.osd->shutdown();
    app.osd.reset();
  }
  stage_cleanup(app);
  stage_unlink_pid_file();
  if (poll_timer_fd >= 0) (void)close(poll_timer_fd);
  if (settings_fd >= 0) (void)close(settings_fd);
  std::cout << "[horizon-stage] exit\n";
  return 0;
}

}  // namespace eh::shell::stage
