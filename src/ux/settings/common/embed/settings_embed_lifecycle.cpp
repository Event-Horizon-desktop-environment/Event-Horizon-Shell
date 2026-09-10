#define _GNU_SOURCE 1

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"
#include "services/process/parent_death_guard.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/common/logo/settings_logo.hpp"
#include "ux/settings/utils/gpu/settings_gpu.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/widget_picker.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "ux/settings/settings_tab_default_apps/settings_tab_default_apps.hpp"
#include "ux/settings/settings_tab_wallpaper/settings_tab_wallpaper.hpp"
#include "wallpaper/wallpaper_log.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "services/bing/bing_wallpaper.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"
#include "configuration/shell_config.hpp"
#include "bootstrap/thread/thread_dispatch.hpp"
#include "services/network/core/network_manager_service.hpp"

#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"
#include "services/ipc/client.hpp"
#include "services/ipc/ipc_server.hpp"

extern void draw(App& app);

extern bool eh_settings_bench();
extern bool eh_wallpaper_thumb_debug();
extern bool eh_settings_embed_post_open_roundtrip();

using SettingsBenchClock = std::chrono::steady_clock;

static inline int64_t settings_bench_us(SettingsBenchClock::time_point a, SettingsBenchClock::time_point b) {
   
  return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

extern unsigned s_settings_bench_session;
extern unsigned s_settings_bench_draw_n;
extern SettingsBenchClock::time_point s_settings_bench_t0;

extern const float kEmbedOpenCloseMs = 220.f;
extern const double kEmbedSlidePx = 18.0;

static void on_buf_release(void* user) {
   
  auto& app = *static_cast<App*>(user);
  if (!app.surface) return;

  app.pendingRedraw = false;
  draw(app);
}

static void xdg_wm_base_ping(void* data, xdg_wm_base* wm, uint32_t serial) {
   
  (void)data;
  xdg_wm_base_pong(wm, serial);
}
static const xdg_wm_base_listener g_wm_base_listener = {.ping = xdg_wm_base_ping};

static void xdg_surface_configure(void* data, xdg_surface* surface, uint32_t serial) {
   
  auto& app = *static_cast<App*>(data);
  debug_log("settings", "xdg_surface_configure enter: serial=%u w=%d h=%d embed=%d last_w=%d last_h=%d",
            serial, app.width, app.height, (int)app.embedded,
            app.last_committed_draw_w, app.last_committed_draw_h);
  const SettingsBenchClock::time_point t0 = SettingsBenchClock::now();
  xdg_surface_ack_configure(surface, serial);

  if (app.embedded && app.last_committed_draw_w == app.width && app.last_committed_draw_h == app.height &&
      app.last_committed_draw_w > 0) {
     
    if (eh_settings_bench()) {
      std::cerr << "[settings-bench] xdg_surface_configure serial=" << serial << " skip_redundant_draw " << app.width << "x"
                << app.height << " ack=" << settings_bench_us(t0, SettingsBenchClock::now()) << "us\n";
    }
    debug_log("settings", "xdg_surface_configure: skip_redundant_draw");
    return;
  }
  if (eh_settings_bench()) {
    std::cerr << "[settings-bench] xdg_surface_configure serial=" << serial << " embedded=" << (app.embedded ? 1 : 0)
              << " ack=" << settings_bench_us(t0, SettingsBenchClock::now()) << "us \xe2\x86\x92 draw\n";
  }
  debug_log("settings", "xdg_surface_configure: calling draw()");
  draw(app);
  debug_log("settings", "xdg_surface_configure: draw() returned");
  if (eh_settings_bench()) {
    std::cerr << "[settings-bench] xdg_surface_configure serial=" << serial << " draw_returned total="
              << settings_bench_us(t0, SettingsBenchClock::now()) << "us\n";
  }
}
static const xdg_surface_listener g_xdg_surface_listener = {.configure = xdg_surface_configure};

static void toplevel_configure(void* data, xdg_toplevel*  , int32_t w, int32_t h, wl_array*  ) {
   
  auto& app = *static_cast<App*>(data);
  if (w > 0) app.width = w;
  if (h > 0) app.height = h;
  settings_clamp_sidebar_scroll_px(app);
}

static void destroy_settings_surfaces(App& app) {
    
  debug_log("settings", "destroy_settings_surfaces: surface=%p embedded=%d wl_display=%p",
            (void*)app.surface, (int)app.embedded, (void*)app.wl.display());
  const SettingsBenchClock::time_point t_destroy0 = SettingsBenchClock::now();
  if (eh_settings_bench()) {
    if (app.embedded) {
      ++s_settings_bench_session;
      s_settings_bench_t0 = t_destroy0;
      s_settings_bench_draw_n = 0;
    }
    std::cerr << "[settings-bench] \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 destroy session=" << s_settings_bench_session << " embedded=" << (app.embedded ? 1 : 0)
              << " \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n";
  }
  app.embedAnim.cancel_all();
  app.embedPresentT = 1.f;
  app.pendingRedraw = false;
  app.settingsDeferRedraw = false;
  app.settings_paint_shell_snapshot_valid = false;
  app.settingsWidgetDragRepaintQueued = false;
  app.soundPaintSnapPending.reset();
  settings_clear_all_gpu_surfaces(app);
  app.last_committed_draw_w = -1;
  app.last_committed_draw_h = -1;
  app.buf[0].set_release_hook(nullptr, nullptr);
  app.buf[1].set_release_hook(nullptr, nullptr);
  eh::settings::widget_picker::teardown_layer(app);
  default_app_picker_teardown_layer(app);
  if (app.surfaceFrameCb) {
    wl_callback_destroy(app.surfaceFrameCb);
    app.surfaceFrameCb = nullptr;
  }
  const SettingsBenchClock::time_point t_after_hooks = SettingsBenchClock::now();
  eh::wallpaper::WallpaperThumbnailService::instance().release_all();
  wallpaper_destroy_thumbs(app);

  if (app.logo && !app.embedded) {
    cairo_surface_destroy(app.logo);
    app.logo = nullptr;
  }
  if (app.wallpaperHeroSurf) {
    cairo_surface_destroy(app.wallpaperHeroSurf);
    app.wallpaperHeroSurf = nullptr;
  }
  app.wallpaperHeroPath.clear();
  const SettingsBenchClock::time_point t_after_thumbs = SettingsBenchClock::now();
  if (app.surface && app.wl.display()) {
    wl_surface_attach(app.surface, nullptr, 0, 0);
    wl_surface_commit(app.surface);
    (void)wl_display_flush(app.wl.display());
  }
  const SettingsBenchClock::time_point t_after_detach = SettingsBenchClock::now();
  app.buf[0].destroy();
  app.buf[1].destroy();
  const SettingsBenchClock::time_point t_after_buf = SettingsBenchClock::now();
  if (app.toplevel) {
    xdg_toplevel_destroy(app.toplevel);
    app.toplevel = nullptr;
  }
  if (app.xdgSurface) {
    xdg_surface_destroy(app.xdgSurface);
    app.xdgSurface = nullptr;
  }
  app.surfaceExt.destroy();
  const SettingsBenchClock::time_point t_after_roles = SettingsBenchClock::now();
  if (app.surface) {
    wl_surface_destroy(app.surface);
    app.surface = nullptr;
  }
  const SettingsBenchClock::time_point t_end = SettingsBenchClock::now();
  if (eh_settings_bench()) {
    std::cerr << "[settings-bench] destroy embedded=" << (app.embedded ? 1 : 0)
              << " cancel_frame_cb=" << settings_bench_us(t_destroy0, t_after_hooks) << "us thumbs_logo="
              << settings_bench_us(t_after_hooks, t_after_thumbs) << "us detach_flush="
              << settings_bench_us(t_after_thumbs, t_after_detach) << "us buf_destroy=" << settings_bench_us(t_after_detach, t_after_buf)
              << "us xdg_ext_destroy=" << settings_bench_us(t_after_buf, t_after_roles) << "us wl_surface_destroy="
              << settings_bench_us(t_after_roles, t_end) << "us TOTAL=" << settings_bench_us(t_destroy0, t_end) << "us\n";
  }
}

static void toplevel_close(void* data, xdg_toplevel*  ) {
    
  auto& app = *static_cast<App*>(data);
  const auto age = SettingsBenchClock::now() - app.embedOpenTime;
  debug_log("settings", "toplevel_close: embedded=%d age=%lldms", (int)app.embedded,
            (long long)std::chrono::duration_cast<std::chrono::milliseconds>(age).count());
  if (app.embedded) {
    // The compositor sends a spurious close ~1s after every xdg_toplevel creation.
    // Ignore close events within 3 seconds of opening to avoid the
    // open→auto-close→reopen cycle.  Genuine user close (Win+Q, window
    // decoration X button) arrives well after 3s and is honoured.
    if (age < std::chrono::seconds(3)) {
      debug_log("settings", "toplevel_close: ignored (age < 3s, likely Hyprland spurious)");
      return;
    }
    debug_log("settings", "toplevel_close: closing embedded window");
    destroy_settings_surfaces(app);
    return;
  }
  debug_log("settings", "toplevel_close: standalone mode, setting running=false");
  app.running = false;
}
static void toplevel_configure_bounds(void*  , xdg_toplevel*  , int32_t  , int32_t  ) {
   
}
static void toplevel_wm_capabilities(void*  , xdg_toplevel*  , wl_array*  ) {
   
}
static const xdg_toplevel_listener g_toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities = toplevel_wm_capabilities,
};

