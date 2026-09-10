#include "bootstrap/session/detail/unified_shell_session.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "desktop_shell/common/bench/memory_usage.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/desktop/spawn/desktop_spawn.hpp"
#include "desktop_shell/dock/spawn/dock_spawn.hpp"
#include "desktop_shell/taskbar/spawn/taskbar_spawn.hpp"
#include "desktop_shell/shared/toplevel/toplevel_tracker.hpp"
#include "desktop_shell/shared/core/config_watch.hpp"

#include "bootstrap/session/unified_shell.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"
#include "bootstrap/thread/thread_pool.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"
#include "services/notifications/notifications_spawn.hpp"
#include "desktop_shell/common/bench/bench_trace.hpp"
#include "desktop_shell/common/bench/bench_file.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"
#include "desktop_shell/common/glyph/bundled_fonts.hpp"
#include "desktop_shell/common/tray/tray_session_defer.hpp"
#include "desktop_shell/keyboard/keyboard_settings_apply.hpp"
#include "services/ipc/ipc_server.hpp"
#include "services/vram_boost/vram_boost_manager.hpp"
#include "services/windows/toplevel_bridge.hpp"
#include "desktop_shell/common/registry/widget_registry.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint.hpp"

#include "bootstrap/loop/poll_mux.hpp"
#include "backends/interfaces/compositor_ipc.h"
#include "backends/hyprland/hyprland_backends.h"

#include <execinfo.h>

#include <signal.h>
#include <unistd.h>
#include <malloc.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include "bootstrap/loop/wl_loop_diag.hpp"
#include "configuration/shell_config.hpp"
#include "configuration/shell_renderer_backend.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "desktop_shell/taskbar/core/taskbar.hpp"

#include "services/network/core/network_manager_service.hpp"
#include "services/autostart/autostart_service.hpp"

#include "desktop_shell/common/log/debug_log.hpp"

#if defined(EH_HAVE_POLKIT_AGENT)
#include "services/polkit/service/polkit_auth_service.hpp"
#endif

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <wayland-client.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <exception>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(EH_USE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#endif

namespace eh::app {
namespace detail {

struct DragPreviewUser {
  eh::wayland::GammaService* gamma = nullptr;
  eh::ipc::IpcService* ipc = nullptr;
};

// Push the persisted nightlight (gamma) settings onto the display. The split-out
// settings child writes the config but has no gamma control of its own, so without
// this the toggle/sliders would never reach the backlight. set_enabled only runs
// when the state actually changes, to avoid restarting the warm-up transition on
// every unrelated config apply.
static void apply_nightlight_config(eh::wayland::GammaService* gs, const eh::config::NightLightSettings& nl) {
  if (!gs) return;
  if (gs->enabled() != nl.enabled) {
    gs->set_enabled(nl.enabled);
  }
  if (nl.enabled) {
    gs->set_temperature(nl.nightTemperature);
  }
}

UnifiedShellSession::UnifiedShellSession(UnifiedShell& owner) : owner_(owner) {
   
  MANGOWM_INFO("UnifiedShellSession ctor this=%p owner=%p", (void*)this, (void*)&owner);
}
UnifiedShellSession::~UnifiedShellSession() {
   
  MANGOWM_INFO("UnifiedShellSession dtor this=%p", (void*)this);
}

// The drag preview halves live in the split-out children: the dock half in
// horizon-dock (dock_sync_settings_from_drag_preview + dock_schedule_frame),
// the taskbar half in horizon-taskbar — both re-apply the merged drag-preview
// overlay when the next config.applied broadcast lands. This process only
// forwards the applied config.

void unified_shell_config_applied_hook(void* user) {
   
  auto& data = *static_cast<DragPreviewUser*>(user);
  eh::settings::embed_request_redraw();

  // Sync nightlight from the freshly applied config (embedded settings saves go
  // through shell_config_apply_from_memory). The gamma service stays
  // supervisor-owned: the taskbar + settings still use it, and a compositor
  // gamma control can only have one owner (a second gamma client lurking in the
  // dock child would fight this one).
  apply_nightlight_config(data.gamma, eh::config::shell_config_snapshot_skip_matugen().nightLight);

  // Broadcast on the IPC bus so the split-out children (horizon-dock / desktop /
  // wallpaper) can re-apply locally. The payload is the aggregate config mtime
  // (sec.nsec); consumers compare it to skip no-ops.
  if (data.ipc && data.ipc->running()) {
    std::string payload;
    if (const auto mt = eh::config::aggregate_config_source_mtime()) {
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%lld.%09ld",
                    static_cast<long long>(mt->tv_sec), mt->tv_nsec);
      payload = buf;
    }
    data.ipc->publish("config.applied", payload);
  }
}

ShellRunMode parse_shell_run_mode(int argc, char** argv) {
   
  MANGOWM_DEBUG("parse_shell_run_mode argc=%d", argc);
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    if (!a) continue;
    if (std::strcmp(a, "--dock") == 0) return ShellRunMode::DockOnly;
    if (std::strcmp(a, "--both") == 0) return ShellRunMode::Both;
  }
  return ShellRunMode::Both;
}

void UnifiedShellSession::bench_log(const char* tag) {
   
  MANGOWM_DEBUG("bench tag=%s", tag);
  eh::bench::bench_write("spawn", tag);
  if (!bench_t0_) return;
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *bench_t0_).count();
  std::cerr << "[dock-bench] unified: " << tag << " cumulative_since_display_connect=" << ms << "ms\n";
}



