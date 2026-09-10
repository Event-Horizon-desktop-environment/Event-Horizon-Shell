#include "app/file_browser/embed/embed.hpp"
#include "../app.hpp"

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

#include <xkbcommon/xkbcommon.h>

#include "config/shell_config.hpp"
#include "services/udisks2/udisks2_drive_service.hpp"
#include "app/file_browser/features/drag.hpp"
#include "platform/common/asset/asset_loader.hpp"
#include "platform/common/bench/startup_trace.hpp"
#include "platform/common/palette/matugen_palette.hpp"
#include "wayland/core/connection.hpp"
#include "wayland/core/seat.hpp"
#include "wayland/buffer/shm_buffer.hpp"
#include "wayland/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"

namespace eh::file_browser {

// ── globals ──────────────────────────────────────────────────────

static std::unique_ptr<AppState> g_app;

// ── signal handling ──────────────────────────────────────────────

namespace {
volatile sig_atomic_t g_signal{0};
void signal_handler(int) { g_signal = 1; }
}

// ── Wayland listeners ────────────────────────────────────────────

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
    app.buf[0].ensure(app.shm, "eh-fb-a", app.width, app.height);
    app.buf[1].ensure(app.shm, "eh-fb-b", app.width, app.height);
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
  if (h > 0) {
    app.height = h;
  }
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

// ── buffer release hook ──────────────────────────────────────────

static void on_buf_release_hook(void* user) {
  auto& app = *static_cast<AppState*>(user);
  if (!app.surface) return;
  app.pendingRedraw = false;
  draw(app);
}

// ── connect globals ──────────────────────────────────────────────

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
  app.buf[0].ensure(g.shm, "eh-fb-shm-a", app.width, app.height);
  app.buf[1].ensure(g.shm, "eh-fb-shm-b", app.width, app.height);
  app.buf[0].set_release_hook(on_buf_release_hook, &app);
  app.buf[1].set_release_hook(on_buf_release_hook, &app);

  // Bind seat — bind() eagerly creates pointer and keyboard objects
  // so we don't depend on the capabilities event having been received.
  if (app.wl.seat()) {
    app.seat.bind(app.wl.seat());
  }

  return true;
}

// ── create window ────────────────────────────────────────────────

static bool create_window(AppState& app) {
  auto* display = app.wl.display();
  if (!display) return false;

  // Re-bind with a simpler approach: scan registry
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
  wl_display_roundtrip(display); // second roundtrip to get all events

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
  xdg_toplevel_set_title(app.toplevel, "Files");
  xdg_toplevel_set_app_id(app.toplevel, "horizon-files");

  // Initialize SHM buffers
  app.buf[0].ensure(wg.shm, "eh-fb-a", app.width, app.height);
  app.buf[0].set_release_hook(on_buf_release_hook, &app);
  app.buf[1].ensure(wg.shm, "eh-fb-b", app.width, app.height);
  app.buf[1].set_release_hook(on_buf_release_hook, &app);

  wl_surface_commit(app.surface);

  return true;
}

