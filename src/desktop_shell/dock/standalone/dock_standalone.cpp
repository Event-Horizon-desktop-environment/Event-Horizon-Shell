#define _GNU_SOURCE 1
#include "desktop_shell/dock/standalone/dock_standalone.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/dock/spawn/dock_spawn.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/launchpad/host/launchpad_host.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

#include "services/global_keyboard/global_keyboard_handler.hpp"
#include "services/process/parent_death_guard.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/gpu_page_trim.hpp"

#include <chrono>
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

bool is_settings_command(const std::string& payload) {
  return payload == "settings.toggle";
}

bool is_launchpad_command(const std::string& payload) {
  return payload == "launchpad.toggle";
}

bool is_overview_command(const std::string& payload) {
  return payload == "overview.toggle" || payload == "overview.open" || payload == "overview.close";
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

  // Load config first: dock_init_on_display → create_layers reads the config
  // snapshot to resolve the dock output / height. Skip palette generation here — it is
  // run asynchronously by the supervisor and applied via the config.applied
  // broadcast; running it synchronously here blocks the dock from connecting
  // to Wayland for seconds when a wallpaper is configured.
  eh::config::shell_config_reload_from_disk_now(true);
  const auto sc0 = eh::config::shell_config_snapshot();

  DockApp app;
  app.settings = sc0.dock;
  app.dockRendererBackend = sc0.renderer;

  wl_display* dpy = wl_display_connect(nullptr);
  if (!dpy) {
    std::cerr << "[horizon-dock] WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  if (!dock_init_on_display(app, dpy)) {
    std::cerr << "[horizon-dock] dock init failed\n";
    wl_display_disconnect(dpy);
    return 1;
  }

  // Launchpad + overview hosts on their own Wayland connections — parity with
  // the supervisor's old in-process wiring (session run(): lpConn/ovConn).
  std::unique_ptr<eh::shell::launchpad::Host> launchpad_host_;
  std::unique_ptr<eh::shell::overview::Host> overview_host_;
  {
    auto lpConn = std::make_unique<eh::wayland::WaylandConnection>();
    if (!lpConn->connect(false)) {
      std::cerr << "[horizon-dock] launchpad own connection unavailable\n";
      lpConn.reset();
    }
    launchpad_host_ = std::make_unique<eh::shell::launchpad::Host>(app, std::move(lpConn));
    app.launchpad = launchpad_host_.get();

    auto ovConn = std::make_unique<eh::wayland::WaylandConnection>();
    if (!ovConn->connect(false)) {
      std::cerr << "[horizon-dock] overview own connection unavailable\n";
      ovConn.reset();
    }
    overview_host_ = std::make_unique<eh::shell::overview::Host>(app, std::move(ovConn));
  }

  app.launch_settings_override = +[]() { eh::settings::request_launch_settings(); };
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
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app, &launchpad_host_, &overview_host_](std::string topic, std::string payload,
                                                                       std::vector<int> /*fds*/) {
        if (topic == "config.applied") {
          dock_sync_settings_from_drag_preview(app);
          eh::config::shell_config_reload_from_disk_now();
          dock_maybe_reload_settings(app, "applied");
          dock_schedule_frame(app);
        } else if (topic == "command.request") {
          if (is_settings_command(payload)) {
            eh::settings::request_launch_settings();
          } else if (is_menu_command(payload)) {
            dock_toggle_menu_from_keyboard(app);
          } else if (is_launchpad_command(payload)) {
            if (launchpad_host_ && launchpad_host_->display()) {
              launchpad_host_->toggle(0, 0);
              wl_display_flush(launchpad_host_->display());
            }
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
          }
        }
      });
      ipc_ok = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
    }
  }

  dock_write_pid_file(::getpid());

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
    if (!deferred_done) {
      deferred_done = true;
      dock_init_deferred_startup(app);
      // Startup init (Vulkan pipelines, first uploads, icon decode) faults in
      // the bulk of the NVIDIA userspace text. Drop it the moment settle is
      // detected instead of waiting for idle heuristics to catch up.
      const uint64_t startup_dropped = eh::gpu::force_trim_gpu_pages();
      if (startup_dropped > 0) {
        std::fprintf(stderr,
                     "[dock-mem] startup gpu page trim: dropped %.1fMB\n",
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
            std::fprintf(stderr, "[dock-mem] gpu page trim: dropped %.1fMB of idle NVIDIA mappings\n",
                         static_cast<double>(dropped) / (1024.0 * 1024.0));
          }
        }
      }
    }

    if (!did_display_event) {
      if (launchpad_host_ && launchpad_host_->display())
        (void)wl_display_flush(launchpad_host_->display());
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

  // Isolated Wayland connections for launchpad + overview.
  if (launchpad_host_ && launchpad_host_->display()) {
    auto* lpDisplay = launchpad_host_->display();
    eh::app::PollMuxDisplay lp{};
    lp.display = lpDisplay;
    lp.fd = wl_display_get_fd(lpDisplay);
    lp.on_dispatch = []() { return true; };
    lp.on_error = [&mux, &launchpad_host_, lpDisplay]() {
      std::cerr << "[launchpad] Wayland protocol error on its own display\n";
      if (launchpad_host_) launchpad_host_->detach_vk();
      for (auto& ed : mux.extra_displays) {
        if (ed.display == lpDisplay) {
          ed.fd = -1;
          ed.display = nullptr;
          break;
        }
      }
    };
    mux.extra_displays.push_back(std::move(lp));
  }
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

  dock_cleanup(app, true);
  dock_unlink_pid_file();
  std::cout << "[horizon-dock] exit\n";
  return 0;
}

}