void UnifiedShellSession::start_notifications_services() {
    
  MANGOWM_INFO("starting notification services");

  // Notifications live in the split-out `horizon-notifications` child: it
  // owns the NotificationManager, the org.freedesktop.Notifications D-Bus
  // service, and the toast host on its own connection. Producers just publish
  // `notify.push`; this process only installs a bus sender and spawns the child.
  eh::notify::setNotifySender([this](const std::string& payload, const std::vector<int>& /*fds*/) {
    if (!ipc_service_ || !ipc_service_->running()) return false;
    ipc_service_->publish(eh::notify::kPushTopic, payload);
    return true;
  });

  if (ipc_service_ && ipc_service_->running()) {
    const int pid = eh::shell::notifications::notifications_spawn_native_child();
    if (pid > 1) notifications_child_pid_ = pid;
  }
}

void UnifiedShellSession::stop_notifications_services() {
    
  MANGOWM_INFO("stopping notification services");
  if (notifications_child_pid_ > 1) {
    eh::shell::notifications::notifications_kill_native_child(notifications_child_pid_);
    notifications_child_pid_ = -1;
  }
  eh::notify::setNotifySender(nullptr);
}

std::vector<FdHandler> UnifiedShellSession::build_handlers() {
   
  std::vector<FdHandler> handlers;
  handlers.reserve(10);
  if (thumb_poll_fd_ >= 0) {
    handlers.emplace_back(thumb_poll_fd_, POLLIN, [this](short) { eh::settings::embed_wallpaper_thumbnail_poll(); });
  }
  if (weather_wake_fd_ >= 0) {
    handlers.emplace_back(
        weather_wake_fd_, POLLIN, [this](short) { eh::widgets::control_center_weather_async_handle_wake(); });
  }
  if (settings_inotify_fd_ >= 0) {
    // This process keeps its own settings watcher (the dock child has its own
    // inotify fd and reloads itself), so raw file edits pull keyboard/nightlight
    // straight from disk here too. Deliberately no config.applied broadcast on
    // this path: the palette must stay skipped on raw file edits (it only changes
    // with the wallpaper), matching the children's independent reload behavior.
    handlers.emplace_back(settings_inotify_fd_, POLLIN, [this](short) {
      eh::shell::shared::drain_inotify(settings_inotify_fd_, [this]() {
        eh::config::shell_config_reload_from_disk_now();
        const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
        // Once the initial startup palette load is done, watch for wallpaper
        // changes and schedule a deferred full reload with the palette.
        if (matugen_reloaded_ && sc.wallpaperImage != last_matugen_wallpaper_path_) {
          last_matugen_wallpaper_path_ = sc.wallpaperImage;
          matugen_pending_ = true;
        }
        eh::shell::keyboard::apply_settings(sc.keyboard);
        apply_nightlight_config(gamma_service_.get(), sc.nightLight);
        sync_widget_registry();
      });
    });
  }

  if (sigchld_fd_ >= 0) {
    // Reap the split-out children (wallpaper / notifications / desktop / dock /
    // taskbar) and respawn them if they die unexpectedly. They spawn with
    // POSIX_SPAWN_SETSID, so a crash in one can't take the session down.
    handlers.emplace_back(sigchld_fd_, POLLIN, [this](short) {
      struct signalfd_siginfo si {};
      while (read(sigchld_fd_, &si, sizeof(si)) == static_cast<ssize_t>(sizeof(si))) {
        int status = 0;
        if (wallpaper_child_pid_ > 1) {
          if (waitpid(wallpaper_child_pid_, &status, WNOHANG) == wallpaper_child_pid_) {
            wallpaper_child_pid_ = -1;
          }
        }
        if (notifications_child_pid_ > 1) {
          if (waitpid(notifications_child_pid_, &status, WNOHANG) == notifications_child_pid_) {
            notifications_child_pid_ = -1;
          }
        }
        if (desktop_child_pid_ > 1) {
          if (waitpid(desktop_child_pid_, &status, WNOHANG) == desktop_child_pid_) {
            std::cerr << "[desktop] child exited (status=" << status << ")\n";
            desktop_child_pid_ = -1;
            if (!running_) continue;
            const uint64_t now = eh::shell::now_mono_ms();
            if (now - desktopLastRestartMs_ < 5000) {
              debug_log("desktop", "child died; respawn RATE_LIMITED");
              continue;
            }
            desktopLastRestartMs_ = now;
            const int pid = eh::shell::desktop::desktop_spawn_native_child();
            if (pid > 1) desktop_child_pid_ = pid;
          }
        }
        if (dock_child_pid_ > 1) {
          if (waitpid(dock_child_pid_, &status, WNOHANG) == dock_child_pid_) {
            std::cerr << "[dock] child exited (status=" << status << ")\n";
            dock_child_pid_ = -1;
            if (!running_) continue;
            const uint64_t now = eh::shell::now_mono_ms();
            if (now - dockLastRestartMs_ < 5000) {
              debug_log("dock", "child died; respawn RATE_LIMITED");
              continue;
            }
            dockLastRestartMs_ = now;
            const int pid = eh::shell::dock::dock_spawn_native_child();
            if (pid > 1) dock_child_pid_ = pid;
          }
        }
        if (taskbar_child_pid_ > 1) {
          if (waitpid(taskbar_child_pid_, &status, WNOHANG) == taskbar_child_pid_) {
            std::cerr << "[taskbar] child exited (status=" << status << ")\n";
            taskbar_child_pid_ = -1;
            if (!running_) continue;
            const uint64_t now = eh::shell::now_mono_ms();
            if (now - taskbarLastRestartMs_ < 5000) {
              debug_log("taskbar", "child died; respawn RATE_LIMITED");
              continue;
            }
            taskbarLastRestartMs_ = now;
            const int pid = eh::shell::taskbar::taskbar_spawn_native_child();
            if (pid > 1) taskbar_child_pid_ = pid;
          }
        }
      }
    });
  }
  if (gamma_service_) {
    const int gfd = gamma_service_->transition_fd();
    if (gfd >= 0) {
      handlers.emplace_back(gfd, POLLIN, [this](short) { gamma_service_->on_transition_timer(); });
    }
  }
  if (ipc_service_) {
    // IPC bus: the listen fd plus every persistent framed client (with POLLOUT when
    // queued events are pending). The bus handles accept/dialect selection/
    // dispatch internally; on_fd_ready resolves pending connections and drops
    // dead ones, so the next build_handlers() pass picks up the fresh set.
    for (const auto& interest : ipc_service_->poll_interests()) {
      const int ifd = interest.fd;
      handlers.emplace_back(ifd, interest.events, [this, ifd](short revents) {
        ipc_service_->on_fd_ready(ifd, revents);
      });
    }
  }
  if (ipc_self_client_ && ipc_self_client_->connected()) {
    // Self-client on the bus (child -> supervisor requests). Delivers the
    // taskbar child's `nightlight.toggle` so the supervisor flips gamma.
    const int selfFd = ipc_self_client_->fd();
    handlers.emplace_back(selfFd, POLLIN, [this, selfFd](short) {
      ipc_self_client_->on_fd_ready(selfFd);
    });
  }
  {
    const int nfd = eh::net::NetworkManagerService::instance().wake_fd();
    if (nfd >= 0) {
      handlers.emplace_back(nfd, POLLIN, [](short) {
        eh::net::NetworkManagerService::instance().handle_wake();
      });
    }
  }
  return handlers;
}