static bool create_settings_window(App& app) {
   
  debug_log("settings", "create_settings_window enter: compositor=%p shm=%p xdg=%p surface=%p",
            (void*)app.wl.compositor(), (void*)app.wl.shm(), (void*)app.wl.xdg_base(),
            (void*)app.surface);
  if (!app.wl.compositor() || !app.wl.shm() || !app.wl.xdg_base()) {
    debug_log("settings", "create_settings_window FAIL: missing globals");
    return false;
  }
  if (app.surface) {
    debug_log("settings", "create_settings_window: already have surface=%p", (void*)app.surface);
    return true;
  }

  const SettingsBenchClock::time_point t_create0 = SettingsBenchClock::now();
  app.embedOpenTime = t_create0;
  app.surface = wl_compositor_create_surface(app.wl.compositor());
  debug_log("settings", "create_settings_window: created surface=%p", (void*)app.surface);
  eh::wayland::attach_surface_extensions(app.wl, app.surface, app.surfaceExt);
  app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wl.xdg_base(), app.surface);
  xdg_surface_add_listener(app.xdgSurface, &g_xdg_surface_listener, &app);
  app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
  xdg_toplevel_add_listener(app.toplevel, &g_toplevel_listener, &app);
  xdg_toplevel_set_title(app.toplevel, "Dock \xe2\x80\x94 Event Horizon");
    xdg_toplevel_set_app_id(app.toplevel, eh::shell::kSettingsAppId);
  xdg_toplevel_set_min_size(app.toplevel, 480, 480);

  debug_log("settings", "create_settings_window: committing surface + roundtrip");
  wl_surface_commit(app.surface);
  const SettingsBenchClock::time_point t_after_commit = SettingsBenchClock::now();
  wl_display_roundtrip(app.wl.display());
  const SettingsBenchClock::time_point t_after_rt = SettingsBenchClock::now();
  debug_log("settings", "create_settings_window: roundtrip returned surface=%p",
            (void*)app.surface);
  if (eh_settings_bench()) {
    std::cerr << "[settings-bench] create_settings_window embedded=" << (app.embedded ? 1 : 0)
              << " surface+toplevel+commit=" << settings_bench_us(t_create0, t_after_commit)
              << "us wl_display_roundtrip=" << settings_bench_us(t_after_commit, t_after_rt)
              << "us (configure/draw may run inside roundtrip) TOTAL=" << settings_bench_us(t_create0, t_after_rt) << "us\n";
  }
  debug_log("settings", "create_settings_window exit: %s", app.surface ? "OK" : "FAILED");
  return app.surface != nullptr;
}

