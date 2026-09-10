#include "../app.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <wayland-client.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "wl/core/seat.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"

namespace eh::disks {

// Globals.

namespace {
std::unique_ptr<AppState> g_app;
volatile sig_atomic_t g_signal{0};
void signal_handler(int) { g_signal = 1; }
}

// Buffer release hook.

static void on_buf_release(void* user) {
  auto& app = *static_cast<AppState*>(user);
  if (!app.surface) return;
  schedule_frame(app);
}

// Wayland listeners.

static void xdg_wm_base_ping(void*, xdg_wm_base* wm, uint32_t serial) {
  xdg_wm_base_pong(wm, serial);
}
static constexpr xdg_wm_base_listener kXdgWmBaseListener{
  .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void* data, xdg_surface* surface,
                                   uint32_t serial) {
  auto& app = *static_cast<AppState*>(data);
  xdg_surface_ack_configure(surface, serial);

  if (app.width <= 0 || app.height <= 0) return;

  bool needs_resize = (app.last_paint_w != app.width ||
                       app.last_paint_h != app.height);

  if (needs_resize && app.shm) {
    app.buf[0].ensure(app.shm, "eh-disks-a", app.width, app.height);
    app.buf[0].set_release_hook(on_buf_release, &app);
    app.buf[1].ensure(app.shm, "eh-disks-b", app.width, app.height);
    app.buf[1].set_release_hook(on_buf_release, &app);
  }

  if (!needs_resize && app.last_paint_w > 0) {
    return;
  }
  draw(app);
}

static void toplevel_configure(void* data, xdg_toplevel*, int32_t w, int32_t h,
                                wl_array*) {
  auto& app = *static_cast<AppState*>(data);
  if (w > 0) app.width = w;
  if (h > 0) app.height = h;
}

static void toplevel_close(void* data, xdg_toplevel*) {
  auto& app = *static_cast<AppState*>(data);
  app.running = false;
}

static constexpr xdg_surface_listener kXdgSurfaceListener{
  .configure = xdg_surface_configure,
};

static constexpr xdg_toplevel_listener kToplevelListener{
  .configure = toplevel_configure,
  .close = toplevel_close,
  .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
  .wm_capabilities = [](void*, xdg_toplevel*, wl_array*) {},
};

// Connect globals.

static bool connect_globals(AppState& app) {
  auto* display = app.wl.display();
  if (!display) return false;

  struct Globals {
    wl_compositor* compositor = nullptr;
    xdg_wm_base* xdgBase = nullptr;
    wl_shm* shm = nullptr;
  } g;

  wl_registry* registry = wl_display_get_registry(display);
  if (!registry) return false;

  static constexpr wl_registry_listener kRegListener{
    .global = [](void* data, wl_registry* reg, uint32_t name,
                  const char* iface, uint32_t) {
      auto& gl = *static_cast<Globals*>(data);
      if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        gl.compositor = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, 4));
      } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        gl.xdgBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, 2));
        xdg_wm_base_add_listener(gl.xdgBase, &kXdgWmBaseListener, nullptr);
      } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        gl.shm = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, 1));
      }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
  };

  wl_registry_add_listener(registry, &kRegListener, &g);
  wl_display_roundtrip(display);

  if (!g.compositor || !g.xdgBase || !g.shm) return false;

  app.shm = g.shm;
  app.buf[0].ensure(g.shm, "eh-disks-a", app.width, app.height);
  app.buf[1].ensure(g.shm, "eh-disks-b", app.width, app.height);

  if (app.wl.seat()) {
    app.seat.bind(app.wl.seat());
  }

  return true;
}

// Create window.

static bool create_window(AppState& app) {
  auto* display = app.wl.display();
  if (!display) return false;

  struct WinGlobals {
    wl_compositor* comp = nullptr;
    xdg_wm_base* xdg = nullptr;
    wl_shm* shm = nullptr;
  } wg;

  wl_registry* reg = wl_display_get_registry(display);
  static constexpr wl_registry_listener kWinRegListener{
    .global = [](void* data, wl_registry* reg, uint32_t name,
                  const char* iface, uint32_t) {
      auto& gl = *static_cast<WinGlobals*>(data);
      if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        gl.comp = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, 4));
      } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        gl.xdg = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, 2));
        xdg_wm_base_add_listener(gl.xdg, &kXdgWmBaseListener, nullptr);
      } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        gl.shm = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, 1));
      }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
  };
  wl_registry_add_listener(reg, &kWinRegListener, &wg);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);

  if (!wg.comp || !wg.xdg || !wg.shm) return false;

  app.shm = wg.shm;
  app.surface = wl_compositor_create_surface(wg.comp);
  if (!app.surface) return false;

  app.xdgSurface = xdg_wm_base_get_xdg_surface(wg.xdg, app.surface);
  if (!app.xdgSurface) return false;
  xdg_surface_add_listener(app.xdgSurface, &kXdgSurfaceListener, &app);

  app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
  if (!app.toplevel) return false;
  xdg_toplevel_add_listener(app.toplevel, &kToplevelListener, &app);
  xdg_toplevel_set_title(app.toplevel, "Disks");
  xdg_toplevel_set_app_id(app.toplevel, "horizon-disks");

  app.buf[0].ensure(wg.shm, "eh-disks-a", app.width, app.height);
  app.buf[0].set_release_hook(on_buf_release, &app);
  app.buf[1].ensure(wg.shm, "eh-disks-b", app.width, app.height);
  app.buf[1].set_release_hook(on_buf_release, &app);

  wl_surface_commit(app.surface);

  return true;
}

