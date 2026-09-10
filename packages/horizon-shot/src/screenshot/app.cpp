#include "screenshot/app.hpp"
#include "screenshot/ui/paint.hpp"
#include "screenshot/ui/input.hpp"
#include "screenshot/ui/layout.hpp"

#include "core/shell_config.hpp"
#include "core/wayland_connection.hpp"
#include "core/wayland_seat.hpp"
#include "xdg-shell-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"

#include "core/logging.hpp"

#include <algorithm>
#include <cstdarg>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

#include <poll.h>
#include <unistd.h>

namespace hs::screenshot {

AppState::AppState() = default;

AppState::~AppState()
{
  if (cached_img) cairo_surface_destroy(cached_img);
}

static volatile sig_atomic_t g_signal = 0;
static void signal_handler(int) { g_signal = 1; }

static void xdg_wm_base_ping(void*, xdg_wm_base* wm, uint32_t serial)
{
  xdg_wm_base_pong(wm, serial);
}

static constexpr xdg_wm_base_listener kXdgWmBaseListener{
  .ping = xdg_wm_base_ping,
};

static void xdg_surface_configure(void* data, xdg_surface* surface, uint32_t serial)
{
  auto& app = *static_cast<AppState*>(data);
  HS_LOG("xdg_surface_configure: serial=%u w=%d h=%d", serial, app.width, app.height);
  xdg_surface_ack_configure(surface, serial);
  if (app.width <= 0 || app.height <= 0) return;
  if (app.width < app.minWidth) app.width = app.minWidth;
  if (app.height < app.minHeight) app.height = app.minHeight;
  paint_frame(app);
}

static void toplevel_configure(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array*)
{
  auto& app = *static_cast<AppState*>(data);
  HS_LOG("toplevel_configure: w=%d h=%d", w, h);
  if (w > 0) app.width = w;
  if (h > 0) app.height = h;
  if (app.width < app.minWidth) app.width = app.minWidth;
  if (app.height < app.minHeight) app.height = app.minHeight;
}

static void toplevel_close(void* data, xdg_toplevel*)
{
  auto& app = *static_cast<AppState*>(data);
  HS_LOG("toplevel_close");
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

static void on_shm_release(void* user)
{
  auto& app = *static_cast<AppState*>(user);
  HS_LOG("on_shm_release");
  if (!app.surface) return;
  app.pendingRedraw = false;
  paint_frame(app);
}

static void refresh_window_list(AppState& app)
{
  HS_LOG("refresh_window_list: enter xwayland=%d has_ext=%d has_wlr=%d",
         app.wl.has_xwayland(),
         app.wl.has_ext_foreign_toplevel_list(),
         app.wl.has_wlr_foreign_toplevel_manager());
  app.window_list.clear();

  if (app.wl.has_ext_foreign_toplevel_list()) {
    const auto& toplevels = app.wl.ext_foreign_toplevels().list();
    HS_LOG("refresh_window_list: %zu ext toplevels found", toplevels.size());
    for (const auto& tl : toplevels) {
      std::string xw = tl.appId.find("XWayland") != std::string::npos ? " [XWayland]" : "";
      HS_LOG("refresh_window_list:   ext window handle=%p title='%s' app_id='%s' identifier='%s'%s",
             (void*)tl.handle, tl.title.c_str(), tl.appId.c_str(), tl.identifier.c_str(), xw.c_str());
      WindowEntry entry;
      entry.handle = tl.handle;
      entry.appId = tl.appId;
      entry.title = tl.title;
      entry.identifier = tl.identifier;
      app.window_list.push_back(std::move(entry));
    }
  } else if (app.wl.has_wlr_foreign_toplevel_manager()) {
    const auto& toplevels = app.wl.wlr_foreign_toplevels().list();
    HS_LOG("refresh_window_list: %zu wlr toplevels found", toplevels.size());
    for (const auto& tl : toplevels) {
      std::string xw = tl.appId.find("XWayland") != std::string::npos ? " [XWayland]" : "";
      HS_LOG("refresh_window_list:   wlr window handle=%p title='%s' app_id='%s' activated=%d parent=%p%s",
             (void*)tl.handle, tl.title.c_str(), tl.appId.c_str(), tl.activated, (void*)tl.parent, xw.c_str());
      WindowEntry entry;
      entry.handle = nullptr;
      entry.wlr_handle = tl.handle;
      entry.appId = tl.appId;
      entry.title = tl.title;
      app.window_list.push_back(std::move(entry));
    }
  } else {
    HS_LOG("refresh_window_list: no toplevel list available");
    return;
  }

  if (app.window_list.empty()) {
    HS_LOG("refresh_window_list: WARNING — window list is empty after refresh");
  }

  if (app.source == Source::Window && app.selected_window_idx < 0 && !app.window_list.empty()) {
    app.selected_window_idx = 0;
  }

  if (app.selected_window_idx >= static_cast<int>(app.window_list.size())) {
    app.selected_window_idx = app.window_list.empty() ? -1 : 0;
  }
}

static void init_palette(AppState& app)
{
  HS_LOG("init_palette: enter");
  const auto& sc = hs::config::shell_config_snapshot_skip_matugen();
  app.chrome = hs::config::derived_chrome_colors(sc.appearance);
}

static bool bind_globals(AppState& app)
{
  HS_LOG("bind_globals: enter");
  auto* display = app.wl.display();
  if (!display) { HS_LOG("bind_globals: no display"); return false; }

  struct Globals {
    wl_compositor* compositor = nullptr;
    xdg_wm_base* xdgBase = nullptr;
    wl_shm* shm = nullptr;
  } g;

  wl_registry* registry = wl_display_get_registry(display);
  if (!registry) return false;

  static constexpr wl_registry_listener kRegListener{
    .global = [](void* data, wl_registry* reg, uint32_t name,
                  const char* iface, uint32_t version) {
      auto& gl = *static_cast<Globals*>(data);
      if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        gl.compositor = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
      } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        gl.xdgBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, std::min(version, 7u)));
        xdg_wm_base_add_listener(gl.xdgBase, &kXdgWmBaseListener, nullptr);
      } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        gl.shm = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, std::min(version, 1u)));
      }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
  };

  wl_registry_add_listener(registry, &kRegListener, &g);
  wl_display_roundtrip(display);

  if (!g.compositor || !g.xdgBase || !g.shm) {
    HS_LOG("bind_globals: FAILED compositor=%p xdgBase=%p shm=%p",
            (void*)g.compositor, (void*)g.xdgBase, (void*)g.shm);
    return false;
  }

  app.compositor = g.compositor;
  app.shm = g.shm;
  HS_LOG("bind_globals: compositor=%p xdgBase=%p shm=%p seat=%p",
          (void*)g.compositor, (void*)g.xdgBase, (void*)g.shm, (void*)app.wl.seat());

  if (app.wl.seat()) {
    app.seat.bind(app.wl.seat());
    HS_LOG("bind_globals: seat bound, has_pointer=%d", app.seat.pointer() != nullptr);
  }

  HS_LOG("bind_globals: success");
  return true;
}