bool UnifiedShellSession::on_after_wayland_dispatch() {
   
  {
    eh::app::wl_loop_diag::StallScope _stall("unified", "embed_after_display_dispatch");
    eh::settings::embed_after_display_dispatch();
  }
  // The dock (its own display dispatch + running flag) lives in the
  // horizon-dock child; here this display only carries the toplevel tracker.
  return true;
}

void UnifiedShellSession::on_idle_flush(bool did_display_event) {
   
  eh::settings::embed_after_display_dispatch();
  if (lock_screen_) lock_screen_->on_idle_flush();
  {
    static uint64_t lastLog = 0;
    static uint64_t lastBd = 0;
    const uint64_t now = eh::shell::now_mono_ms();
    if (now - lastLog > 10000) {
      lastLog = now;
      std::cerr << "[mem] rss=" << eh::shell::mem::current_rss_kb() << "kB\n";
    }
    if (now - lastBd > 60000) {
      lastBd = now;
      eh::shell::mem::log_mem_breakdown();
#if defined(EH_USE_JEMALLOC)
      mallctl("arenas.purge", nullptr, nullptr, nullptr, 0);
#else
      malloc_trim(0);
#endif
    }
    static uint64_t lastFontClear = 0;
    if (now - lastFontClear > 120000) {
      lastFontClear = now;
      if (auto* fm = pango_cairo_font_map_get_default())
        pango_fc_font_map_cache_clear(PANGO_FC_FONT_MAP(fm));
    }
  }
  static bool bundledFontsRegistered = false;
  if (!bundledFontsRegistered) {
    bundledFontsRegistered = true;
    eh::shell::register_bundled_fonts_once();
  }

  if (!bootstrap_done_) {
    bootstrap_done_ = true;
    bench_log("after_embed_init");
    if (bench_t0_) {
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - *bench_t0_).count();
      std::cerr << "[dock-bench] unified: SUMMARY after_deferred_bootstrap total=" << ms
                << "ms cumulative_since_display_connect — embed ready; async jobs continue after this line\n";
    }
    eh::bench::bench_write("spawn", "all_components_ready");

    // Launch the user's autostart entries (XDG autostart spec).
    // Only meaningful for a real desktop session, not a pure --eh-settings run that exits early.
    {
      const auto& sc = eh::config::shell_config_snapshot();
      if (sc.autostartEnabled) {
        eh::app::wl_loop_diag::StallScope _stall("unified", "autostart");
        auto entries = eh::autostart::scan_autostart_entries();
        eh::autostart::launch_all_autostart(entries);
      } else {
        debug_log("autostart", "disabled via config");
      }
    }
  }
  if (!did_display_event) {
    eh::app::wl_loop_diag::StallScope _stall("unified", "wl_display_flush_idle");
    (void)wl_display_flush(display_);
  }
  if (!matugen_reloaded_ && bootstrap_done_) {
    matugen_reloaded_ = true;
    eh::config::shell_config_trigger_async_matugen();
    eh::bench::bench_write("spawn", "matugen_reloaded");
  }
  if (matugen_pending_ && bootstrap_done_) {
    matugen_pending_ = false;
    eh::config::shell_config_trigger_async_matugen();
  }
  {
    eh::app::wl_loop_diag::StallScope _stall("unified", "weather_drive_curl_multi");
    eh::widgets::control_center_weather_drive_curl_multi();
  }
}