// ── run standalone ───────────────────────────────────────────────

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

  // Bind clipboard service (data-control protocol)
  {
    auto* extMgr = app.wl.ext_data_control_manager();
    auto* wlrMgr = app.wl.wlr_data_control_manager();
    if (extMgr) {
      app.clipboard.bind(extMgr, eh::wayland::ext_data_control_ops(), app.wl.seat(), app.wl.display());
    } else if (wlrMgr) {
      app.clipboard.bind(wlrMgr, eh::wayland::wlr_data_control_ops(), app.wl.seat(), app.wl.display());
    }
  }

  // Initialize data device for drag-and-drop (wl_data_device protocol)
  {
    auto* mgr = app.wl.data_device_manager();
    if (mgr) {
      app.data_device = wl_data_device_manager_get_data_device(mgr, app.wl.seat());
      if (app.data_device) {
        setup_drop_receiver(app);
      }
    }
  }

  if (!create_window(app)) {
    std::cerr << "Failed to create file browser window.\n";
    return 1;
  }

  // Load arrow-icon SVGs at high resolution for crisp rendering at all zoom levels.
  static constexpr int kArrowIconLoadPx = 256;
  app.arrow_left_svg  = eh::shell::asset::load_asset_svg("UI", "arrow-left.svg", kArrowIconLoadPx);
  app.arrow_right_svg = eh::shell::asset::load_asset_svg("UI", "arrow-right.svg", kArrowIconLoadPx);
  app.arrow_up_svg    = eh::shell::asset::load_asset_svg("UI", "arrow-up.svg", kArrowIconLoadPx);
  app.search_svg      = eh::shell::asset::load_asset_svg("UI", "search.svg", kArrowIconLoadPx);
  app.folder_search_svg = eh::shell::asset::load_asset_svg("UI", "folder-search.svg", kArrowIconLoadPx);
  app.mounted_svg     = eh::shell::asset::load_asset_svg("UI", "Mounted.svg", kArrowIconLoadPx);
  app.icon_desktop_svg    = eh::shell::asset::load_asset_svg("UI", "icon-desktop.svg", 64);
  app.icon_documents_svg  = eh::shell::asset::load_asset_svg("UI", "icon-documents.svg", 64);
  app.icon_downloads_svg  = eh::shell::asset::load_asset_svg("UI", "icon-downloads.svg", 64);
  app.icon_music_svg      = eh::shell::asset::load_asset_svg("UI", "icon-music.svg", 64);
  app.icon_pictures_svg   = eh::shell::asset::load_asset_svg("UI", "icon-pictures.svg", 64);
  app.icon_videos_svg     = eh::shell::asset::load_asset_svg("UI", "icon-videos.svg", 64);
  app.icon_publicshare_svg  = eh::shell::asset::load_asset_svg("UI", "icon-publicshare.svg", 64);
  app.icon_templates_svg    = eh::shell::asset::load_asset_svg("UI", "icon-templates.svg", 64);
  if (!app.arrow_left_svg || !app.arrow_right_svg || !app.arrow_up_svg || !app.search_svg || !app.folder_search_svg) {
    std::fprintf(stderr, "[horizon-files] WARNING: arrow/search SVGs not loaded (L=%p R=%p U=%p S=%p FS=%p). "
                         "CWD=%s\n",
                 (void*)app.arrow_left_svg, (void*)app.arrow_right_svg, (void*)app.arrow_up_svg,
                 (void*)app.search_svg, (void*)app.folder_search_svg,
                 []{ static char buf[4096]; return getcwd(buf, sizeof(buf)) ? buf : "?"; }());
  }

  // Initialize icon theme from shell config (e.g., MacTahoe-dark)
  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    if (!sc.dock.iconTheme.empty()) {
      app.icons.set_icon_theme(sc.dock.iconTheme);
      app.last_icon_theme = sc.dock.iconTheme;
    }
    app.icons.prewarm_search_dirs();
  }

  // Colors are set by reload_settings_from_config below

  // Load file browser settings from config
  reload_settings_from_config(app);

  // Start UDisks2 drive service (background D-Bus event loop) — needs
  // to be running before refresh_sidebar() so query_drives() succeeds.
  drives::UDisks2DriveService::instance().start();
  drives::UDisks2DriveService::instance().set_change_callback([&app]() {
    app.sidebar_needs_refresh = true;
    app.pendingRedraw = true;
  });

  // Initialize
  refresh_sidebar(app);
  reload_dir(app);

  // Wire up input event callbacks
  if (app.seat.pointer()) {
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          app.pointerX = x;
          app.pointerY = y;
          handle_pointer_move(app, static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_button_cb(
        [&app](uint32_t button, uint32_t state) {
          if (state == 1) {
            handle_click(app, static_cast<int>(app.pointerX),
                         static_cast<int>(app.pointerY),
                         static_cast<int>(button));
          } else {
            handle_pointer_release(app, static_cast<int>(app.pointerX),
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
  if (app.seat.keyboard()) {
    app.seat.set_keyboard_key_cb(
        [&app](const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
          int len = std::min(ev.utf8_len, static_cast<int>(ev.utf8.size()) - 1);
          handle_key(app, ev.keycode, ev.state, ev.sym, ev.utf8.data(), len);
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

  std::cout << "Event Horizon File Browser started. Current path: "
            << app.cur_tab().current_path << "\n";

  // Initial draw
  draw(app);

  // Event loop
  const int dpy_fd = wl_display_get_fd(app.wl.display());
  constexpr int kPollMs = 200;

  // Track settings file mtimes so we only re-read theme when they change
  int64_t toml_mtime = 0;
  int64_t ini_mtime = 0;
  int64_t dev_disk_mtime = 0;

  while (app.running && g_signal == 0) {
    // ── check for external file changes on every iteration ────────
    if (!app.confirm_open && !app.create_dialog_open &&
        !app.settings_open && !app.open_with_open &&
        !app.term_chooser_open && !app.context_menu_open) {
      bool need_redraw = false;

      // Icon theme sync — stat the settings files and re-read when
      // either changes (the file browser is a separate process so
      // shell_config_snapshot_skip_matugen never sees external updates).
      {
        bool theme_changed = false;
        struct stat st{};

        if (stat(eh::config::state_settings_toml_path().c_str(), &st) == 0) {
          int64_t mt = static_cast<int64_t>(st.st_mtime);
          if (mt != toml_mtime) { toml_mtime = mt; theme_changed = true; }
        }
        if (stat(eh::config::legacy_ini_path().c_str(), &st) == 0) {
          int64_t mt = static_cast<int64_t>(st.st_mtime);
          if (mt != ini_mtime) { ini_mtime = mt; theme_changed = true; }
        }

        if (theme_changed) {
          std::string current_theme = eh::config::read_dock_icon_theme_from_disk();
          if (!current_theme.empty() && current_theme != app.last_icon_theme) {
            app.icons.set_icon_theme(current_theme);
            app.last_icon_theme = current_theme;
            need_redraw = true;
          }
        }
      }

      // Drive list refresh — check /dev/disk mtime as fallback for non-UDisks2
      {
        struct stat ds{};
        if (stat("/dev/disk", &ds) == 0) {
          int64_t mt = static_cast<int64_t>(ds.st_mtime);
          if (mt != dev_disk_mtime) {
            dev_disk_mtime = mt;
            app.sidebar_needs_refresh = true;
            need_redraw = true;
          }
        }
      }

      // Directory content refresh
      {
        struct stat dir_st;
        if (stat(app.cur_tab().current_path.c_str(), &dir_st) == 0) {
          int64_t new_mtime = static_cast<int64_t>(dir_st.st_mtime);
          if (new_mtime != app.cur_tab().dir_mtime) {
            int saved_scroll = app.cur_tab().scroll_px;
            int saved_selected = app.cur_tab().selected_idx;

            reload_dir(app);

            app.cur_tab().scroll_px = saved_scroll;
            app.cur_tab().scroll_smooth_current = static_cast<double>(saved_scroll);
            app.cur_tab().scroll_smooth_target = static_cast<double>(saved_scroll);
            app.cur_tab().selected_idx = saved_selected;

            need_redraw = true;
          }
        }
      }

      if (need_redraw) draw(app);
    }

    // ── deferred sidebar refresh (avoids synchronous D-Bus inside event dispatch) ──
    if (app.sidebar_needs_refresh) {
      app.sidebar_needs_refresh = false;
      refresh_sidebar(app);
      if (!app.mount_navigate_drive_id.empty()) {
        for (auto& loc : app.sidebar_locations) {
          if (loc.kind == SidebarLocation::Kind::Drive &&
              loc.drive_id == app.mount_navigate_drive_id && loc.is_mounted) {
            navigate_to(app, loc.path);
            break;
          }
        }
        app.mount_navigate_drive_id.clear();
      }
      app.pendingRedraw = true;
    }

    // ── poll Wayland display fd ───────────────────────────────────
    struct pollfd pf{};
    pf.fd = dpy_fd;
    pf.events = POLLIN | POLLERR | POLLHUP;

    bool search_pending = ((app.search_active || app.recursive_search_active) && !app.search_query.empty()) || ((app.r_search_active || app.r_recursive_search_active) && !app.r_search_query.empty());
    bool mount_wake = app.mount_poll_wake.exchange(false, std::memory_order_acq_rel);
    int poll_ms = (!app.thumb_pending_queue.empty() || search_pending || app.key_repeat_sym != 0 || mount_wake) ? 0 : kPollMs;
    int pr = poll(&pf, 1, poll_ms);
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

    // ── process pending drive mount results ──────────────────────
    {
      std::string result_id;
      bool ok = false;
      {
        std::lock_guard<std::mutex> lock(app.mount_mtx);
        if (!app.mount_result_drive_id.empty()) {
          result_id = std::move(app.mount_result_drive_id);
          app.mount_result_drive_id.clear();
          ok = app.mount_success;
        }
      }
      if (!result_id.empty()) {
        if (ok) {
          app.mount_navigate_drive_id = result_id;
          app.sidebar_needs_refresh = true;
          app.computer_needs_refresh = true;
        }
        app.pendingRedraw = true;
      }
    }

    // ── process pending drive unmount results ────────────────────
    {
      std::string result_id;
      bool ok = false;
      {
        std::lock_guard<std::mutex> lock(app.mount_mtx);
        if (!app.unmount_result_drive_id.empty()) {
          result_id = std::move(app.unmount_result_drive_id);
          app.unmount_result_drive_id.clear();
          ok = app.unmount_success;
        }
      }
      if (!result_id.empty()) {
        if (ok) {
          // If we're viewing a directory on the unmounted drive, go home
          for (const auto& loc : app.sidebar_locations) {
            if (loc.kind == SidebarLocation::Kind::Drive &&
                loc.drive_id == result_id && loc.path != "/" &&
                loc.path.size() > 1) {
              if (app.cur_tab().current_path.size() >= loc.path.size() &&
                  app.cur_tab().current_path.compare(0, loc.path.size(), loc.path) == 0 &&
                  (app.cur_tab().current_path.size() == loc.path.size() ||
                   app.cur_tab().current_path[loc.path.size()] == '/')) {
                navigate_to(app, home_dir());
              }
              break;
            }
          }
          app.sidebar_needs_refresh = true;
          app.computer_needs_refresh = true;
        }
        app.pendingRedraw = true;
      }
    }

    // ── process pending thumbnail decodes ─────────────────────────
    if (process_pending_thumbnails(app)) {
      app.pendingRedraw = true;
    }

    // ── hover preview timer check ─────────────────────────────────
    check_hover_preview(app);

    // ── application-level key repeat ───────────────────────────────
    if (app.key_repeat_sym != 0) {
      auto now = std::chrono::steady_clock::now();
      uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
      uint64_t elapsed = now_ms - app.key_repeat_start_ms;
      constexpr uint64_t kRepeatDelay = 0;
      constexpr uint64_t kRepeatRate = 100;
      if (elapsed >= kRepeatDelay) {
        uint64_t delta = now_ms - app.key_repeat_last_ms;
        if (delta >= kRepeatRate) {
          int sym = app.key_repeat_sym;
          if (app.rename_ui_open) {
            if (sym == XKB_KEY_BackSpace && !app.rename_ui_buf.empty()) {
              if (app.rename_ui_cursor_pos > 0) {
                app.rename_ui_buf.erase(app.rename_ui_cursor_pos - 1, 1);
                --app.rename_ui_cursor_pos;
              }
              app.pendingRedraw = true;
            } else if (sym == XKB_KEY_Delete && !app.rename_ui_buf.empty()) {
              if (app.rename_ui_cursor_pos < static_cast<int>(app.rename_ui_buf.size()))
                app.rename_ui_buf.erase(app.rename_ui_cursor_pos, 1);
              else
                app.rename_ui_buf.pop_back();
              if (app.rename_ui_cursor_pos > static_cast<int>(app.rename_ui_buf.size()))
                app.rename_ui_cursor_pos = static_cast<int>(app.rename_ui_buf.size());
              app.pendingRedraw = true;
            } else if (sym == XKB_KEY_Left && app.rename_ui_cursor_pos > 0) {
              --app.rename_ui_cursor_pos;
              app.pendingRedraw = true;
            } else if (sym == XKB_KEY_Right && app.rename_ui_cursor_pos < static_cast<int>(app.rename_ui_buf.size())) {
              ++app.rename_ui_cursor_pos;
              app.pendingRedraw = true;
            }
          }
          if (app.pendingRedraw)
            app.key_repeat_last_ms = now_ms;
        }
      }
    }

    if (app.pendingRedraw) {
      app.pendingRedraw = false;
      if (app.surface) draw(app);
    }
  }

  // Cleanup (destroy shm-buffers BEFORE disconnecting the display)
  for (auto& b : app.buf) b.destroy();
  app.previewPopupBuf.destroy();
  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.clipboard.cleanup();
  app.seat.unbind();
  app.wl.disconnect();

  g_app = nullptr;
  return 0;
}

// ── run select directory ─────────────────────────────────────────

[[nodiscard]] int run_select_directory(std::string& out_path) {
  g_app = std::make_unique<AppState>();
  AppState& app = *g_app;
  app.select_dir_mode = true;

  if (!app.wl.connect()) {
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  if (!connect_globals(app)) {
    std::cerr << "Failed to connect Wayland globals.\n";
    return 1;
  }

  if (!create_window(app)) {
    std::cerr << "Failed to create file browser window.\n";
    return 1;
  }

  // Set title for picker mode
  if (app.toplevel)
    xdg_toplevel_set_title(app.toplevel, "Select Directory");

  // Load arrow-icon SVGs
  static constexpr int kArrowIconLoadPx = 256;
  app.arrow_left_svg  = eh::shell::asset::load_asset_svg("UI", "arrow-left.svg", kArrowIconLoadPx);
  app.arrow_right_svg = eh::shell::asset::load_asset_svg("UI", "arrow-right.svg", kArrowIconLoadPx);
  app.arrow_up_svg    = eh::shell::asset::load_asset_svg("UI", "arrow-up.svg", kArrowIconLoadPx);
  app.search_svg      = eh::shell::asset::load_asset_svg("UI", "search.svg", kArrowIconLoadPx);
  app.folder_search_svg = eh::shell::asset::load_asset_svg("UI", "folder-search.svg", kArrowIconLoadPx);
  app.mounted_svg     = eh::shell::asset::load_asset_svg("UI", "Mounted.svg", kArrowIconLoadPx);
  app.icon_desktop_svg    = eh::shell::asset::load_asset_svg("UI", "icon-desktop.svg", 64);
  app.icon_documents_svg  = eh::shell::asset::load_asset_svg("UI", "icon-documents.svg", 64);
  app.icon_downloads_svg  = eh::shell::asset::load_asset_svg("UI", "icon-downloads.svg", 64);
  app.icon_music_svg      = eh::shell::asset::load_asset_svg("UI", "icon-music.svg", 64);
  app.icon_pictures_svg   = eh::shell::asset::load_asset_svg("UI", "icon-pictures.svg", 64);
  app.icon_videos_svg     = eh::shell::asset::load_asset_svg("UI", "icon-videos.svg", 64);
  app.icon_publicshare_svg  = eh::shell::asset::load_asset_svg("UI", "icon-publicshare.svg", 64);
  app.icon_templates_svg    = eh::shell::asset::load_asset_svg("UI", "icon-templates.svg", 64);
  if (!app.arrow_left_svg || !app.arrow_right_svg || !app.arrow_up_svg || !app.search_svg || !app.folder_search_svg) {
    std::fprintf(stderr, "[horizon-files] WARNING: arrow/search SVGs not loaded (L=%p R=%p U=%p S=%p FS=%p). "
                         "CWD=%s\n",
                 (void*)app.arrow_left_svg, (void*)app.arrow_right_svg, (void*)app.arrow_up_svg,
                 (void*)app.search_svg, (void*)app.folder_search_svg,
                 []{ static char buf[4096]; return getcwd(buf, sizeof(buf)) ? buf : "?"; }());
  }

  // Initialize icon theme
  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    if (!sc.dock.iconTheme.empty()) {
      app.icons.set_icon_theme(sc.dock.iconTheme);
      app.last_icon_theme = sc.dock.iconTheme;
    }
    app.icons.prewarm_search_dirs();
  }

  reload_settings_from_config(app);

  // Initialize
  refresh_sidebar(app);
  reload_dir(app);

  // Wire up input event callbacks
  if (app.seat.pointer()) {
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          app.pointerX = x;
          app.pointerY = y;
          handle_pointer_move(app, static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_button_cb(
        [&app](uint32_t button, uint32_t state) {
          if (state == 1) {
            handle_click(app, static_cast<int>(app.pointerX),
                         static_cast<int>(app.pointerY),
                         static_cast<int>(button));
          } else {
            handle_pointer_release(app, static_cast<int>(app.pointerX),
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
  if (app.seat.keyboard()) {
    app.seat.set_keyboard_key_cb(
        [&app](const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
          int len = std::min(ev.utf8_len, static_cast<int>(ev.utf8.size()) - 1);
          handle_key(app, ev.keycode, ev.state, ev.sym, ev.utf8.data(), len);
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
  constexpr int kPollMs = 200;

  while (app.running && g_signal == 0) {
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
      if (app.surface) draw(app);
    }
  }

  // Collect result
  if (!app.select_dir_result.empty()) {
    out_path = std::move(app.select_dir_result);
  }

  // Cleanup (destroy shm-buffers BEFORE disconnecting the display)
  for (auto& b : app.buf) b.destroy();
  app.previewPopupBuf.destroy();
  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.clipboard.cleanup();
  app.seat.unbind();
  app.wl.disconnect();

  g_app = nullptr;
  return out_path.empty() ? 1 : 0;
}

[[nodiscard]] int run_select_file(std::string& out_path) {
  g_app = std::make_unique<AppState>();
  AppState& app = *g_app;
  app.select_file_mode = true;

  if (!app.wl.connect()) {
    std::cerr << "WAYLAND_DISPLAY not set or compositor unavailable.\n";
    return 1;
  }

  if (!connect_globals(app)) {
    std::cerr << "Failed to connect Wayland globals.\n";
    return 1;
  }

  if (!create_window(app)) {
    std::cerr << "Failed to create file browser window.\n";
    return 1;
  }

  // Set title for picker mode
  if (app.toplevel)
    xdg_toplevel_set_title(app.toplevel, "Select File");

  // Load arrow-icon SVGs
  static constexpr int kArrowIconLoadPx = 256;
  app.arrow_left_svg  = eh::shell::asset::load_asset_svg("UI", "arrow-left.svg", kArrowIconLoadPx);
  app.arrow_right_svg = eh::shell::asset::load_asset_svg("UI", "arrow-right.svg", kArrowIconLoadPx);
  app.arrow_up_svg    = eh::shell::asset::load_asset_svg("UI", "arrow-up.svg", kArrowIconLoadPx);
  app.search_svg      = eh::shell::asset::load_asset_svg("UI", "search.svg", kArrowIconLoadPx);
  app.folder_search_svg = eh::shell::asset::load_asset_svg("UI", "folder-search.svg", kArrowIconLoadPx);
  app.mounted_svg     = eh::shell::asset::load_asset_svg("UI", "Mounted.svg", kArrowIconLoadPx);
  app.icon_desktop_svg    = eh::shell::asset::load_asset_svg("UI", "icon-desktop.svg", 64);
  app.icon_documents_svg  = eh::shell::asset::load_asset_svg("UI", "icon-documents.svg", 64);
  app.icon_downloads_svg  = eh::shell::asset::load_asset_svg("UI", "icon-downloads.svg", 64);
  app.icon_music_svg      = eh::shell::asset::load_asset_svg("UI", "icon-music.svg", 64);
  app.icon_pictures_svg   = eh::shell::asset::load_asset_svg("UI", "icon-pictures.svg", 64);
  app.icon_videos_svg     = eh::shell::asset::load_asset_svg("UI", "icon-videos.svg", 64);
  app.icon_publicshare_svg  = eh::shell::asset::load_asset_svg("UI", "icon-publicshare.svg", 64);
  app.icon_templates_svg    = eh::shell::asset::load_asset_svg("UI", "icon-templates.svg", 64);
  if (!app.arrow_left_svg || !app.arrow_right_svg || !app.arrow_up_svg || !app.search_svg || !app.folder_search_svg) {
    std::fprintf(stderr, "[horizon-files] WARNING: arrow/search SVGs not loaded (L=%p R=%p U=%p S=%p FS=%p). "
                         "CWD=%s\n",
                 (void*)app.arrow_left_svg, (void*)app.arrow_right_svg, (void*)app.arrow_up_svg,
                 (void*)app.search_svg, (void*)app.folder_search_svg,
                 []{ static char buf[4096]; return getcwd(buf, sizeof(buf)) ? buf : "?"; }());
  }

  // Initialize icon theme
  {
    const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
    if (!sc.dock.iconTheme.empty()) {
      app.icons.set_icon_theme(sc.dock.iconTheme);
      app.last_icon_theme = sc.dock.iconTheme;
    }
    app.icons.prewarm_search_dirs();
  }

  reload_settings_from_config(app);

  // Initialize
  refresh_sidebar(app);
  reload_dir(app);

  // Wire up input event callbacks
  if (app.seat.pointer()) {
    app.seat.set_pointer_motion_cb(
        [&app](wl_surface*, double x, double y) {
          app.pointerX = x;
          app.pointerY = y;
          handle_pointer_move(app, static_cast<int>(x), static_cast<int>(y));
        });
    app.seat.set_pointer_button_cb(
        [&app](uint32_t button, uint32_t state) {
          if (state == 1) {
            handle_click(app, static_cast<int>(app.pointerX),
                         static_cast<int>(app.pointerY),
                         static_cast<int>(button));
          } else {
            handle_pointer_release(app, static_cast<int>(app.pointerX),
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
  if (app.seat.keyboard()) {
    app.seat.set_keyboard_key_cb(
        [&app](const eh::wayland::WaylandSeat::KeyboardEvent& ev) {
          int len = std::min(ev.utf8_len, static_cast<int>(ev.utf8.size()) - 1);
          handle_key(app, ev.keycode, ev.state, ev.sym, ev.utf8.data(), len);
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
  constexpr int kPollMs = 200;

  while (app.running && g_signal == 0) {
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
      if (app.surface) draw(app);
    }
  }

  // Collect result
  if (!app.select_dir_result.empty()) {
    out_path = std::move(app.select_dir_result);
  }

  // Cleanup (destroy shm-buffers BEFORE disconnecting the display)
  for (auto& b : app.buf) b.destroy();
  app.previewPopupBuf.destroy();
  if (app.toplevel) xdg_toplevel_destroy(app.toplevel);
  if (app.xdgSurface) xdg_surface_destroy(app.xdgSurface);
  if (app.surface) wl_surface_destroy(app.surface);
  app.clipboard.cleanup();
  app.seat.unbind();
  app.wl.disconnect();

  g_app = nullptr;
  return out_path.empty() ? 1 : 0;
}

} // namespace eh::file_browser