extern void on_settings_keyboard(App& app, const eh::wayland::WaylandSeat::KeyboardEvent& ev);

[[nodiscard]] static bool settings_app_connect_globals(App& app, bool embed_pointer_via_dock) {
  if (!app.wl.compositor() || !app.wl.shm() || !app.wl.xdg_base()) {
    debug_log("settings", "connect_globals FAIL: missing Wayland globals (compositor=%p shm=%p xdg=%p)",
              (void*)app.wl.compositor(), (void*)app.wl.shm(), (void*)app.wl.xdg_base());
    std::cerr << "Missing Wayland globals for settings app. Need wl_compositor, wl_shm, xdg_wm_base.\n";
    return false;
  }

  xdg_wm_base_add_listener(app.wl.xdg_base(), &g_wm_base_listener, &app);

  {
    auto* extMgr = app.wl.ext_data_control_manager();
    auto* wlrMgr = app.wl.wlr_data_control_manager();
    if (extMgr) {
      (void)app.clipboard.bind(extMgr, eh::wayland::ext_data_control_ops(), app.wl.seat(), app.wl.display());
    } else if (wlrMgr) {
      (void)app.clipboard.bind(wlrMgr, eh::wayland::wlr_data_control_ops(), app.wl.seat(), app.wl.display());
    }
  }

  if (!embed_pointer_via_dock) {
    app.seat.bind(app.wl.seat());
    app.seat.set_pointer_motion_cb(
        [&](wl_surface* surf, double x, double y) { on_pointer_motion(app, surf, x, y); });
    app.seat.set_pointer_button_cb([&](uint32_t b, uint32_t st) {
      on_pointer_button(app, app.seat.pointer_focus_surface(), b, st, 0);
    });
    app.seat.set_pointer_leave_cb([&]() { on_pointer_leave(app); });
    app.seat.set_pointer_axis_vertical_cb(
        [&](double delta_px) { settings_apply_wheel_scroll_delta(app, delta_px); });
    app.seat.set_keyboard_key_cb([&](const eh::wayland::WaylandSeat::KeyboardEvent& ev) { on_settings_keyboard(app, ev); });
  }
  app.buf[0].set_release_hook(&on_buf_release, &app);
  app.buf[1].set_release_hook(&on_buf_release, &app);
  return true;
}

namespace eh::settings {
namespace {
volatile sig_atomic_t g_settings_standalone_signal{0};
void settings_standalone_signal_handler(int) { g_settings_standalone_signal = 1; }
}

std::unique_ptr<App> g_embed;

void embed_register_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk) { settings_gpu_embed_shell_vk(std::move(vk)); }

