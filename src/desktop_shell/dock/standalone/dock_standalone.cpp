#define _GNU_SOURCE 1
#include "desktop_shell/dock/standalone/dock_standalone.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/dock/core/dock_boot_log.hpp"
#include "desktop_shell/dock/spawn/dock_spawn.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/dashboard/dashboard_dispatch.hpp"
#include "desktop_shell/controlcenter/input/control_center_bus_hook.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include "services/global_keyboard/global_keyboard_handler.hpp"
#include "services/process/parent_death_guard.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/gpu_page_trim.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef EH_USE_JEMALLOC
#include <jemalloc/jemalloc.h>
#else
#include <malloc.h>
#endif

#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <poll.h>
#include <unistd.h>

#include <wayland-client.h>

namespace eh::shell::dock {

namespace {

// Intercept every client-side wl_log() call (protocol errors go through
// wl_log in libwayland) and prefix it with the boot step that was in progress,
// so a compositor kill is attributable: "wl_surface#N: error C: msg" becomes
// "while step N (foo): wl_surface#N: error C: msg". Installed once at the top
// of run_dock_standalone(); applies to every display in this process.
void dock_wl_log_handler(const char* fmt, va_list args) {
  std::fprintf(stderr, "[horizon-dock] wl_log while step %d (%s): ",
               dock_boot_step_index(), dock_boot_current_step());
  std::vfprintf(stderr, fmt, args);
  std::fflush(stderr);
}

bool is_settings_command(const std::string& payload) {
  return payload == "settings.toggle";
}


bool is_overview_command(const std::string& payload) {
  return payload == "overview.toggle" || payload == "overview.open" || payload == "overview.close";
}

bool is_dashboard_command(const std::string& payload) {
  return payload == "dashboard.toggle" || payload == "dashboard.open" || payload == "dashboard.close";
}

bool is_menu_command(const std::string& payload) {
  return payload == "menu.toggle";
}

// Super toggles the dock's start-menu/app-drawer widget (moved here with the
// dock; the supervisor's handler keeps the taskbar side). Priority: smenu first
// (matches the old in-process global-keyboard behaviour).
void dock_toggle_menu_from_keyboard(DockApp& app) {
  const auto has_widget = [](const std::vector<std::string>& list, const char* type) {
    for (const auto& w : list) {
      if (eh::config::widget_implementation_type(w) == type) return true;
    }
    return false;
  };
  const auto check = [&](const auto& left, const auto& center, const auto& right, const char* type) {
    return has_widget(left, type) || has_widget(center, type) || has_widget(right, type);
  };

  const bool smenu = check(app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets, "smenu");
  const bool drawer = check(app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets, "app_drawer");
  if (!smenu && !drawer) {
    std::cerr << "[horizon-dock] no start_menu/app_drawer widget found on dock\n";
    return;
  }
  if (app.popupOpen && app.popupKind == DockApp::PopupKind::AppMenu) {
    popup_close(app);
  } else {
    if (smenu) {
      app.appMenuSmenuMode = true;
      eh_app_drawer_update_categories(app);
    }
    popup_open_app_menu(app, 0, 0);
  }
  wl_display_flush(app.display);
}

}  // namespace

int run_dock_standalone() {
  // Die with the supervisor even on hard kills (PDEATHSIG fires on parent exit,
  // so `pkill -9 EventHorizon` reaps us too).
  eh::proc::install_parent_death_guard();

  // Route every wl_log (protocol errors included) through the step-aware
  // reporter before anything can fail; this must be the first Wayland-adjacent
  // setup so a kill during connect/roundtrip is still attributable.
  wl_log_set_handler_client(dock_wl_log_handler);

  dock_boot_step("boot begin pid=%d", static_cast<int>(::getpid()));

  // Load config first: dock_init_on_display → create_layers reads the config
  // snapshot to resolve the dock output / height. Skip palette generation here — it is
  // run asynchronously by the supervisor and applied via the config.applied
  // broadcast; running it synchronously here blocks the dock from connecting
  // to Wayland for seconds when a wallpaper is configured.
  dock_boot_step("load config");
  eh::config::shell_config_reload_from_disk_now(true);
  const auto sc0 = eh::config::shell_config_snapshot();

  DockApp app;
  app.settings = sc0.dock;
  app.dockRendererBackend = sc0.renderer;

  dock_boot_step("connect wayland (WAYLAND_DISPLAY=%s)",
                 std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "<unset>");
  wl_display* dpy = wl_display_connect(nullptr);
  if (!dpy) {
    std::cerr << "[horizon-dock] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  dock_boot_step("wayland connected, init dock");
  if (!dock_init_on_display(app, dpy)) {
    std::cerr << "[horizon-dock] dock init failed\n";
    wl_display_disconnect(dpy);
    return 1;
  }
  dock_boot_step("dock_init_on_display done configured=%d", app.configured ? 1 : 0);
  {
    // Identify the display fd/socket now so a later before_ppoll EPIPE can be
    // told apart from an fd being closed and recycled underneath us.
    const int dfd = wl_display_get_fd(dpy);
    char link[256] = "<none>";
    if (dfd >= 0) {
      char path[64];
      std::snprintf(path, sizeof(path), "/proc/self/fd/%d", dfd);
      const ssize_t n = ::readlink(path, link, sizeof(link) - 1);
      if (n > 0) link[n] = '\0';
    }
    std::cerr << "[horizon-dock] wayland display fd=" << dfd << " " << link << "\n";
  }

  // Overview host on its own Wayland connection — parity with
  // the supervisor's old in-process wiring (session run(): ovConn).
  std::unique_ptr<eh::shell::overview::Host> overview_host_;
  {
    dock_boot_step("overview host connect");
    auto ovConn = std::make_unique<eh::wayland::WaylandConnection>();
    if (!ovConn->connect(false)) {
      std::cerr << "[horizon-dock] overview own connection unavailable\n";
      ovConn.reset();
    }
    overview_host_ = std::make_unique<eh::shell::overview::Host>(app, std::move(ovConn));
  }

  app.launch_settings_override = +[]() { eh::settings::request_launch_settings(); };
  dock_boot_step("install loop fds");
  dock_install_loop_fds(app);

  // Own the global-keyboard menu toggle: the dock's start-menu/app-drawer moved
  // here with the dock, so Super toggles it from this process. No settingsFn —
  // the supervisor keeps Super+S (launch/toggle settings) to avoid double-fire.
  eh::service::GlobalKeyboardHandler global_keyboard;
  global_keyboard.init([&app]() { dock_toggle_menu_from_keyboard(app); });

  // Weather async engine for the control-center widget (supervisor used to
  // wire its wake fd + curl drive from on_idle_flush).
  eh::shell::dock_slot_hooks::control_center_weather_async_init();
  const int weather_wake_fd = eh::shell::dock_slot_hooks::control_center_weather_async_wake_fd();

  // IPC client: config.applied → reload + redraw; command.request → commands
  // forwarded from the supervisor (settings / launchpad / overview / menu).
  eh::ipc::IpcClient ipc;
  bool ipc_ok = false;
  {
    dock_boot_step("ipc connect");
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app, &overview_host_](std::string topic, std::string payload,
                                                                       std::vector<int> /*fds*/) {
        if (topic == "config.applied") {
          dock_sync_settings_from_drag_preview(app);
          eh::config::shell_config_reload_from_disk_now();
          dock_maybe_reload_settings(app, "applied");
          eh_app_drawer_set_nightlight_active(eh::config::shell_config_snapshot().nightLight.enabled);
          dock_schedule_frame(app);
        } else if (topic == "command.request") {
          if (is_settings_command(payload)) {
            eh::settings::request_launch_settings();
          } else if (is_menu_command(payload)) {
            dock_toggle_menu_from_keyboard(app);
          } else if (is_overview_command(payload)) {
            if (overview_host_) {
              if (payload == "overview.toggle") {
                overview_host_->toggle();
              } else if (payload == "overview.open") {
                if (!overview_host_->is_open()) overview_host_->toggle();
              } else if (payload == "overview.close") {
                if (overview_host_->is_open()) overview_host_->close();
              }
            }
          } else if (is_dashboard_command(payload)) {
            if (payload == "dashboard.toggle") {
              if (app.dash.open) eh::shell::dashboard::dashboard_close(app);
              else eh::shell::dashboard::dashboard_open(app);
            } else if (payload == "dashboard.open") {
              if (!app.dash.open) eh::shell::dashboard::dashboard_open(app);
            } else if (payload == "dashboard.close") {
              if (app.dash.open) eh::shell::dashboard::dashboard_close(app);
            }
          }
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
    }
  }

  // Control Centre child -> supervisor commands (DND / nightlight / color
  // scheme toggles from the PearCenter compact popup). Mirrors the taskbar
  // nightlight pattern: publish `command.request`, the supervisor self-client
  // applies it (it owns gamma + the config snapshot).
  eh::shell::dock::control_center::control_center_set_bus_publish_fn(
      [&ipc](const std::string& payload) { (void)ipc.publish("command.request", payload); });

  dock_write_pid_file(::getpid());
  dock_boot_step("pid file written, entering event loop");

  bool running = true;
  bool deferred_done = false;
  eh::app::PollMuxLoop mux{};
  app.eventLoopMux = &mux;
  mux.wayland_display = dpy;
  mux.display_fd = wl_display_get_fd(dpy);
  mux.on_display = [&app, &running]() {
    dock_after_display_dispatch(app);
    return running && app.running;
  };
  mux.on_idle_flush = [&](bool did_display_event) {
    if (app.exitRequested) {
      std::cerr << "[horizon-dock] show_dock disabled, exiting\n";
      running = false;
      app.running = false;
      return;
    }
    {
      static uint64_t lastFdHealMs = 0;
      const uint64_t nowMs = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count());
      if ((app.pollTimerFd < 0 || app.settingsInotifyFd < 0) && nowMs - lastFdHealMs > 5000) {
        lastFdHealMs = nowMs;
        dock_install_loop_fds(app);
      }
    }
    if (!deferred_done) {
      deferred_done = true;
      dock_boot_step("deferred startup begin (first idle)");
      dock_init_deferred_startup(app);
      // Startup init (Vulkan pipelines, first uploads, icon decode) faults in
      // the bulk of the NVIDIA userspace text. Drop it the moment settle is
      // detected instead of waiting for idle heuristics to catch up.
      const uint64_t startup_dropped = eh::gpu::force_trim_gpu_pages();
      if (startup_dropped > 0) {
        debug_log("dock", "startup gpu page trim: dropped %.1fMB",
                  static_cast<double>(startup_dropped) / (1024.0 * 1024.0));
      }
    }
    dock_try_start_deferred_tray(app);
    eh::shell::dock_slot_hooks::control_center_weather_drive_curl_multi();

    // Periodic allocator / font-cache maintenance (mirrors the supervisor's
    // loop): returns purged dirty pages and bounds Pango's glyph cache so
    // idle RSS does not creep over long sessions.
    {
      static uint64_t lastPurge = 0;
      static uint64_t lastFontClear = 0;
      const uint64_t now = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
              .count());
#if defined(EH_USE_JEMALLOC)
      if (now - lastPurge > 60000) {
        lastPurge = now;
        mallctl("arenas.purge", nullptr, nullptr, nullptr, 0);
      }
#else
      if (now - lastPurge > 60000) {
        lastPurge = now;
        malloc_trim(0);
      }
#endif
      if (now - lastFontClear > 120000) {
        lastFontClear = now;
        if (PangoFontMap* fm = pango_cairo_font_map_get_default())
          pango_fc_font_map_cache_clear(PANGO_FC_FONT_MAP(fm));
      }
      // Drop idle NVIDIA userspace text pages (gpucomp/glcore/glvkspirv can
      // pin ~90MB RSS after Vulkan init). Clean file-backed pages only:
      // they re-fault from page cache on the next activity burst.
      {
        static uint64_t lastGpuTrim = 0;
        if (now - lastGpuTrim > 1000) {
          lastGpuTrim = now;
          const uint64_t dropped = eh::gpu::trim_idle_gpu_pages(30000);
          if (dropped > 0) {
            debug_log("dock", "gpu page trim: dropped %.1fMB of idle NVIDIA mappings",
                       static_cast<double>(dropped) / (1024.0 * 1024.0));
          }
        }
      }
    }

    if (!did_display_event) {
      if (overview_host_ && overview_host_->display())
        (void)wl_display_flush(overview_host_->display());
    }
  };
  mux.get_handlers = [&]() {
    std::vector<eh::app::FdHandler> handlers;
    handlers.reserve(8);
    handlers.emplace_back(app.pollTimerFd, POLLIN, [&app, &running](short) {
      if (!running) return;
      dock_handle_timer(app);
    });
    if (app.mediaAnimTimerFd >= 0) {
      handlers.emplace_back(app.mediaAnimTimerFd, POLLIN, [&app, &running](short) {
        if (running) dock_handle_media_anim_timer(app);
      });
    }
    handlers.emplace_back(app.trayEventFd, POLLIN, [&app](short) { dock_handle_tray(app); });
    handlers.emplace_back(app.settingsInotifyFd, POLLIN, [&app](short) {
      dock_handle_inotify(app);
      dock_schedule_frame(app);
    });
    if (weather_wake_fd >= 0) {
      handlers.emplace_back(weather_wake_fd, POLLIN, [](short) {
        eh::shell::dock_slot_hooks::control_center_weather_async_handle_wake();
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
    // Compositor event socket for overview event-driven state updates.
    if (overview_host_) {
      const int evFd = overview_host_->hyprland_event_fd();
      if (evFd >= 0) {
        auto* ovRaw = overview_host_.get();
        handlers.emplace_back(evFd, POLLIN, [ovRaw](short) {
          ovRaw->drain_hyprland_events();
        });
      }
    }
    return handlers;
  };

  // Isolated Wayland connection for overview.
  if (overview_host_ && overview_host_->display()) {
    auto* ovDisplay = overview_host_->display();
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

  std::cout << "[horizon-dock] running pid=" << ::getpid() << " ipc=" << (ipc_ok ? 1 : 0)
            << " pollTimerFd=" << app.pollTimerFd << " trayEventFd=" << app.trayEventFd
            << " settingsInotifyFd=" << app.settingsInotifyFd << "\n";

  (void)mux.run(&running);

  dock_boot_step("event loop ended running=%d app.running=%d", running ? 1 : 0, app.running ? 1 : 0);

  // stderr (not stdout): the log file is block-buffered and the teardown
  // below can crash, which would take the buffered line with it.
  if (app.display) {
    std::cerr << "[horizon-dock] loop ended running=" << (running ? 1 : 0)
              << " app.running=" << (app.running ? 1 : 0)
              << " display_error=" << wl_display_get_error(app.display) << "\n";
  } else {
    std::cerr << "[horizon-dock] loop ended running=" << (running ? 1 : 0)
              << " app.running=" << (app.running ? 1 : 0) << " display_error=<no display>\n";
  }

  dock_cleanup(app, true);
  dock_unlink_pid_file();
  std::cout << "[horizon-dock] exit\n";
  return 0;
}

}
