#include "ux/Live_Wallpaper/embed/embed.hpp"
#include "ux/Live_Wallpaper/app.hpp"
#include "ux/Live_Wallpaper/settings.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <memory>

#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <iostream>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "desktop_shell/common/asset/asset_loader.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail_service.hpp"
// Standalone: removed main settings_serialize dep as part of full separation.
#include "wl/core/connection.hpp"
#include "wl/core/seat.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"

namespace eh::live_wallpaper {

static std::unique_ptr<AppState> g_app;

namespace {
volatile sig_atomic_t g_signal{0};
void signal_handler(int) { g_signal = 1; }
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

  const bool needs_resize = (app.last_paint_w != app.width ||
                             app.last_paint_h != app.height);

  if (needs_resize && app.shm) {
    app.buf[0].ensure(app.shm, "eh-wp-a", app.width, app.height);
    app.buf[1].ensure(app.shm, "eh-wp-b", app.width, app.height);
  }

  if (!needs_resize && app.last_paint_w > 0) return;

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

// Buffer release.

static void on_buf_release(void* data, wl_buffer*) {
  auto& app = *static_cast<AppState*>(data);
  if (!app.surface) return;
  app.pendingRedraw = false;
  draw(app);
}

static constexpr wl_buffer_listener kBufListener{
  .release = on_buf_release,
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
  app.buf[0].ensure(g.shm, "eh-wp-shm-a", app.width, app.height);
  app.buf[1].ensure(g.shm, "eh-wp-shm-b", app.width, app.height);

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
  xdg_toplevel_set_title(app.toplevel, "Horizon Live Wallpaper");
  xdg_toplevel_set_app_id(app.toplevel, "horizon-live-wallpaper");

  app.buf[0].ensure(wg.shm, "eh-wp-a", app.width, app.height);
  wl_buffer_add_listener(app.buf[0].wl(), &kBufListener, &app);
  app.buf[1].ensure(wg.shm, "eh-wp-b", app.width, app.height);
  wl_buffer_add_listener(app.buf[1].wl(), &kBufListener, &app);

  wl_surface_commit(app.surface);

  return true;
}

// Run standalone.

[[nodiscard]] int run_standalone() {
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
    std::cerr << "Failed to create wallpaper window.\n";
    return 1;
  }

  // Load settings for the *standalone* live wallpaper (fully separated).
  // We no longer pull the monolithic main ::Settings. LiveWallpaperConfig is local.
  // (lw_load will populate from our own live_wallpaper/live_wallpaper.toml)
  lw_load_settings(app.config);
  lw_load_ui_config(app);

  // Apply dynamic color palette
  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    eh::config::ShellAppearance ap = sc.appearance;
    eh::matugen::refresh_wallpaper_derived_palette(ap, sc.wallpaperImage);
    const auto mc = eh::config::derived_chrome_colors(ap);
    app.drawChrome = mc;
    app.drawChromeMatugen = ap.anyPaletteActive();
  }

  // Wire up input events
  if (app.seat.pointer()) {
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          handle_pointer_move(app, static_cast<int>(x), static_cast<int>(y));
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
                        static_cast<int>(app.pointerY), 0.0, delta_px);
        });
  }

  // Signal handling
  g_signal = 0;
  {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
  }

  // Initial draw
  draw(app);

  // Event loop
  const int dpy_fd = wl_display_get_fd(app.wl.display());
  const int thumb_fd = eh::wallpaper::WallpaperThumbnailService::instance().wake_fd();
  constexpr int kPollMs = 1000;

  while (app.running && g_signal == 0) {
    // Process pending thumbnail decodes
    {
      bool any_new = false;
      auto& svc = eh::wallpaper::WallpaperThumbnailService::instance();
      auto completed = svc.take_completed();
      for (auto& decoded : completed) {
        const std::string& path = decoded.path;
        cairo_surface_t* surf = eh::wallpaper::thumbnail_decoded_to_surface(decoded);
        auto it = app.liveWallpaperThumbs.find(path);
        if (it != app.liveWallpaperThumbs.end()) {
          cairo_surface_destroy(it->second);
          it->second = surf;
        } else {
          app.liveWallpaperThumbs[path] = surf;
        }
        { auto bit = app.liveWallpaperBlurredThumbs.find(path); if (bit != app.liveWallpaperBlurredThumbs.end()) { cairo_surface_destroy(bit->second); app.liveWallpaperBlurredThumbs.erase(bit); } }
        app.liveWallpaperThumbLru.remove(path);
        app.liveWallpaperThumbLru.push_back(path);
        any_new = true;

        if (path == app.liveWallpaperHeroRequest && path == app.config.image) {
          if (app.liveWallpaperHeroSurf) cairo_surface_destroy(app.liveWallpaperHeroSurf);
          app.liveWallpaperHeroSurf = surf;
          app.liveWallpaperHeroPath = path;
          app.liveWallpaperHeroRequest.clear();
          cairo_surface_reference(surf);
        }
      }
      // Evict LRU beyond a generous limit
      while (app.liveWallpaperThumbLru.size() > 200) {
        const std::string evict = app.liveWallpaperThumbLru.front();
        app.liveWallpaperThumbLru.pop_front();
        auto it = app.liveWallpaperThumbs.find(evict);
        if (it != app.liveWallpaperThumbs.end()) {
          cairo_surface_destroy(it->second);
          app.liveWallpaperThumbs.erase(it);
        }
        auto bit = app.liveWallpaperBlurredThumbs.find(evict);
        if (bit != app.liveWallpaperBlurredThumbs.end()) {
          cairo_surface_destroy(bit->second);
          app.liveWallpaperBlurredThumbs.erase(bit);
        }
      }
      if (any_new) {
        draw(app);
      }
    }

    // Process one pending video thumbnail per event loop iteration
    if (!app.liveWallpaperPendingVideoThumbs.empty()) {
      int64_t t0 = lw_now_us();
      live_wallpaper_process_video_thumbnails(app);
      int64_t dt = lw_now_us() - t0;
      if (dt > 50000)
        LW_LOG("embed process_video_thumb dt=%lldms remaining=%zu",
               (long long)(dt/1000), app.liveWallpaperPendingVideoThumbs.size());
      draw(app);
    }

    // Poll Wayland display and thumbnail service wake fd
    if (g_signal != 0) break;
    if (!app.running) break;
    struct pollfd pf[2]{};
    int n = 0;
    pf[n].fd = dpy_fd;
    pf[n].events = POLLIN | POLLERR | POLLHUP;
    ++n;
    if (thumb_fd >= 0) {
      pf[n].fd = thumb_fd;
      pf[n].events = POLLIN | POLLERR | POLLHUP;
      ++n;
    }

    int pr = poll(pf, static_cast<nfds_t>(n), kPollMs);
    if (pr < 0) {
      if (errno == EINTR) {
        if (g_signal != 0) break;
        continue;
      }
      break;
    }
    if (g_signal != 0) break;
    if (pf[0].revents & (POLLERR | POLLHUP)) break;

    if (pf[0].revents & POLLIN) {
      if (wl_display_dispatch(app.wl.display()) < 0) break;
    } else {
      wl_display_flush(app.wl.display());
    }

    if (thumb_fd >= 0 && n > 1 && (pf[1].revents & POLLIN)) {
      eh::wallpaper::WallpaperThumbnailService::instance().drain_wake();
    }

    if (app.pendingRedraw) {
      app.pendingRedraw = false;
      if (app.surface) draw(app);
    }
  }

  // Cleanup
  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.seat.unbind();
  app.wl.disconnect();

  g_app = nullptr;
  return 0;
}

} // namespace eh::live_wallpaper