static bool create_window(AppState& app)
{
  HS_LOG("create_window: enter");
  auto* display = app.wl.display();
  if (!display) { HS_LOG("create_window: no display"); return false; }

  struct WinGlobals {
    wl_compositor* comp = nullptr;
    xdg_wm_base* xdg = nullptr;
    wl_shm* shm = nullptr;
  } wg;

  wl_registry* reg = wl_display_get_registry(display);
  static constexpr wl_registry_listener kWinRegListener{
    .global = [](void* data, wl_registry* reg, uint32_t name,
                  const char* iface, uint32_t version) {
      auto& gl = *static_cast<WinGlobals*>(data);
      if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        gl.comp = static_cast<wl_compositor*>(
            wl_registry_bind(reg, name, &wl_compositor_interface, std::min(version, 4u)));
      } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        gl.xdg = static_cast<xdg_wm_base*>(
            wl_registry_bind(reg, name, &xdg_wm_base_interface, std::min(version, 7u)));
        xdg_wm_base_add_listener(gl.xdg, &kXdgWmBaseListener, nullptr);
      } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        gl.shm = static_cast<wl_shm*>(
            wl_registry_bind(reg, name, &wl_shm_interface, std::min(version, 1u)));
      }
    },
    .global_remove = [](void*, wl_registry*, uint32_t) {},
  };
  wl_registry_add_listener(reg, &kWinRegListener, &wg);
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);

  if (!wg.comp || !wg.xdg || !wg.shm) {
    HS_LOG("create_window: FAILED comp=%p xdg=%p shm=%p",
            (void*)wg.comp, (void*)wg.xdg, (void*)wg.shm);
    return false;
  }

  app.shm = wg.shm;
  app.compositor = wg.comp;
  app.surface = wl_compositor_create_surface(wg.comp);
  if (!app.surface) { HS_LOG("create_window: wl_compositor_create_surface failed"); return false; }
  HS_LOG("create_window: surface=%p", (void*)app.surface);

  app.xdgSurface = xdg_wm_base_get_xdg_surface(wg.xdg, app.surface);
  if (!app.xdgSurface) { HS_LOG("create_window: xdg_wm_base_get_xdg_surface failed"); return false; }
  xdg_surface_add_listener(app.xdgSurface, &kXdgSurfaceListener, &app);

  app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
  if (!app.toplevel) { HS_LOG("create_window: xdg_surface_get_toplevel failed"); return false; }
  xdg_toplevel_add_listener(app.toplevel, &kToplevelListener, &app);
  xdg_toplevel_set_title(app.toplevel, "Screenshot");
  xdg_toplevel_set_app_id(app.toplevel, "event-horizon-screenshot");
  xdg_toplevel_set_min_size(app.toplevel, app.minWidth, app.minHeight);

  if (!app.buf[0].ensure(wg.shm, "eh-shot-a", app.width, app.height)) {
    HS_LOG("create_window: buf[0].ensure failed");
    return false;
  }
  app.buf[0].set_release_hook(on_shm_release, &app);
  if (!app.buf[1].ensure(wg.shm, "eh-shot-b", app.width, app.height)) {
    HS_LOG("create_window: buf[1].ensure failed");
    return false;
  }
  app.buf[1].set_release_hook(on_shm_release, &app);

  wl_surface_commit(app.surface);
  wl_display_flush(display);

  HS_LOG("create_window: success");
  return true;
}