namespace {

std::function<void()> g_settings_launcher;

std::string settings_pid_path() {
  const char* d = std::getenv("XDG_RUNTIME_DIR");
  return std::string(d && *d ? d : "/tmp") + "/event-horizon-settings.pid";
}

bool pid_alive(int pid) { return pid > 0 && ::kill(pid, 0) == 0; }

std::string settings_log_path() {
  const char* d = std::getenv("XDG_STATE_HOME");
  return std::string(d && *d ? d : "/tmp") + "/event-horizon-settings.log";
}

}  // namespace

void set_settings_launcher(SettingsLauncherFn fn) { g_settings_launcher = std::move(fn); }

void request_launch_settings() {
  if (g_settings_launcher) {
    g_settings_launcher();
    return;
  }
  (void)spawn_settings();
}

bool settings_singleton_running() {
  std::string p;
  {
    FILE* f = std::fopen(settings_pid_path().c_str(), "r");
    if (!f) return false;
    int pid = 0;
    const int got = std::fscanf(f, "%d", &pid);
    std::fclose(f);
    if (got != 1) return false;
    if (pid_alive(pid)) return true;
  }
  ::unlink(settings_pid_path().c_str());
  return false;
}

bool settings_singleton_acquire() {
  if (settings_singleton_running()) return false;
  FILE* f = std::fopen(settings_pid_path().c_str(), "w");
  if (!f) return true;
  std::fprintf(f, "%d\n", static_cast<int>(::getpid()));
  std::fclose(f);
  return true;
}

void settings_singleton_release() {
  FILE* f = std::fopen(settings_pid_path().c_str(), "r");
  if (!f) return;
  int pid = 0;
  const int got = std::fscanf(f, "%d", &pid);
  std::fclose(f);
  if (got != 1 || pid != static_cast<int>(::getpid())) return;
  ::unlink(settings_pid_path().c_str());
}

bool spawn_settings() {
  if (settings_singleton_running()) return false;

  // Reap the spawned settings asynchronously: without this an exiting
  // horizon-settings lingers as a zombie child of whichever shell component
  // launched it. The supervisor is unaffected — it keeps SIGCHLD blocked for
  // its signalfd, so this handler never fires there.
  static const bool reaper_installed = []() {
    struct sigaction sa {};
    sa.sa_handler = +[](int) {
      while (::waitpid(-1, nullptr, WNOHANG) > 0) {
      }
    };
    sigemptyset(&sa.sa_mask);
    return ::sigaction(SIGCHLD, &sa, nullptr) == 0;
  }();
  (void)reaper_installed;

  // Resolve the child binary: prefer a `horizon-settings` sitting next to our own
  // executable (works from a build dir / uninstalled tree), else rely on PATH.
  std::string childBin = "horizon-settings";
  {
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
      self[n] = '\0';
      const std::string exe = self;
      const auto slash = exe.find_last_of('/');
      const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
      if (::access((dir + "/horizon-settings").c_str(), X_OK) == 0)
        childBin = dir + "/horizon-settings";
    }
  }

  const std::string logPath = settings_log_path();
  const int logFd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (logFd >= 0) {
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, logFd);
  }

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

  char* argv[] = { const_cast<char*>(childBin.c_str()), nullptr };
  eh::proc::mark_child_parent_pid();
  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, childBin.c_str(), &actions, &attr, argv, environ);
  eh::proc::clear_child_parent_pid();

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) {
    debug_log("settings", "spawn_settings: posix_spawnp %s failed: %s", childBin.c_str(), strerror(rc));
    return false;
  }
  debug_log("settings", "spawn_settings: spawned %s pid=%d (log: %s)", childBin.c_str(), (int)pid, logPath.c_str());
  return true;
}

void redraw_settings_application(App& app) {
   
  draw(app);
}

// Standalone settings runs its own config cache. When the local async palette
// generation completes, the config cache is updated but nothing told the app to
// refresh; the shell's config.applied broadcast only covers cross-process saves.
// This hook reloads app.settings so sliders/toggles adopt the engine colors
// shortly after startup without any external event.
static App* g_standalone_settings_app = nullptr;

static void standalone_config_applied_hook(void*) {
   
  App* app = g_standalone_settings_app;
  if (!app || !app->surface) return;
  app->settings = load_settings();
  app->settingsDeferRedraw = true;
  app->pendingRedraw = true;
  app->settings_paint_shell_snapshot_valid = false;
}