// Run standalone.

int run_standalone() {
  g_app = std::make_unique<AppState>();
  AppState& app = *g_app;

  if (!app.wl.connect()) {
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  if (!connect_globals(app)) {
    std::cerr << "Failed to connect Wayland globals.\n";
    return 1;
  }

  if (!create_window(app)) {
    std::cerr << "Failed to create disk utility window.\n";
    return 1;
  }

  // Initialize the icon cache from the system theme.
  {
    std::string iconTheme = eh::config::read_dock_icon_theme_from_disk();
    if (iconTheme.empty()) {
      iconTheme = eh::icons::detect_system_icon_theme();
    }
    if (!iconTheme.empty()) {
      app.icons.set_icon_theme(iconTheme);
    }
    app.icons.prewarm_search_dirs();
  }

  // Apply the dynamic colour palette.
  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    eh::config::ShellAppearance ap = sc.appearance;
    eh::matugen::refresh_wallpaper_derived_palette(ap, sc.wallpaperImage);
    const auto mc = eh::config::derived_chrome_colors(ap);
    app.bgR = mc.drawerDimR;
    app.bgG = mc.drawerDimG;
    app.bgB = mc.drawerDimB;
    app.surfaceR = mc.dockFillR;
    app.surfaceG = mc.dockFillG;
    app.surfaceB = mc.dockFillB;
    app.accentR = mc.accentR;
    app.accentG = mc.accentG;
    app.accentB = mc.accentB;
    app.outlineR = mc.outlineR;
    app.outlineG = mc.outlineG;
    app.outlineB = mc.outlineB;
    app.textR = mc.textR;
    app.textG = mc.textG;
    app.textB = mc.textB;
  }

  // Start the UDisks2 backend.
  auto& mgr = Manager::instance();
  mgr.set_change_callback([&app]() {
    app.needs_refresh = true;
    schedule_frame(app);
  });
  mgr.start();

  // Wire input callbacks.
  disk_log("startup pointer=%s seat=%s ndrives=%zu selected=%d",
           app.seat.pointer() ? "yes" : "NO",
           app.wl.seat() ? "yes" : "NO",
           Manager::instance().get_drives().size(),
           app.selected_drive);

  if (app.seat.pointer()) {
    app.seat.set_pointer_enter_cb(
        [&app](double x, double y) {
          app.pointerX = x;
          app.pointerY = y;
          disk_log("enter x=%d y=%d", static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          handle_move(app, static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_button_cb(
        [&app](uint32_t button, uint32_t state) {
          if (state == 1) {
            handle_click(app, static_cast<int>(app.pointerX),
                         static_cast<int>(app.pointerY),
                         static_cast<int>(button));
          }
        });
    app.seat.set_pointer_axis_vertical_cb(
        [&app](double delta_px) {
          handle_scroll(app, static_cast<int>(app.pointerX),
                        static_cast<int>(app.pointerY), delta_px);
        });
  }

  // Signal handling.
  g_signal = 0;
  {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
  }

  std::cout << "Event Horizon Disks started.\n";

  // Initial draw
  draw(app);

  // Event loop.
  const int dpy_fd = wl_display_get_fd(app.wl.display());

  while (app.running && g_signal == 0) {
    // Refresh the drive tree if needed.
    if (app.needs_refresh) {
      app.needs_refresh = false;
      auto fresh = mgr.get_drives();
      int prev_sel = app.selected_drive;
      if (app.selected_drive >= static_cast<int>(fresh.size())) {
        app.selected_drive = fresh.empty() ? -1 : 0;
        disk_log("refresh reset %d -> %d ndrives=%zu",
                 prev_sel, app.selected_drive, fresh.size());
      }
      schedule_frame(app);
    }

    // Poll the Wayland display fd.
    struct pollfd pf{};
    pf.fd = dpy_fd;
    pf.events = POLLIN | POLLERR | POLLHUP;

    int pr = poll(&pf, 1, 100);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_signal != 0) break;
    if (pf.revents & (POLLERR | POLLHUP)) break;

    if (pf.revents & POLLIN) {
      if (wl_display_dispatch(app.wl.display()) < 0) break;
    } else {
      wl_display_flush(app.wl.display());
    }

    // Redraw immediately if a frame is pending.
    if (app.pendingRedraw) {
      app.pendingRedraw = false;
      if (app.surface) draw(app);
    }

    // Process the mount result.
    {
      std::lock_guard<std::mutex> lock(app.mtx);
      if (app.mountPending) {
        app.mountPending = false;
        app.opActive = false;
        if (app.mountResultOk) {
          auto drives = mgr.get_drives();
          if (app.selected_drive >= 0 &&
              app.selected_drive < static_cast<int>(drives.size())) {
            for (auto& block : drives[app.selected_drive]->blocks()) {
              block->invalidate_features();
            }
          }
          schedule_frame(app);
        }
      }
      if (app.unmountPending) {
        app.unmountPending = false;
        app.opActive = false;
        if (app.unmountResultOk) {
          auto drives = mgr.get_drives();
          if (app.selected_drive >= 0 &&
              app.selected_drive < static_cast<int>(drives.size())) {
            for (auto& block : drives[app.selected_drive]->blocks()) {
              block->invalidate_features();
            }
          }
          schedule_frame(app);
        }
      }
    }

    if (app.pendingRedraw) {
      app.pendingRedraw = false;
      if (app.surface) draw(app);
    }
  }

  // Cleanup.
  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.seat.unbind();
  app.wl.disconnect();

  g_app = nullptr;
  return 0;
}

}