int run_screenshot_app(bool select_on_launch)
{
  HS_LOG("run_screenshot_app: enter select_on_launch=%d", select_on_launch);
  (void)fopen("/tmp/eh-shot.log", "w");
  auto app_ptr = std::make_unique<AppState>();
  AppState& app = *app_ptr;

  if (select_on_launch) {
    app.source = Source::Selection;
  }

  init_palette(app);

  if (!app.wl.connect()) {
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  HS_LOG("run_screenshot_app: connected to Wayland");

  wl_display_roundtrip(app.wl.display());

  if (!bind_globals(app)) {
    std::cerr << "Failed to bind Wayland globals.\n";
    return 1;
  }
  HS_LOG("run_screenshot_app: globals bound");

  {
    const auto sc = hs::config::shell_config_snapshot_skip_matugen();
    app.iconCache.set_icon_theme(sc.appearance.iconTheme);
  }

  if (!create_window(app)) {
    std::cerr << "Failed to create window.\n";
    return 1;
  }
  HS_LOG("run_screenshot_app: window created (surface=%p)", (void*)app.surface);

  wl_display_roundtrip(app.wl.display());
  HS_LOG("run_screenshot_app: after roundtrip (expecting configure event)");

  {
    auto* extMgr = app.wl.ext_data_control_manager();
    auto* wlrMgr = app.wl.wlr_data_control_manager();
    if (extMgr) {
      app.clipboard.bind_ext(extMgr, app.wl.seat(), app.wl.display());
      HS_LOG("run_screenshot_app: clipboard bound (ext)");
    } else if (wlrMgr) {
      app.clipboard.bind_wlr(wlrMgr, app.wl.seat(), app.wl.display());
      HS_LOG("run_screenshot_app: clipboard bound (wlr)");
    } else {
      HS_LOG("run_screenshot_app: no clipboard manager available");
    }
  }

  if (app.wl.has_any_toplevel_list()) {
    wl_display_roundtrip(app.wl.display());
    wl_display_roundtrip(app.wl.display());
    refresh_window_list(app);
    app.toplevel_available = true;
    HS_LOG("run_screenshot_app: toplevel list available, %zu windows found",
            app.window_list.size());
  } else {
    HS_LOG("run_screenshot_app: no toplevel list - window mode unavailable");
  }

  refresh_window_list(app);
  app.status = "Ready";

  if (app.seat.pointer()) {
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          app.pointer_x = x;
          app.pointer_y = y;
          handle_motion(app, static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_button_cb(
        [&app](uint32_t, uint32_t state) {
          if (state == 1) {
            handle_click(app, static_cast<int>(app.pointer_x),
                         static_cast<int>(app.pointer_y));
          } else {
            handle_release(app);
          }
        });
    app.seat.set_pointer_leave_cb(
        [&app]() {
          app.dragging = false;
          app.pressed_item = -1;
          app.pressed_area = 0;
          app.hovered_item = -1;
          app.hovered_area = 0;
          app.pendingRedraw = true;
        });
    app.seat.set_pointer_axis_vertical_cb(
        [&app](double delta_px) {
          handle_scroll(app, static_cast<int>(app.pointer_x),
                        static_cast<int>(app.pointer_y), delta_px);
        });
  }

  g_signal = 0;
  {
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
  }

  paint_frame(app);

  if (select_on_launch) {
    wl_display_roundtrip(app.wl.display());
    wl_display_roundtrip(app.wl.display());
    trigger_capture(app);
  }

  const int dpy_fd = wl_display_get_fd(app.wl.display());
  constexpr int kPollMs = 5000;

  while (app.running && g_signal == 0) {
    if (app.toplevel_available) {
      if (app.wl.has_wlr_foreign_toplevel_manager()
            ? app.wl.wlr_foreign_toplevels().dirty()
            : app.refresh_counter++ % 10 == 0) {
        refresh_window_list(app);
      }
    }

    struct pollfd pf{};
    pf.fd = dpy_fd;
    pf.events = POLLIN | POLLERR | POLLHUP;

    int pr = poll(&pf, 1, kPollMs);
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

    if (app.pendingRedraw) {
      app.pendingRedraw = false;
      if (app.surface) paint_frame(app);
    }
  }

  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.seat.unbind();
  app.wl.disconnect();

  if (!app.last_capture_path.empty()) {
    unlink(app.last_capture_path.c_str());
  }

  app_ptr = nullptr;
  HS_LOG("run_screenshot_app: exit success");
  return 0;
}

int run_screenshot_cli(const AppOptions& opts)
{
  HS_LOG("run_screenshot_cli: enter mode=%d output=%s copy=%d", (int)opts.mode, opts.output_path.c_str(), opts.copy);

  if (opts.mode == AppOptions::Gui && !opts.list_windows) {
    return run_screenshot_app(false);
  }

  bool ok = false;
  std::string out_path = opts.output_path;

  hs::core::WaylandConnection wl;
  if (!wl.connect()) {
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }
  wl_display_roundtrip(wl.display());

  hs::core::ClipboardService clipboard;
  if (opts.copy) {
    auto* extMgr = wl.ext_data_control_manager();
    auto* wlrMgr = wl.wlr_data_control_manager();
    if (extMgr) {
      clipboard.bind_ext(extMgr, wl.seat(), wl.display());
    } else if (wlrMgr) {
      clipboard.bind_wlr(wlrMgr, wl.seat(), wl.display());
    }
  }

  if (opts.list_windows) {
    wl_display_roundtrip(wl.display());
    wl_display_roundtrip(wl.display());
    wl_display_roundtrip(wl.display());

    if (wl.has_ext_foreign_toplevel_list()) {
      const auto& toplevels = wl.ext_foreign_toplevels().list();
      if (toplevels.empty()) {
        std::cout << "No windows detected.\n";
      } else {
        for (size_t i = 0; i < toplevels.size(); ++i) {
          const auto& tl = toplevels[i];
          std::cout << i << ": app_id='" << tl.appId << "' title='" << tl.title << "'"
                    << " identifier='" << tl.identifier << "'\n";
        }
      }
    } else if (wl.has_wlr_foreign_toplevel_manager()) {
      const auto& toplevels = wl.wlr_foreign_toplevels().list();
      if (toplevels.empty()) {
        std::cout << "No windows detected.\n";
      } else {
        for (size_t i = 0; i < toplevels.size(); ++i) {
          const auto& tl = toplevels[i];
          std::cout << i << ": app_id='" << tl.appId << "' title='" << tl.title << "'"
                    << " activated=" << tl.activated << "\n";
        }
      }
    } else {
      std::cerr << "No toplevel list available from compositor.\n";
    }
    return 0;
  }

  if (out_path.empty()) {
    char tmp_pattern[] = "/tmp/horizon-shot-XXXXXX";
    int fd = mkstemp(tmp_pattern);
    if (fd < 0) { std::cerr << "Failed to create temp file\n"; return 1; }
    close(fd);
    out_path = std::string(tmp_pattern) + ".png";
  }

  switch (opts.mode) {
  case AppOptions::Select:
    {
      wl.refresh_logical_outputs();
      auto bounds = wl.logical_output_bounds();
      ok = capture_selection_interactive(wl, bounds, out_path);
      if (ok) wl_display_roundtrip(wl.display());
    }
    break;

  case AppOptions::Focused:
    ok = capture_focused_window(wl, out_path);
    break;

  case AppOptions::Screen:
    {
      hs::screenshot::HdrData hdr;
      if (!opts.output_name.empty()) {
        wl.refresh_logical_outputs();
        auto outputs = list_outputs(wl);
        wl_output* target = nullptr;
        for (const auto& o : outputs) {
          if (o.name == opts.output_name) { target = o.output; break; }
        }
        if (!target) {
          std::cerr << "Output not found: " << opts.output_name << "\n";
          return 1;
        }
        ok = capture_output(wl, target, out_path, opts.capture_hdr ? &hdr : nullptr);
      } else {
        ok = capture_all_screens(wl, out_path, opts.capture_hdr ? &hdr : nullptr);
      }
    }
    break;

  case AppOptions::Window:
    {
      wl_display_roundtrip(wl.display());
      if (!wl.has_ext_foreign_toplevel_list() && !wl.has_wlr_foreign_toplevel_manager()) {
        std::cerr << "No toplevel list available from compositor.\n";
        return 1;
      }
      wl_display_roundtrip(wl.display());
      wl_display_roundtrip(wl.display());

      if (opts.window_selector.empty()) {
        std::cerr << "Use --window <index>, --window <title>, or --window <app_id>.\n";
        std::cerr << "Use --list-windows to see available windows.\n";
        return 1;
      }

      ext_foreign_toplevel_handle_v1* handle = nullptr;
      bool found = false;

      if (wl.has_ext_foreign_toplevel_list()) {
        const auto& toplevels = wl.ext_foreign_toplevels().list();
        char* end = nullptr;
        long idx = std::strtol(opts.window_selector.c_str(), &end, 10);
        if (*end == '\0' && idx >= 0 && static_cast<size_t>(idx) < toplevels.size()) {
          handle = toplevels[idx].handle;
          found = true;
        } else {
          for (size_t i = 0; i < toplevels.size(); ++i) {
            const auto& tl = toplevels[i];
            if (tl.appId.find(opts.window_selector) != std::string::npos ||
                tl.title.find(opts.window_selector) != std::string::npos) {
              handle = tl.handle;
              found = true;
              break;
            }
          }
        }
      }

      if (!found && wl.has_wlr_foreign_toplevel_manager()) {
        const auto& toplevels = wl.wlr_foreign_toplevels().list();
        char* end = nullptr;
        long idx = std::strtol(opts.window_selector.c_str(), &end, 10);
        if (*end == '\0' && idx >= 0 && static_cast<size_t>(idx) < toplevels.size()) {
          handle = nullptr;
          found = true;
        } else {
          for (size_t i = 0; i < toplevels.size(); ++i) {
            const auto& tl = toplevels[i];
            if (tl.appId.find(opts.window_selector) != std::string::npos ||
                tl.title.find(opts.window_selector) != std::string::npos) {
              handle = nullptr;
              found = true;
              break;
            }
          }
        }
      }

      if (!found) {
        std::cerr << "No window matching '" << opts.window_selector << "' found.\n";
        return 1;
      }

      ok = capture_window_by_handle(wl, handle, out_path);
    }
    break;

  default:
    break;
  }

  if (!ok) {
    std::cerr << "Capture failed.\n";
    if (out_path != opts.output_path) unlink(out_path.c_str());
    return 1;
  }

  wl_display_roundtrip(wl.display());

  bool needs_compose = opts.frame.shadow != 16 || opts.frame.inset != 32 ||
    opts.frame.cornerRadius != 24 || opts.frame.showBorder ||
    opts.frame.includeCursor || opts.frame.hideChrome;

  if (needs_compose) {
    auto img = load_capture(out_path);
    if (!img.valid) {
      std::cerr << "Failed to load captured image for composition.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
    if (!save_composed_png(img, opts.frame, out_path)) {
      std::cerr << "Frame composition failed.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
  }

  if (opts.copy) {
    if (!clipboard.is_available()) {
      std::cerr << "Clipboard unavailable.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
    FILE* f = fopen(out_path.c_str(), "rb");
    if (!f) {
      std::cerr << "Failed to read capture for clipboard.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    std::string png_data(static_cast<std::string::size_type>(fsize), '\0');
    size_t nread = fread(png_data.data(), 1, static_cast<size_t>(fsize), f);
    fclose(f);
    if (static_cast<long>(nread) != fsize) {
      std::cerr << "Failed to read capture for clipboard.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
    if (!clipboard.copy_data("image/png", std::move(png_data))) {
      std::cerr << "Clipboard copy failed.\n";
      if (out_path != opts.output_path) unlink(out_path.c_str());
      return 1;
    }
    if (out_path != opts.output_path) unlink(out_path.c_str());
  }

  if (opts.output_path.empty() && !opts.copy) {
    std::cout << "Saved to " << out_path << "\n";
  }

  clipboard.cleanup();
  wl.disconnect();
  HS_LOG("run_screenshot_cli: exit success");
  return 0;
}

}