[[nodiscard]] int run_standalone() {
  // Shell-spawned settings die with the supervisor even on hard kills; standalone
  // launches (the settings .desktop file) carry no marker and are unaffected.
  eh::proc::install_parent_death_guard();

  if (!settings_singleton_acquire()) {
    std::cerr << "Settings already running (another instance owns the pidfile).\n";
    return 0;
  }

  App app{};
  g_standalone_settings_app = &app;
  // Register before the async palette generation enqueued by load_settings()
  // completes so the local applied hook is captured when it fires.
  eh::config::shell_config_set_applied_hook(standalone_config_applied_hook, nullptr);
  // If the async generation finished between App construction and the hook
  // registration above, the hook callback may have been skipped (app was null);
  // detect that and refresh now so the controls still pick up the palette.
  {
    const eh::config::ShellConfig& c0 = eh::config::shell_config_snapshot();
    if (!app.settings.horizonColorsPaletteOk && !app.settings.matugenPaletteOk &&
        (c0.appearance.horizonColorsPaletteOk || c0.appearance.matugenPaletteOk)) {
      app.settings = load_settings();
      app.settingsDeferRedraw = true;
      app.pendingRedraw = true;
      app.settings_paint_shell_snapshot_valid = false;
    }
  }
  if (!app.wl.connect()) {
    settings_singleton_release();
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  if (!settings_app_connect_globals(app,  false)) { settings_singleton_release(); return 1; }

  if (!create_settings_window(app)) { settings_singleton_release(); return 1; }

  g_settings_standalone_signal = 0;
  {
    struct sigaction sa {};
    sa.sa_handler = settings_standalone_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    (void)sigaction(SIGINT, &sa, nullptr);
    (void)sigaction(SIGTERM, &sa, nullptr);
  }

  {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    std::cout << "Settings running XDG_STATE_HOME=\"" << (xdg ? xdg : "")
              << "\". Diagnostics: EH_BENCH=1 (all benches), EH_SETTINGS_DEBUG / EH_SETTINGS_BENCH (see settings_app.cpp); "
                 "wallpaper thumbs: EH_WALLPAPER_THUMB_DEBUG=1\n";
  }

  const int dpyFd = wl_display_get_fd(app.wl.display());
  const int thumbFd = eh::wallpaper::WallpaperThumbnailService::instance().wake_fd();
  auto& nm = eh::net::NetworkManagerService::instance();
  const int nmFd = nm.wake_fd();

  eh::ipc::IpcClient ipc;
  bool ipcOk = false;
  {
    const int cfd = ipc.connect(eh::ipc::default_socket_path(), 3);
    if (cfd >= 0) {
      ipc.set_event_handler([&app](std::string topic, std::string payload, std::vector<int>) {
        if (topic == "command.request" && payload == "settings.toggle") {
          app.running = false;
        } else if (topic == "config.applied") {
          eh::config::shell_config_reload_from_disk_now();
          // Reload app.settings so controls resolved from it (sliders/toggles via
          // settings_resolve_colors) pick up the freshly applied palette instead
          // of keeping the stale startup cache.
          app.settings = load_settings();
          app.settingsDeferRedraw = true;
        }
      });
      ipcOk = ipc.subscribe("config.applied") && ipc.subscribe("command.request");
    }
  }

  if (ipcOk) {
    eh::notify::setNotifySender([&ipc](const std::string& payload, const std::vector<int>& /*fds*/) {
      return ipc.publish(eh::notify::kPushTopic, payload);
    });
    app.ipcPublish = [&ipc](const std::string& topic, const std::string& payload) {
      return ipc.publish(topic, payload);
    };
  }

  // Short timeout so DeferredCall callbacks (async palette completion → applied
  // hook) are drained promptly even when no fd has events.
  constexpr int kSettingsStandalonePollMs = 250;
  while (app.running && g_settings_standalone_signal == 0) {
    pollfd pf[4]{};
    int n = 0;
    int thumbIdx = -1, nmIdx = -1, ipcIdx = -1;
    pf[n].fd = dpyFd;
    pf[n].events = POLLIN | POLLERR | POLLHUP;
    ++n;
    if (thumbFd >= 0) {
      pf[n].fd = thumbFd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      thumbIdx = n;
      ++n;
    }
    if (nmFd >= 0) {
      pf[n].fd = nmFd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      nmIdx = n;
      ++n;
    }
    if (ipcOk) {
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
        pf[n].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
        ipcIdx = n;
        ++n;
      }
    }
    const int pr = poll(pf, static_cast<nfds_t>(n), kSettingsStandalonePollMs);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_settings_standalone_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_settings_standalone_signal != 0) break;
    // Run callbacks posted from worker threads (async palette completion fires
    // the applied hook via DeferredCall). Without this the standalone process
    // would never notice the freshly generated color-engine palette.
    DeferredCall::drain();
    if ((pf[0].revents & (POLLERR | POLLHUP)) != 0) break;
    if (thumbIdx >= 0 && (pf[thumbIdx].revents & (POLLERR | POLLHUP)) != 0) break;
    if (nmIdx >= 0 && (pf[nmIdx].revents & (POLLERR | POLLHUP)) != 0) break;
    if (ipcIdx >= 0 && (pf[ipcIdx].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
      ipcOk = false;
      ipcIdx = -1;
    }

    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(app.wl.display()) < 0) break;
    } else {
      (void)wl_display_flush(app.wl.display());
    }
    if (app.settingsDeferRedraw || app.pendingRedraw) {
      app.settingsDeferRedraw = false;
      app.pendingRedraw = false;
      app.settings_paint_shell_snapshot_valid = false;
      if (app.surface) draw(app);
    }
    if (thumbIdx >= 0 && (pf[thumbIdx].revents & POLLIN)) {
      eh::wallpaper::WallpaperThumbnailService::instance().drain_wake();
      draw(app);
    }
    if (nmIdx >= 0 && (pf[nmIdx].revents & POLLIN)) {
      nm.handle_wake();
      draw(app);
    }
    if (ipcIdx >= 0 && (pf[ipcIdx].revents & POLLIN)) {
      ipc.on_fd_ready(pf[ipcIdx].fd);
    }
  }

  destroy_settings_surfaces(app);
  app.clipboard.cleanup();
  app.seat.unbind();
  app.wl.disconnect();
  eh::notify::setNotifySender(nullptr);
  settings_singleton_release();
  return 0;
}