int UnifiedShellSession::run(ShellRunMode /*mode*/) {
   
  MANGOWM_INFO("=== UnifiedShellSession::run START ===");
  {
    const auto crash_handler = [](int sig, siginfo_t* info, void*) {
      const char* name = (sig == SIGSEGV) ? "SIGSEGV" : (sig == SIGABRT) ? "SIGABRT" : "SIG??";
      std::cerr << "\n*** CRASH: " << name << "(" << sig << ")";
      if (info) std::cerr << " si_addr=" << info->si_addr;
      if (info && (sig == SIGSEGV || sig == SIGABRT))
        std::cerr << " si_code=" << info->si_code;
      std::cerr << " ***\n" << std::flush;
      // Print a backtrace (debug builds have symbols; release builds give addresses)
      void* bt[64];
      const int n = backtrace(bt, 64);
      std::cerr << "*** backtrace:\n";
      backtrace_symbols_fd(bt, n, STDERR_FILENO);
      std::cerr << "\n" << std::flush;
      // Re-raise with the default handler so core dumps still work.
      struct sigaction sa{};
      sa.sa_handler = SIG_DFL;
      sigemptyset(&sa.sa_mask);
      sigaction(sig, &sa, nullptr);
      raise(sig);
    };
    struct sigaction sa{};
    sa.sa_sigaction = crash_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
  }

  // Route SIGCHLD through a signalfd so the event loop can reap the split-out
  // children (wallpaper / notifications / desktop) and respawn the desktop
  // child if it ever dies. Blocking SIGCHLD here, before any spawn, guarantees
  // no exit is missed.
  {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    sigchld_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  }

  // Silence std::cerr unless EH_DEBUG is active.
  struct NullBuf : std::streambuf {};
  NullBuf nullCerrBuf;
  if (!eh::debug_profile::eh_debug_enabled()) {
    std::cerr.rdbuf(&nullCerrBuf);
  }

  MANGOWM_INFO("startup: connect to wayland display");
  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: wl_display_connect");
  const auto t_boot0 = std::chrono::steady_clock::now();
  auto boot_mark = [t_boot0](const char* label) {
    const auto now = std::chrono::steady_clock::now();
    debug_log("boot", "%s: t=%lldms", label,
              (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - t_boot0).count());
  };
  boot_mark("run::enter (post-sigchld-sigfd)");
  display_ = wl_display_connect(nullptr);
  if (!display_) {
    MANGOWM_CRITICAL("WAYLAND_DISPLAY not set or compositor unavailable");
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  MANGOWM_INFO("display connected ptr=%p", (void*)display_);
  eh::bench::bench_write("spawn", "display_connected");
  if (eh::bench::enabled() || eh::debug_profile::env_bool("EH_DOCK_BENCH")) {
    bench_t0_ = std::chrono::steady_clock::now();
  }
  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: display ok ptr=" << static_cast<void*>(display_));
  boot_mark("display_connected");

  MANGOWM_INFO("startup: load config");
  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: shell_config_snapshot → settings");
  sc0_ = eh::config::shell_config_snapshot_skip_matugen();
  eh::bench::bench_write("spawn", "config_loaded");
  boot_mark("config_loaded");

  // Own settings watcher: the dock child used to own it but now opens its own
  // inotify fd. This process reloads keyboard/nightlight/taskbar from raw file
  // edits without disturbing the dock child's independent reload.
  settings_inotify_fd_ = eh::shell::shared::open_state_inotify();

  // The dock bar runs as the split-out `horizon-dock` child, spawned once the
  // IPC service is up below. This display only carries a toplevel tracker whose
  // containers the taskbar borrows (its own Wayland connection can't reliably
  // track foreign toplevels) and that the ToplevelBridge publishes onto the
  // IPC bus.
  MANGOWM_INFO("startup: init toplevel tracker");
  if (!eh::shell::shared::toplevel_tracker_init(toplevel_tracker_, display_, nullptr)) {
    MANGOWM_CRITICAL("toplevel tracker init failed");
    wl_display_disconnect(display_);
    display_ = nullptr;
    return 1;
  }
  boot_mark("toplevel_tracker_init_done");
  eh::bench::bench_write("spawn", "dock_init_done");
  MANGOWM_INFO("startup: init wallpaper");
  // Wallpaper runs as the split-out `horizon-wallpaper` child: it
  // owns its own WaylandConnection + WallpaperRenderer and reads the config
  // itself, re-applying on the `config.applied` broadcast. The supervisor only
  // spawns it (after the IPC service is up, below) and kills it at shutdown.
  MANGOWM_INFO("startup: wallpaper/desktop/dock/taskbar run as split-out children");
  // The desktop icon layer + widgets, the dock bar, and the taskbar bar all run
  // as split-out children (horizon-desktop / horizon-dock / horizon-taskbar),
  // spawned once the IPC service is up below. This process keeps the toplevel
  // tracker (broadcast by ToplevelBridge) plus gamma/lockscreen/idle/polkit on
  // their own connections.
  eh::shell::keyboard::apply_settings(sc0_.keyboard);

#if defined(__GLIBC__)
  malloc_trim(0);
#endif
  bench_log("after_dock_init_and_install_fds");
  boot_mark("keyboard_apply+malloc_trim_done");

  drag_preview_user_ = std::make_unique<DragPreviewUser>();
  eh::config::shell_config_set_applied_hook(unified_shell_config_applied_hook, drag_preview_user_.get());

  eh::settings::set_settings_launcher([this]() {
    if (eh::settings::settings_singleton_running()) {
      if (ipc_service_) ipc_service_->publish("command.request", "settings.toggle");
      return;
    }
    (void)eh::settings::spawn_settings();
  });
  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: settings now launched as external horizon-settings child");

  // IPC service: create + listen EARLY. Time-to-socket is what clients and
  // launch harnesses measure as startup, and the gamma/idle/lockscreen
  // connections initialized below each make several blocking wl_display
  // round-trips, so the listening socket must exist BEFORE them. Handlers are
  // still registered at the original site below.
  MANGOWM_INFO("startup: init IPC service (early listen)");
  ipc_service_ = std::make_unique<eh::ipc::IpcService>();
  ipc_service_->start(eh::ipc::default_socket_path());
  if (drag_preview_user_) drag_preview_user_->ipc = ipc_service_.get();
  boot_mark("ipc_service_started");

  MANGOWM_INFO("startup: init gamma control");
  // Nightlight gamma lives on its own Wayland connection so a protocol error
  // there can never take down the main shell connection.
  gamma_conn_ = std::make_unique<eh::wayland::WaylandConnection>();
  if (gamma_conn_->connect(false) && gamma_conn_->gamma_control_manager()) {
    gamma_service_ = std::make_unique<eh::wayland::GammaService>(gamma_conn_->gamma_control_manager());
    for (wl_output* out : gamma_conn_->outputs()) {
      if (out) gamma_service_->add_output(out);
    }
    eh::settings::embed_set_gamma_service(gamma_service_.get());
    apply_nightlight_config(gamma_service_.get(), sc0_.nightLight);
    if (drag_preview_user_) drag_preview_user_->gamma = gamma_service_.get();
    EH_ST_TRACE(std::cerr << "gamma_service: initialized on own connection with "
                          << gamma_conn_->outputs().size() << " outputs\n");
  } else {
    std::cerr << "[gamma] own Wayland connection unavailable — nightlight disabled\n";
  }
  boot_mark("gamma_connect_done");

  MANGOWM_INFO("startup: init idle service");
  idle_conn_ = std::make_unique<eh::wayland::WaylandConnection>();
  if (idle_conn_->connect(false) && idle_conn_->idle_notifier() && idle_conn_->seat()) {
    idle_service_ = std::make_unique<eh::wayland::IdleService>(idle_conn_->idle_notifier(), idle_conn_->seat());
    const auto& idleCfg = sc0_.idle;
    std::vector<eh::wayland::IdleBehavior> behaviors;
    behaviors.reserve(idleCfg.behaviors.size());
    for (const auto& ib : idleCfg.behaviors) {
      eh::wayland::IdleBehavior b;
      b.name = ib.name;
      b.timeout_ms = static_cast<uint32_t>(ib.timeout_sec) * 1000u;
      b.enabled = ib.enabled;
      if (!ib.command.empty()) {
        b.on_idle = [cmd = ib.command]() { ThreadPool::instance().enqueue([cmd]() { (void)system(cmd.c_str()); }); };
      }
      if (!ib.resume_command.empty()) {
        b.on_resumed = [cmd = ib.resume_command]() { ThreadPool::instance().enqueue([cmd]() { (void)system(cmd.c_str()); }); };
      }
      behaviors.push_back(std::move(b));
    }
    idle_service_->reload(std::move(behaviors));
    EH_ST_TRACE(std::cerr << "idle_service: initialized with " << behaviors.size() << " behaviors on own connection\n");
  } else {
    std::cerr << "[idle] own Wayland connection unavailable — idle service disabled\n";
  }
  boot_mark("idle_connect_done");

  MANGOWM_INFO("startup: init lockscreen");
  lock_conn_ = std::make_unique<eh::wayland::WaylandConnection>();
  if (lock_conn_->connect(false) && lock_conn_->has_core_shell_globals() &&
      lock_conn_->session_lock_manager() && lock_conn_->seat()) {
    lock_screen_ = std::make_unique<eh::shell::lockscreen::LockScreen>(
        lock_conn_->display(), lock_conn_->compositor(), lock_conn_->shm(),
        lock_conn_->session_lock_manager(), lock_conn_->seat(), nullptr,
        lock_conn_->pointer_constraints());
    for (wl_output* out : lock_conn_->outputs()) {
      if (out) lock_screen_->add_output(out);
    }
    EH_ST_TRACE(std::cerr << "lock_screen: initialized on own connection\n");
  } else {
    std::cerr << "[lockscreen] own Wayland connection unavailable — locking disabled\n";
  }

  boot_mark("lockscreen_connect_done");

  sync_widget_registry();
  boot_mark("sync_widget_registry_done");

  MANGOWM_INFO("startup: init global keyboard handler");
  // The Super key is the start-menu toggle and is owned by the split-out
  // horizon-dock child, which fires its own evdev menu handler. This process
  // only keeps Super+S (settings), so only the settings fn lands here;
  // pressing Super alone is handled entirely by the dock child.
  global_keyboard_ = std::make_unique<eh::service::GlobalKeyboardHandler>();
  global_keyboard_->init(nullptr, [this]() {
    if (eh::settings::settings_singleton_running()) {
      if (ipc_service_) ipc_service_->publish("command.request", "settings.toggle");
      return;
    }
    (void)eh::settings::spawn_settings();
  });
  EH_ST_TRACE(std::cerr << "run_event_horizon_shell: Super handled by dock child; Super+S launches settings");

  MANGOWM_INFO("startup: init IPC service");
  // IPC service (Unix socket command server).
  {
    // The socket was already created and is listening earlier in startup
    // (see "create + listen EARLY") so clients/harnesses see it before the
    // auxiliary Wayland connections. Only handler registration happens here.
    if (!ipc_service_) {
      ipc_service_ = std::make_unique<eh::ipc::IpcService>();
      ipc_service_->start(eh::ipc::default_socket_path());
      if (drag_preview_user_) drag_preview_user_->ipc = ipc_service_.get();
    }

    ipc_service_->register_handler("lock", [this](const auto&) -> std::string {
      if (lock_screen_ && lock_screen_->lock()) return "ok";
      return "error lock screen not available or lock request failed";
    });

    ipc_service_->register_handler("unlock", [this](const auto&) -> std::string {
      if (lock_screen_) { lock_screen_->unlock(); return "ok"; }
      return "error lock screen not available";
    });

    ipc_service_->register_handler("test-lock", [this](const auto&) -> std::string {
      if (lock_screen_ && lock_screen_->test_lock(15000)) { // 15s safe auto-unlock test
        return "ok (TEST MODE: will auto-unlock in ~15s; Esc aborts)";
      }
      return "error lock screen not available or lock request failed";
    });

    ipc_service_->register_handler("logout", [this](const auto&) -> std::string {
      running_ = false;
      if (toplevel_tracker_.compositorKind == CompositorKind::Mango) {
        ThreadPool::instance().enqueue([]() {
          int ret = ::system("mmsg dispatch quit >/dev/null 2>&1");
          if (ret != 0) std::cerr << "[session] mmsg quit failed: " << ret << '\n';
        });
      }
      return "ok";
    });

    ipc_service_->register_handler("shutdown", [](const auto&) -> std::string {
      ThreadPool::instance().enqueue([]() {
        int ret = ::system("loginctl poweroff");
        if (ret != 0) std::cerr << "[session] loginctl poweroff failed: " << ret << '\n';
      });
      return "ok";
    });

    ipc_service_->register_handler("reboot", [](const auto&) -> std::string {
      ThreadPool::instance().enqueue([]() {
        int ret = ::system("loginctl reboot");
        if (ret != 0) std::cerr << "[session] loginctl reboot failed: " << ret << '\n';
      });
      return "ok";
    });

    ipc_service_->register_handler("overview-toggle", [this](const auto&) -> std::string {
      // Overview surfaces now live in the split-out horizon-dock child; forward the
      // toggle onto the IPC bus so it can own them directly.
      if (ipc_service_) ipc_service_->publish("command.request", "overview.toggle");
      return "ok";
    });

    ipc_service_->register_handler("overview-open", [this](const auto&) -> std::string {
      if (ipc_service_) ipc_service_->publish("command.request", "overview.open");
      return "ok";
    });

    ipc_service_->register_handler("overview-close", [this](const auto&) -> std::string {
      if (ipc_service_) ipc_service_->publish("command.request", "overview.close");
      return "ok";
    });

    ipc_service_->register_handler("notify", [this](const std::vector<std::string>& args) -> std::string {
      if (args.size() < 2) return "error usage: notify <title> <body>";
      eh::notify::push_internal("IPC Command", args[0], args[1], 1);
      return "ok";
    });

    ipc_service_->register_handler("launchpad", [this](const auto&) -> std::string {
      // Launchpad surfaces live in the split-out horizon-dock child; forward the
      // toggle onto the IPC bus (the child owns them).
      if (ipc_service_) ipc_service_->publish("command.request", "launchpad.toggle");
      return "ok";
    });

    ipc_service_->register_handler("menu-toggle", [this](const auto&) -> std::string {
      // The taskbar's start-menu/app-drawer toggle moved to the split-out
      // horizon-taskbar child with the taskbar; forward it as the taskbar-scoped
      // command so one press can't toggle both bars (the dock child keeps its
      // own `menu.toggle`).
      if (ipc_service_) ipc_service_->publish("command.request", "taskbar.menu.toggle");
      return "ok";
    });

    ipc_service_->register_handler("list-commands", [this](const auto&) -> std::string {
      std::string result;
      for (const auto& name : ipc_service_->registered_commands()) {
        if (!result.empty()) result += " ";
        result += name;
      }
      return result;
    });

    ipc_service_->register_handler("widget-activate", [this](const std::vector<std::string>& args) -> std::string {
      if (args.empty()) return "error usage: widget-activate <role>";
      if (widget_registry_.activate(args[0])) return "ok";
      return "error no widget with role '" + args[0] + "' is configured";
    });

    ipc_service_->register_handler("widget-list", [this](const auto&) -> std::string {
      auto entries = widget_registry_.list();
      if (entries.empty()) return "";
      std::string result;
      for (const auto& e : entries) {
        if (!result.empty()) result += "\n";
        result += e.role + " " + e.component;
      }
      return result;
    });

    ipc_service_->register_handler("screenshot", [](const std::vector<std::string>& args) -> std::string {
      ThreadPool::instance().enqueue([args]() {
        std::string cmd = "/usr/bin/EventHorizon --eh-screenshot";
        for (const auto& a : args) {
          cmd += " " + a;
        }
        cmd += " 2>/dev/null";
        int ret = ::system(cmd.c_str());
        if (ret != 0) std::cerr << "[session] screenshot failed: " << ret << '\n';
      });
      return "ok";
    });

    EH_ST_TRACE(std::cerr << "ipc_service: initialized\n");
  }

  // Self-client on the bus: the server itself doesn't receive its own
  // publishes, so the supervisor dials into its own socket to catch child ->
  // parent requests. The taskbar child (which owns no gamma) publishes
  // `nightlight.toggle` here; the supervisor owns the single gamma client and
  // flips it. A sub-second connect with retries covers the just-started socket.
  ipc_self_client_ = std::make_unique<eh::ipc::IpcClient>();
  if (ipc_self_client_->connect(eh::ipc::default_socket_path(), 3) < 0 ||
      !ipc_self_client_->subscribe_async("command.request")) {
    std::cerr << "[session] ipc self-client unavailable — child → supervisor commands disabled\n";
    ipc_self_client_.reset();
  } else {
    ipc_self_client_->set_event_handler(
        [this](std::string topic, std::string payload, std::vector<int> /*fds*/) {
          if (topic != "command.request") return;
          if (payload == "nightlight.toggle") {
            if (!gamma_service_) return;
            // Same behavior as the taskbar's old in-process toggle: a fixed
            // night temperature when it comes on.
            const bool next = !gamma_service_->enabled();
            gamma_service_->set_enabled(next);
            if (next) gamma_service_->set_temperature(4000);
          }
        });
  }

  // Toplevel/window event service: put the supervisor's toplevel tracker (main
  // connection) onto the IPC bus so split components can observe windows
  // without running a tracker of their own.
  if (ipc_service_ && ipc_service_->running()) {
    toplevel_bridge_ =
        std::make_unique<eh::windows::ToplevelBridge>(toplevel_tracker_.toplevels, *ipc_service_);
    toplevel_bridge_->start();
  }

  // VRAM boost (dmem cgroups): shield the focused fullscreen app's VRAM from
  // eviction by raising its cgroup's dmem.min/dmem.low. No-op when the kernel
  // lacks the dmem controller or no GPU memory region is registered.
  vram_boost_ = std::make_unique<eh::service::VramBoostManager>(toplevel_tracker_.toplevels);
  vram_boost_->start();

  // Spawn the split-out `horizon-desktop` child: the desktop icon layer +
  // widgets + menus live on its own WaylandConnection, timer fds, and MPRIS
  // listener, and it subscribes to `config.applied` for output / theme /
  // widget-config changes. The SIGCHLD handler respawns it on crash.
  if (ipc_service_ && ipc_service_->running()) {
    const int dPid = eh::shell::desktop::desktop_spawn_native_child();
    if (dPid > 1) desktop_child_pid_ = dPid;
  }
  boot_mark("desktop_child_spawned");

  // Spawn the split-out `horizon-wallpaper` child now the IPC bus is up (it
  // subscribes to `config.applied`). Wallpaper rendering is fully isolated
  // from the shell; a crash in the child can't take the session down.
  if (ipc_service_ && ipc_service_->running()) {
    const int wpPid = eh::wallpaper::wallpaper_spawn_native_child();
    if (wpPid > 1) wallpaper_child_pid_ = wpPid;
  }
  boot_mark("wallpaper_child_spawned");

  // Spawn the split-out `horizon-dock` child: the dock bar (surfaces, launchpad,
  // overview, start menu, tray, settings watcher, MPRIS, and the Super-key menu
  // toggle) now runs on its own WaylandConnection + loop fds and subscribes to
  // `config.applied`. The SIGCHLD handler respawns it on crash; the taskbar +
  // gamma + Super+S settings were the last supervisor bits removed by this
  // split.
  if (ipc_service_ && ipc_service_->running()) {
    const int dPid = eh::shell::dock::dock_spawn_native_child();
    if (dPid > 1) dock_child_pid_ = dPid;
  }
  boot_mark("dock_child_spawned");

  // Spawn the split-out `horizon-taskbar` child: the taskbar bar + app drawer +
  // popups get their own WaylandConnection with toplevel tracking of their own
  // (mirroring horizon-dock), plus a timer fd, settings inotify, and MPRIS
  // listener, and subscribe to `config.applied` + `command.request`. The
  // SIGCHLD handler respawns it on crash.
  if (ipc_service_ && ipc_service_->running()) {
    const int tPid = eh::shell::taskbar::taskbar_spawn_native_child();
    if (tPid > 1) taskbar_child_pid_ = tPid;
  }
  boot_mark("taskbar_child_spawned");

  MANGOWM_INFO("startup: start network service");
  // Pre-start the network service so cached state is ready when the UI first queries it.
  eh::net::NetworkManagerService::instance().start();
  boot_mark("network_service_started");

  const int display_fd = wl_display_get_fd(display_);
  EH_ST_TRACE(std::cerr << "unified: entering run_poll_mux displayFd=" << display_fd);

  MANGOWM_INFO("startup: start notification services");
  thumb_poll_fd_ = eh::settings::thumbnail_wake_fd();
  eh::widgets::control_center_weather_async_init();
  weather_wake_fd_ = eh::widgets::control_center_weather_async_wake_fd();

  start_notifications_services();

  boot_mark("notifications_started");

  MANGOWM_INFO("all services started, entering event loop");
  eh::bench::bench_write("spawn", "services_started");
  bench_log("before_main_loop_poll_mux (embed deferred to first idle)");

#if defined(EH_HAVE_POLKIT_AGENT)
  {
    auto& polkit = eh::polkit::PolkitAuthService::instance();
    polkit.start();
    polkit_conn_ = std::make_unique<eh::wayland::WaylandConnection>();
    if (polkit_conn_->connect(false)) {
      polkit_dialog_ = std::make_unique<eh::polkit::PolkitAuthDialog>(
          polkit_conn_->display(), polkit_conn_->compositor(), polkit_conn_->shm(),
          polkit_conn_->layer_shell(), polkit_conn_->viewporter(),
          polkit_conn_->fractional_scale_manager());
      // The dialog surface lives on its own connection, so seat input has to be
      // routed through that connection's seat (keys only arrive here while the
      // EXCLUSIVE layer surface holds keyboard focus).
      polkit_seat_.bind(polkit_conn_->seat());
      polkit_seat_.set_pointer_motion_cb([this](wl_surface* surface, double x, double y) {
        if (polkit_dialog_ && surface == polkit_dialog_->surface())
          polkit_dialog_->pointer_motion(x, y);
      });
      polkit_seat_.set_pointer_button_cb([this](uint32_t button, uint32_t state) {
        if (polkit_dialog_ && polkit_seat_.pointer_focus_surface() == polkit_dialog_->surface())
          polkit_dialog_->pointer_button(button, state);
      });
      polkit_seat_.set_keyboard_key_cb([this](const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
        if (polkit_dialog_)
          polkit_dialog_->consume_keyboard_key(ev.state, ev.sym, ev.utf8.data(), ev.utf8_len);
      });
      EH_ST_TRACE(std::cerr << "polkit dialog: initialized on own connection\n");
    } else {
      std::cerr << "[polkit] own Wayland connection unavailable — auth dialog disabled\n";
    }
    polkit.set_change_callback([this]() {
      auto snap = eh::polkit::PolkitAuthService::instance().snapshot();
      if (snap.has_request && snap.state != eh::polkit::AuthState::Idle) {
        if (!polkit_dialog_) return;
        wl_output* out = nullptr;
        if (polkit_conn_ && !polkit_conn_->outputs().empty()) out = polkit_conn_->outputs().front();
        polkit_dialog_->show(out);
      } else {
        if (polkit_dialog_) polkit_dialog_->hide();
      }
    });
  }
#endif

  eh::bench::bench_write("spawn", "main_loop_start");
  EH_ST_TRACE(std::cerr << "unified: run_poll_mux (blocking until exit)");
  PollMuxLoop mux{};
  mux.wayland_display = display_;
  mux.display_fd = display_fd;
  mux.on_display = [this]() { return on_after_wayland_dispatch(); };
  mux.on_idle_flush = [this](bool did) { on_idle_flush(did); };
  mux.get_handlers = [this]() { return build_handlers(); };

  // Poll the gamma/idle/lockscreen/polkit connections in isolation. A protocol
  // error on any of them disables just that connection instead of killing the
  // main event loop.
  {
    auto registerOwnDisplay = [&mux](wl_display* dpy, const char* tag, std::function<bool()> onDispatch) {
      if (!dpy) return;
      PollMuxDisplay dm{};
      dm.display = dpy;
      dm.fd = wl_display_get_fd(dpy);
      dm.on_dispatch = std::move(onDispatch);
      dm.on_error = [dpy, tag, &mux]() {
        std::cerr << "[" << tag << "] Wayland protocol error on its own display\n";
        for (auto& ed : mux.extra_displays) {
          if (ed.display == dpy) {
            ed.fd = -1;
            ed.display = nullptr;
            break;
          }
        }
      };
      mux.extra_displays.push_back(std::move(dm));
    };
    registerOwnDisplay(gamma_conn_ ? gamma_conn_->display() : nullptr, "gamma", []() { return true; });
    registerOwnDisplay(idle_conn_ ? idle_conn_->display() : nullptr, "idle", []() { return true; });
    registerOwnDisplay(lock_conn_ ? lock_conn_->display() : nullptr, "lockscreen", [this]() {
      if (lock_screen_ && lock_conn_) lock_screen_->sync_outputs(lock_conn_->outputs());
      return true;
    });
#if defined(EH_HAVE_POLKIT_AGENT)
    registerOwnDisplay(polkit_conn_ ? polkit_conn_->display() : nullptr, "polkit", []() { return true; });
#endif
  }
#if defined(EH_HAVE_POLKIT_AGENT)
  mux.on_gather_fds = [](std::vector<pollfd>& fds) {
    eh::polkit::PolkitAuthService::instance().gather_fds(fds);
  };
  mux.on_poll_timeout = []() -> int {
    return eh::polkit::PolkitAuthService::instance().poll_timeout();
  };
  mux.on_post_poll = [](const pollfd* fds, int nfds) {
    eh::polkit::PolkitAuthService::instance().dispatch_glib(fds, nfds);
  };
#endif
  MANGOWM_INFO("entering main event loop (PollMuxLoop)");
  (void)mux.run(&running_);
  MANGOWM_INFO("main event loop exited");
  EH_ST_TRACE(std::cerr << "unified: run_poll_mux returned");

  eh::config::shell_config_clear_drag_preview_paint_tick();
  drag_preview_user_.reset();
  eh::config::shell_config_clear_settings_drag_preview();

#if defined(EH_HAVE_POLKIT_AGENT)
  polkit_dialog_.reset();
  polkit_seat_.unbind();
  polkit_conn_.reset();
  eh::polkit::PolkitAuthService::instance().stop();
#endif

  eh::net::NetworkManagerService::instance().stop();

  if (ipc_service_) ipc_service_->stop();
  ipc_self_client_.reset();

  // Stop the toplevel bridge first — before the dock tracker (dock_cleanup)
  // and the IPC bus go down — so it stops observing/publishing cleanly.
  if (toplevel_bridge_) toplevel_bridge_.reset();
  // Stop the VRAM boost before the toplevel tracker/connections are torn down,
  // so any applied dmem limits are restored.
  if (vram_boost_) vram_boost_.reset();

  stop_notifications_services();

  eh::settings::embed_shutdown();
  if (sigchld_fd_ >= 0) {
    close(sigchld_fd_);
    sigchld_fd_ = -1;
  }

  if (wallpaper_child_pid_ > 1) {
    eh::wallpaper::wallpaper_kill_native_child(wallpaper_child_pid_);
    wallpaper_child_pid_ = -1;
  }
  if (desktop_child_pid_ > 1) {
    eh::shell::desktop::desktop_kill_native_child(desktop_child_pid_);
    desktop_child_pid_ = -1;
  }
  if (dock_child_pid_ > 1) {
    eh::shell::dock::dock_kill_native_child(dock_child_pid_);
    dock_child_pid_ = -1;
  }
  if (taskbar_child_pid_ > 1) {
    eh::shell::taskbar::taskbar_kill_native_child(taskbar_child_pid_);
    taskbar_child_pid_ = -1;
  }
  if (settings_inotify_fd_ >= 0) {
    close(settings_inotify_fd_);
    settings_inotify_fd_ = -1;
  }
  eh::shell::shared::toplevel_tracker_cleanup(toplevel_tracker_);
  // Tear each service down before its own connection so the proxies release
  // while the connection display is still alive.
  gamma_service_.reset();
  idle_service_.reset();
  lock_screen_.reset();
  gamma_conn_.reset();
  idle_conn_.reset();
  lock_conn_.reset();
  MANGOWM_INFO("=== UnifiedShellSession::run END (clean shutdown) ===");
  wl_display_disconnect(display_);
  display_ = nullptr;
  return 0;
}

void UnifiedShellSession::sync_widget_registry() {
   
  // Rebuild the registry from the current taskbar widget config snapshot (the
  // dock bar's widgets live in the split-out horizon-dock child, which keeps
  // its own registry — this one covers taskbar-launchable roles only; and the
  // taskbar itself now runs in the horizon-taskbar child, so config is read
  // from the snapshot rather than an in-process app).
  // Clear any existing entries first.
  widget_registry_.remove_component("taskbar");

  // Walk the widget lists and register activatable types.
  auto register_widgets = [this](const std::vector<std::string>& list,
                                  const std::string& component) {
    for (const auto& token : list) {
      const std::string type = eh::config::widget_implementation_type(token);

      // Activatable widget types map to actions forwarded to the dock child,
      // which owns the launchpad + start-menu surfaces.
      if (type == "launchpad" || type == "app_drawer" || type == "smenu") {
        widget_registry_.add(type,
            [this]() {
              if (ipc_service_) ipc_service_->publish("command.request", "launchpad.toggle");
            },
            component);
      }
    }
  };

  const auto& t = eh::config::shell_config_snapshot().taskbar;
  register_widgets(t.leftWidgets, "taskbar");
  register_widgets(t.centerWidgets, "taskbar");
  register_widgets(t.rightWidgets, "taskbar");
}

}
}