[[nodiscard]] bool embed_init(::wl_display* display, ::wl_seat* shared_seat) {
  debug_log("settings", "embed_init: begin display=%p seat=%p", (void*)display, (void*)shared_seat);
  EH_ST_TRACE(std::cerr << "settings embed_init: begin display=" << static_cast<void*>(display) << " seat=" << static_cast<void*>(shared_seat));
  if (g_embed) {
    debug_log("settings", "embed_init: already initialized (g_embed=%p)", (void*)g_embed.get());
    return true;
  }
  g_embed = std::make_unique<App>();
  g_embed->embedded = true;
  debug_log("settings", "embed_init: created g_embed=%p, calling wl.attach(display=%p, seat=%p)",
            (void*)g_embed.get(), (void*)display, (void*)shared_seat);
  EH_ST_TRACE(std::cerr << "settings embed_init: WaylandConnection::attach");
  if (!g_embed->wl.attach(display, shared_seat)) {
    debug_log("settings", "embed_init FAIL: WaylandConnection::attach returned false");
    g_embed.reset();
    return false;
  }
  debug_log("settings", "embed_init: wl.attach OK, calling settings_app_connect_globals");
  EH_ST_TRACE(std::cerr << "settings embed_init: settings_app_connect_globals");
  if (!settings_app_connect_globals(*g_embed, /*embed_pointer_via_dock=*/true)) {
    debug_log("settings", "embed_init FAIL: settings_app_connect_globals returned false");
    g_embed->wl.disconnect();
    g_embed.reset();
    return false;
  }
  debug_log("settings", "embed_init: globals OK, loading logo");
  if (!g_embed->logo) g_embed->logo = load_logo_surface();
  debug_log("settings", "embed_init: registering redraw callbacks");
  eh::bing::BingWallpaperService::instance().register_redraw([]() {
    if (g_embed) draw(*g_embed);
  });
  eh::net::NetworkManagerService::instance().set_change_callback([](const eh::net::Snapshot&) {
    if (g_embed) draw(*g_embed);
  });
  debug_log("settings", "embed_init: OK returning true");
  EH_ST_TRACE(std::cerr << "settings embed_init: ok (no window until toggle)");
  return true;
}

bool embed_is_initialized() { return g_embed != nullptr; }

void embed_set_gamma_service(eh::wayland::GammaService* gs) {
  if (g_embed) g_embed->gammaService_ = gs;
}

void embed_shutdown() {
    
  debug_log("settings", "embed_shutdown: begin g_embed=%p", (void*)g_embed.get());
  eh::config::shell_config_clear_settings_drag_preview();
  settings_gpu_reset_shell_vk();
  if (!g_embed) {
    debug_log("settings", "embed_shutdown: no g_embed, returning");
    return;
  }
  if (g_embed->logo) {
    debug_log("settings", "embed_shutdown: destroying logo");
    cairo_surface_destroy(g_embed->logo);
    g_embed->logo = nullptr;
  }
  debug_log("settings", "embed_shutdown: destroying surfaces");
  destroy_settings_surfaces(*g_embed);
  debug_log("settings", "embed_shutdown: cleaning up clipboard/seat/wl");
  g_embed->clipboard.cleanup();
  g_embed->seat.unbind();
  g_embed->wl.disconnect();
  debug_log("settings", "embed_shutdown: resetting g_embed");
  g_embed.reset();
  debug_log("settings", "embed_shutdown: done");
}

[[nodiscard]] bool embed_window_visible() {
  return g_embed && g_embed->surface != nullptr;
}

static void embed_run_open_animation_and_create_window() {
   
  debug_log("settings", "open_anim: enter g_embed=%p", (void*)g_embed.get());
  if (!g_embed) {
    debug_log("settings", "open_anim: no g_embed, returning");
    return;
  }
  if (eh_settings_bench()) {
    ++s_settings_bench_session;
    s_settings_bench_t0 = SettingsBenchClock::now();
    s_settings_bench_draw_n = 0;
    std::cerr << "[settings-bench] \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 embed open session=" << s_settings_bench_session << " \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n";
  }
  debug_log("settings", "open_anim: loading settings");
  g_embed->settings = load_settings();
  const SettingsBenchClock::time_point t_open0 = SettingsBenchClock::now();
  g_embed->embedAnim.cancel_all();
  if (eh::shell::animations_reduced_motion()) {
    g_embed->embedPresentT = 1.f;
  } else {
    g_embed->embedPresentT = 0.f;
    g_embed->embedAnim.animate(0.f, 1.f, kEmbedOpenCloseMs, eh::shell::Easing::EaseOutCubic,
                                [&](float v) { g_embed->embedPresentT = v; }, {});
  }
  const SettingsBenchClock::time_point t_after_anim = SettingsBenchClock::now();
  debug_log("settings", "open_anim: calling create_settings_window");
  if (!create_settings_window(*g_embed)) {
    debug_log("settings", "open_anim: create_settings_window FAILED");
    g_embed->embedAnim.cancel_all();
    g_embed->embedPresentT = 1.f;
    return;
  }
  debug_log("settings", "open_anim: create_settings_window OK");

  const SettingsBenchClock::time_point t_after_create = SettingsBenchClock::now();
  const char* xdg = std::getenv("XDG_STATE_HOME");
  std::cout << "Settings (embedded) XDG_STATE_HOME=\"" << (xdg ? xdg : "") << "\"\n";
  debug_log("settings", "open_anim: calling explicit draw()");
  draw(*g_embed);
  debug_log("settings", "open_anim: explicit draw() returned");
  const SettingsBenchClock::time_point t_after_draw = SettingsBenchClock::now();
  if (g_embed->wl.display()) {
    (void)wl_display_flush(g_embed->wl.display());
    const SettingsBenchClock::time_point t_after_flush = SettingsBenchClock::now();
    SettingsBenchClock::time_point t_after_sync = t_after_flush;
    if (eh_settings_embed_post_open_roundtrip()) {
      debug_log("settings", "open_anim: post-open roundtrip (EH_SETTINGS_EMBED_SYNC=1)");
      (void)wl_display_roundtrip(g_embed->wl.display());
      t_after_sync = SettingsBenchClock::now();
      debug_log("settings", "open_anim: post-open roundtrip returned");
    }
    if (eh_settings_bench()) {
      std::cerr << "[settings-bench] open: post-draw flush=" << settings_bench_us(t_after_draw, t_after_flush) << "us ";
      if (eh_settings_embed_post_open_roundtrip())
        std::cerr << "roundtrip=" << settings_bench_us(t_after_flush, t_after_sync) << "us\n";
      else
        std::cerr << "roundtrip=skipped (set EH_SETTINGS_EMBED_SYNC=1 if first frame glitches)\n";
    }
  }
  const SettingsBenchClock::time_point t_open_end = SettingsBenchClock::now();
  debug_log("settings", "open_anim: done (total=%.0fms)", settings_bench_us(t_open0, t_open_end) / 1000.0);
  if (eh_settings_bench()) {
    std::cerr << "[settings-bench] open SUMMARY: anim_setup=" << settings_bench_us(t_open0, t_after_anim) << "us "
              << "create_settings_window=" << settings_bench_us(t_after_anim, t_after_create) << "us explicit_draw="
              << settings_bench_us(t_after_create, t_after_draw) << "us post_draw_flush_rt="
              << settings_bench_us(t_after_draw, t_open_end) << "us WALL_OPEN_TOTAL=" << settings_bench_us(t_open0, t_open_end)
              << "us (see per-draw + xdg_configure lines above)\n";
  }
}

static void embed_toggle_impl() {
   
  debug_log("settings", "embed_toggle_impl: g_embed=%p window_visible=%d",
            (void*)g_embed.get(), (int)embed_window_visible());
  if (!g_embed) {
    debug_log("settings", "embed_toggle_impl: no g_embed, returning");
    return;
  }
  if (embed_window_visible()) {
    debug_log("settings", "embed_toggle_impl: window visible, destroying");
    destroy_settings_surfaces(*g_embed);
    debug_log("settings", "embed_toggle_impl: surfaces destroyed");
    return;
  }
  debug_log("settings", "embed_toggle_impl: calling embed_run_open_animation_and_create_window");
  embed_run_open_animation_and_create_window();
  debug_log("settings", "embed_toggle_impl: embed_run_open_animation_and_create_window returned");
}

void embed_toggle() {
   
  debug_log("settings", "embed_toggle: g_embed=%p setting pendingToggle (deferred)", (void*)g_embed.get());
  if (!g_embed) return;
  g_embed->pendingToggle = true;
}

void embed_minimize() {
   
  debug_log("settings", "embed_minimize: g_embed=%p toplevel=%p", (void*)g_embed.get(),
            (void*)(g_embed ? g_embed->toplevel : nullptr));
  if (!g_embed || !g_embed->toplevel) return;
  xdg_toplevel_set_minimized(g_embed->toplevel);
}

void embed_maximize_toggle() {
   
  debug_log("settings", "embed_maximize_toggle: g_embed=%p toplevel=%p maximized=%d", (void*)g_embed.get(),
            (void*)(g_embed ? g_embed->toplevel : nullptr), g_embed ? (int)g_embed->maximized : -1);
  if (!g_embed || !g_embed->toplevel) return;
  g_embed->maximized = !g_embed->maximized;
  if (g_embed->maximized) {
    xdg_toplevel_set_maximized(g_embed->toplevel);
  } else {
    xdg_toplevel_unset_maximized(g_embed->toplevel);
  }
}

void embed_show_tab(int tab_index) {
   
  if (!g_embed) return;
  const int tab = std::clamp(tab_index, 0, 48);
  if (embed_window_visible()) {
    g_embed->activeTab = tab;
    draw(*g_embed);
    if (g_embed->wl.display()) (void)wl_display_flush(g_embed->wl.display());
    return;
  }
  g_embed->activeTab = tab;
  debug_log("settings", "embed_show_tab: deferring open (pendingToggle)");
  g_embed->pendingToggle = true;
}

wl_surface* embed_settings_surface() { return g_embed && g_embed->surface ? g_embed->surface : nullptr; }

wl_surface* embed_widget_picker_surface() {
   
  return g_embed && g_embed->widgetPickerSurface ? g_embed->widgetPickerSurface : nullptr;
}

wl_surface* embed_default_app_picker_surface() {
   
  if (!g_embed) return nullptr;
  if (g_embed->defaultAppPickerXdgWlSurface) return g_embed->defaultAppPickerXdgWlSurface;
  return g_embed->defaultAppPickerLayerWlSurface;
}

int thumbnail_wake_fd() {
  WP_SCOPE();
  return eh::wallpaper::WallpaperThumbnailService::instance().wake_fd();
}

void embed_wallpaper_thumbnail_poll() {
  WP_SCOPE(); 
  if (!g_embed) return;
  if (wl_display* dpy = g_embed->wl.display()) {
    (void)wl_display_dispatch_pending(dpy);
    embed_after_display_dispatch();
  }
  auto& thumbs = eh::wallpaper::WallpaperThumbnailService::instance();
  thumbs.drain_wake();
  if (!g_embed->surface) return;

  const bool decoded = thumbs.has_pending_completed();
  if (!decoded && !g_embed->pendingRedraw) return;

  if (g_embed->activeTab != 6) {
    if (eh_wallpaper_thumb_debug() && decoded) {
      static std::chrono::steady_clock::time_point lastTabSkipLog{};
      const auto now = std::chrono::steady_clock::now();
      if (now - lastTabSkipLog >= std::chrono::seconds(3)) {
        lastTabSkipLog = now;
        std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
        std::cerr << "[wallpaper-thumb] embed poll: thumbnails ready but activeTab=" << g_embed->activeTab
                  << " (wallpaper tab=6) \xe2\x80\x94 skipping draw; merge runs on next full draw when tab visible"
                  << " pendingRedraw=" << (g_embed->pendingRedraw ? 1 : 0) << "\n";
      }
    }
    if (g_embed->pendingRedraw) draw(*g_embed);
    return;
  }

  if (eh_wallpaper_thumb_debug()) {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(2)) {
      last = now;
      std::lock_guard<std::mutex> lk(eh::wallpaper::wallpaper_thumbnail_log_mutex());
      std::cerr << "[wallpaper-thumb] embed poll \xe2\x86\x92 draw (thumbnail eventfd readable)\n";
    }
  }
  draw(*g_embed);
}

void embed_request_redraw() {
    
  if (g_embed && g_embed->surface) {
    // Config (palette) may have been re-applied; refresh app.settings so
    // controls keep adopting the color-engine colors after a config reload.
    g_embed->settings = load_settings();
    g_embed->settingsDeferRedraw = true;
    g_embed->pendingRedraw = true;
    g_embed->settings_paint_shell_snapshot_valid = false;
  }
}

void embed_after_display_dispatch() {
    
  if (!g_embed) return;
  if (g_embed->pendingToggle) {
    g_embed->pendingToggle = false;
    debug_log("settings", "embed_after_dispatch: processing pendingToggle");
    embed_toggle_impl();
  }
  if (g_embed->settingsDeferRedraw || g_embed->pendingRedraw) {
    g_embed->settingsDeferRedraw = false;
    g_embed->pendingRedraw = false;
    if (g_embed->surface) draw(*g_embed);
  }
}

} // namespace eh::settings
