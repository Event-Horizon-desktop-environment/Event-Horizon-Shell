#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/taskbar/paint/taskbar_paint.hpp"
#include "desktop_shell/taskbar/Features/taskbar_pinned.h"
#include "desktop_shell/taskbar/Features/taskbar_pin_drag.h"

#include "desktop_shell/common/log/debug_log.hpp"

#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"

#include "desktop_shell/common/bench/bench_file.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/taskbar/layout/taskbar_position.hpp"
#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"
#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"
#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "desktop_shell/controlcenter/paint/control_center_popup_paint.hpp"
#include "services/mpris/mpris_player.hpp"
#include "services/tray/manager/tray_manager.hpp"
#include "services/tray/dbus/tray_context_menu.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "wl/core/protocols.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "wl/surface/vulkan_wayland.hpp"
#include "desktop_shell/power_confirm/power_confirm.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/layout/control_center_slider_metrics.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <cmath>
#include <numbers>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

using eh::shell::dock::control_center::ControlCenterActiveModal;

namespace eh::shell::taskbar {

// Forward declarations.

static void taskbar_popup_draw(TaskbarApp& app);

static bool eh_taskbar_fast_start() {
  static const bool v = [] {
    const char* s = std::getenv("EH_TASKBAR_FAST_START");
    if (!s) return true;
    return std::strcmp(s, "0") != 0 && std::strcmp(s, "false") != 0 && std::strcmp(s, "no") != 0;
  }();
  return v;
}

std::vector<TaskbarWidgetHit> g_widgetHits;

namespace {

static bool point_in(double px, double py, double x, double y, double w, double h) {
  return px >= x && py >= y && px < (x + w) && py < (y + h);
}

static bool taskbar_layer_surface(const TaskbarApp& app, wl_surface* s) {
  if (!s) return false;
  for (const auto& up : app.layers)
    if (up && up->surface == s) return true;
  return false;
}

static bool taskbar_popup_surface(const TaskbarApp& app, wl_surface* s) {
  return s && s == app.popupSurface;
}

static void path_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double radius) {
  const double r = std::min({radius, w * 0.5, h * 0.5});
  const double p = std::numbers::pi;
  cairo_new_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -p * 0.5, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, p * 0.5);
  cairo_arc(cr, x + r, y + h - r, r, p * 0.5, p);
  cairo_arc(cr, x + r, y + r, r, p, p * 1.5);
  cairo_close_path(cr);
}

static std::string strip_field_codes(std::string exec) {
  std::string out;
  out.reserve(exec.size());
  for (size_t i = 0; i < exec.size(); i++) {
    if (exec[i] == '%' && i + 1 < exec.size()) {
      const char c = exec[i + 1];
      if (c == '%') out.push_back('%');
      i++;
      continue;
    }
    out.push_back(exec[i]);
  }
  // Trim trailing whitespace
  while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
  return out;
}

static void launch_exec_command(const std::string& execLine) {
  if (execLine.empty()) return;
  const std::string cmd = strip_field_codes(execLine);
  if (cmd.empty()) return;

  const pid_t intermediate = ::fork();
  if (intermediate < 0) return;
  if (intermediate > 0) {
    ::waitpid(intermediate, nullptr, 0);
    return;
  }

  if (::setsid() < 0) ::_exit(1);

  const pid_t worker = ::fork();
  if (worker < 0) ::_exit(1);
  if (worker > 0) ::_exit(0);

  const int devnull = ::open("/dev/null", O_RDWR);
  if (devnull >= 0) {
    ::dup2(devnull, STDIN_FILENO);
    ::dup2(devnull, STDOUT_FILENO);
    ::dup2(devnull, STDERR_FILENO);
    ::close(devnull);
  }

  std::vector<char> arg_lc(cmd.begin(), cmd.end());
  arg_lc.push_back('\0');

  // Launch through the per-user systemd manager whenever possible so the app's
  // process materializes in the user-writable (delegated) cgroup subtree —
  // user@1000.service/app.slice. VramBoostManager needs that to protect the
  // app's VRAM via dmem.min without running the shell as root. `--scope` keeps
  // the caller's full environment (games rely on it). As root the DE bypasses
  // this (root can write any cgroup already).
  if (::geteuid() != 0) {
    const std::string unit = "eh-app-" + std::to_string(static_cast<long>(::getpid()));
    std::vector<std::string> sdr = {"systemd-run", "--user", "--scope", "--collect",
                                    "--quiet", "--slice=app.slice", "--unit=" + unit,
                                    "--", "/bin/sh", "-lc", cmd};
    std::vector<char*> argvS;
    argvS.reserve(sdr.size() + 1);
    for (auto& a : sdr) argvS.push_back(a.data());
    argvS.push_back(nullptr);
    ::execvp("systemd-run", argvS.data());
  }

  char argv0[] = "sh";
  char argv1[] = "-lc";
  char* argv[] = {argv0, argv1, arg_lc.data(), nullptr};

  ::execvp("/bin/sh", argv);
  ::_exit(127);
}

static std::string ipc_socket_path() {
  if (const char* p = std::getenv("EH_IPC_SOCKET"))
    return std::string(p);
  if (const char* dir = std::getenv("XDG_RUNTIME_DIR"))
    return std::string(dir) + "/event-horizon-ipc.sock";
  return "/tmp/event-horizon-ipc.sock";
}

static void ipc_send_command(const std::string& command) {
  const std::string path = ipc_socket_path();
  struct sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const size_t pathLen = std::min(path.size(), sizeof(addr.sun_path) - 1);
  std::memcpy(addr.sun_path, path.data(), pathLen);
  addr.sun_path[pathLen] = '\0';

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return;
  if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return;
  }
  const std::string cmd = command + "\n";
  ::write(fd, cmd.data(), cmd.size());
  ::close(fd);
}

// Popup layer-surface listener.

struct TaskbarPopupCtx {
  TaskbarApp* taskbar = nullptr;
};

static void taskbar_popup_configure(void* data, zwlr_layer_surface_v1* layer,
                                     uint32_t serial, uint32_t width, uint32_t height) {
  auto* ctx = static_cast<TaskbarPopupCtx*>(data);
  if (!ctx || !ctx->taskbar) return;
  TaskbarApp& app = *ctx->taskbar;
  if (layer != app.popupLayer) return;

  zwlr_layer_surface_v1_ack_configure(layer, serial);
  app.popupPendingSerial = 0;
  if (width > 0) app.popupConfiguredW = static_cast<int>(width);
  if (height > 0) app.popupConfiguredH = static_cast<int>(height);
  app.popupConfigured = app.popupConfiguredW > 0 && app.popupConfiguredH > 0;

  taskbar_popup_draw(app);
}

static void taskbar_popup_closed(void* data, zwlr_layer_surface_v1*) {
  auto* ctx = static_cast<TaskbarPopupCtx*>(data);
  if (!ctx || !ctx->taskbar) return;
  auto& app = *ctx->taskbar;
  if (app.popupFrameCb) {
    wl_callback_destroy(app.popupFrameCb);
    app.popupFrameCb = nullptr;
  }
  if (app.popupLayer) zwlr_layer_surface_v1_destroy(app.popupLayer);
  if (app.popupSurface) wl_surface_destroy(app.popupSurface);
  app.popupLayer = nullptr;
  app.popupSurface = nullptr;
  delete ctx;
}

static const zwlr_layer_surface_v1_listener g_taskbar_popup_listener = {
  .configure = taskbar_popup_configure,
  .closed = taskbar_popup_closed,
};

// Popup frame callback, driving the power-confirm countdown.

static void taskbar_popup_frame_done(void* data, wl_callback* cb, uint32_t);

static const wl_callback_listener g_taskbar_popup_frame_listener = {
  .done = taskbar_popup_frame_done,
};

static void taskbar_popup_frame_done(void* data, wl_callback* cb, uint32_t /*compositor_time_ms*/) {
  auto& app = *static_cast<TaskbarApp*>(data);
  wl_callback_destroy(cb);
  app.popupFrameCb = nullptr;
  if (!app.popupSurface || !app.popupLayer) return;
  if (app.popupKind != TaskbarPopupKind::PowerConfirm) return;
  if (!app.appDrawerPowerConfirmOpen) return;
  const bool motionDirty = app.popupMotionDirty;
  app.popupMotionDirty = false;
  const uint64_t now = eh::shell::monotonic_ms();
  if (motionDirty || now - app.popupLastDrawMs >= 250) {
    taskbar_popup_draw(app);
  } else {
    if (!app.popupFrameCb) {
      app.popupFrameCb = wl_surface_frame(app.popupSurface);
      wl_callback_add_listener(app.popupFrameCb, &g_taskbar_popup_frame_listener, &app);
    }
    wl_surface_commit(app.popupSurface);
  }
}

// Output-layer listener.

static void taskbar_layer_configure(void* data, zwlr_layer_surface_v1* layer,
                                     uint32_t serial, uint32_t width, uint32_t height) {
  auto* L = static_cast<TaskbarOutputLayer*>(data);
  if (!L || !L->taskbar) return;
  TaskbarApp& app = *L->taskbar;
  if (layer != L->layer) return;

  zwlr_layer_surface_v1_ack_configure(layer, serial);
  L->pendingSerial = 0;
  if (width > 0) L->configuredWidth = static_cast<int>(width);
  if (height > 0) L->configuredHeight = static_cast<int>(height);
  L->configured = L->configuredWidth > 0 && L->configuredHeight > 0;
  L->everConfigured = true;

  taskbar_draw(app);
}

static void taskbar_layer_closed(void* data, zwlr_layer_surface_v1*) {
  auto* L = static_cast<TaskbarOutputLayer*>(data);
  if (!L || !L->taskbar) return;
  L->layer = nullptr;
  L->surface = nullptr;
  L->configured = false;
  L->everConfigured = false;
}

static const zwlr_layer_surface_v1_listener g_taskbar_layer_listener = {
  .configure = taskbar_layer_configure,
  .closed = taskbar_layer_closed,
};

// Popup open / close / draw.

static void taskbar_popup_close(TaskbarApp& app) {
  if (app.popupSurface) {
    if (app.popupFrameCb) {
      wl_callback_destroy(app.popupFrameCb);
      app.popupFrameCb = nullptr;
    }
    app.popupBuf.destroy();
    if (app.popupLayer) zwlr_layer_surface_v1_destroy(app.popupLayer);
    wl_surface_destroy(app.popupSurface);
    app.popupSurface = nullptr;
    app.popupLayer = nullptr;
    app.popupConfiguredW = 0;
    app.popupConfiguredH = 0;
    app.popupConfigured = false;
    app.popupPendingSerial = 0;
    if (app.display) (void)wl_display_roundtrip(app.display);
  }
}

static bool taskbar_popup_create(TaskbarApp& app, int anchorX, int popupW, int popupH) {
  taskbar_popup_close(app);

  if (!app.compositor || !app.layerShell) return false;

  const auto pos = eh::shell::taskbar::taskbar_compute_popup_position(app, anchorX, popupW, popupH);
  if (!pos.wl_out) return false;

  eh::wayland::LayerSurfaceConfig cfg{};
  if (app.popupKind == TaskbarPopupKind::PowerConfirm) {
    cfg.nameSpace = eh::shell::kPopupNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    cfg.width = 0;
    cfg.height = 0;
    cfg.exclusiveZone = -1;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = 0;
    cfg.marginLeft = 0;
    cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
  } else {
    cfg.nameSpace = (app.popupKind == TaskbarPopupKind::AppDrawer)
        ? eh::shell::kAppDrawerNamespace
        : eh::shell::kPopupNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    cfg.width = static_cast<uint32_t>(std::max(1, popupW));
    cfg.height = static_cast<uint32_t>(std::max(1, popupH));
    cfg.exclusiveZone = -1;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = 0;
    cfg.marginLeft = pos.margin_left;
    if (app.settings.positionTop) {
      cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
      cfg.marginTop = pos.clearance;
    } else {
      cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
      cfg.marginBottom = pos.margin_bottom;
    }
    cfg.keyboard = (app.popupKind == TaskbarPopupKind::AppDrawer)
        ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND
        : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
  }

  auto ctx = std::make_unique<TaskbarPopupCtx>();
  ctx->taskbar = &app;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, pos.wl_out, cfg,
                                          &g_taskbar_popup_listener, ctx.get(),
                                          &surf, &layer)) {
    return false;
  }
  ctx.release();

  app.popupSurface = surf;
  app.popupLayer = layer;
  app.popupConfiguredW = 0;
  app.popupConfiguredH = 0;
  app.popupConfigured = false;
  app.popupPendingSerial = 0;
  app.popupW = popupW;
  app.popupH = popupH;

  wl_surface_commit(app.popupSurface);
  if (app.display) wl_display_roundtrip(app.display);
  return true;
}

static void draw_context_menu(TaskbarApp& app, cairo_t* cr, int w, int h) {
  cairo_set_source_rgba(cr, 0.12, 0.12, 0.13, 0.96);
  path_rounded_rect(cr, 0, 0, static_cast<double>(w), static_cast<double>(h), 12.0);
  cairo_fill(cr);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13.0);
  double yy = 4.0;
  for (const auto& it : app.popupItems) {
    if (it.id < 0) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, 10.0, yy + 13.0);
      cairo_line_to(cr, static_cast<double>(w) - 10.0, yy + 13.0);
      cairo_stroke(cr);
      yy += 13.0;
      continue;
    }
    cairo_set_source_rgba(cr, 1, 1, 1, it.enabled ? 0.87 : 0.35);
    cairo_move_to(cr, 12.0, yy + 17.0);
    cairo_show_text(cr, it.label.c_str());
    yy += 26.0;
  }
}

}

void taskbar_popup_draw(TaskbarApp& app) {
  if (!app.popupSurface || !app.popupLayer) return;
  if (!app.popupConfigured) return;

  const int w = app.popupConfiguredW;
  const int h = app.popupConfiguredH;
  if (w <= 0 || h <= 0) return;

  if (!app.popupBuf.ensure(app.shm, eh::shell::kPopupNamespace, w, h)) return;

  const auto& sc = eh::config::shell_config_snapshot();

  cairo_t* cr = app.popupBuf.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  switch (app.popupKind) {
    case TaskbarPopupKind::Calendar:
      eh::widgets::popup::calendar::calendar_popup_paint(app, cr, sc);
      break;
    case TaskbarPopupKind::Weather:
      eh::widgets::popup::weather::weather_popup_paint(app, cr, sc);
      break;
    case TaskbarPopupKind::VolumeMixer:
      eh::widgets::popup::volume_mixer::volume_mixer_popup_paint(app, cr, sc);
      break;
    case TaskbarPopupKind::MediaPlayer:
      eh::widgets::popup::media_player::media_player_popup_paint(app, cr, sc);
      break;
    case TaskbarPopupKind::Vpn:
      eh::widgets::popup::vpn::vpn_popup_paint(app, cr, sc);
      break;
    case TaskbarPopupKind::Battery:
      eh::widgets::dock_battery_popup_paint(app.pointerX, app.pointerY, cr, sc);
      break;
    case TaskbarPopupKind::Bluetooth:
      eh::widgets::dock_bluetooth_popup_paint(app.pointerX, app.pointerY, cr, sc);
      break;
    case TaskbarPopupKind::ControlCenter: {
      const std::string wid = app.ccWidgetId.empty() ? std::string("control_center") : app.ccWidgetId;
      control_center_popup_paint(app.ccState, cr, w, h, sc, app.mpris.get(), wid, app.icons, app.settings.pinnedApps);
      break;
    }
    case TaskbarPopupKind::AppDrawer: {
      const eh::config::ChromePaintColors mc2 = eh::config::derived_chrome_colors(sc.appearance);
      eh::appdrawer::AppDrawerChromeColors adc{};
      adc.dockFillR = mc2.dockFillR;
      adc.dockFillG = mc2.dockFillG;
      adc.dockFillB = mc2.dockFillB;
      adc.drawerDimR = mc2.drawerDimR;
      adc.drawerDimG = mc2.drawerDimG;
      adc.drawerDimB = mc2.drawerDimB;
      adc.accentR = mc2.accentR;
      adc.accentG = mc2.accentG;
      adc.accentB = mc2.accentB;
      adc.outlineR = mc2.outlineR;
      adc.outlineG = mc2.outlineG;
      adc.outlineB = mc2.outlineB;
      const float popupAlpha = eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::AppDrawer);
      eh::appdrawer::app_drawer_paint(app.appDrawerState, cr, app.icons,
                                       true, true, popupAlpha, &adc);
      if (app.appDrawerPowerConfirmOpen && app.appDrawerPowerConfirmIdx >= 1 &&
          app.appDrawerPowerConfirmIdx <= 3) {
        eh::appdrawer::paint_power_confirm_modal(
            cr, w, h, app.appDrawerPowerConfirmIdx, app.pointerX, app.pointerY, mc2);
      }
      break;
    }
    case TaskbarPopupKind::PowerConfirm: {
      const uint64_t now = eh::shell::monotonic_ms();
      const uint64_t elapsed = (app.appDrawerPowerConfirmStartMs != 0) ? (now - app.appDrawerPowerConfirmStartMs) : 0;
      const int remainingSec = (elapsed >= 60000) ? 0 : static_cast<int>(60 - elapsed / 1000);
      if (app.appDrawerPowerConfirmOpen && app.appDrawerPowerConfirmIdx >= 1 &&
          app.appDrawerPowerConfirmIdx <= 3) {
        if (remainingSec <= 0) {
          const int idx = app.appDrawerPowerConfirmIdx;
          app.appDrawerPowerConfirmOpen = false;
          app.appDrawerPowerConfirmIdx = -1;
          taskbar_popup_close(app);
          eh::appdrawer::app_drawer_power_exec(app.compositorKind, idx);
          wl_display_flush(app.display);
          cairo_restore(cr);
          return;
        }
        eh::power_confirm::paint(cr, static_cast<double>(w), static_cast<double>(h),
                                 app.appDrawerPowerConfirmIdx,
                                 app.pointerX, app.pointerY,
                                 remainingSec, sc);
      }
      break;
    }
    case TaskbarPopupKind::Tray:
    case TaskbarPopupKind::App:
      draw_context_menu(app, cr, w, h);
      break;
    default:
      break;
  }

  cairo_restore(cr);
  cairo_surface_flush(app.popupBuf.cairo_surface());

  wl_surface_attach(app.popupSurface, app.popupBuf.wl(), 0, 0);
  wl_surface_damage_buffer(app.popupSurface, 0, 0, w, h);
  app.popupBuf.mark_busy();
  app.popupLastDrawMs = eh::shell::monotonic_ms();
  if (app.popupKind == TaskbarPopupKind::PowerConfirm && app.appDrawerPowerConfirmOpen &&
      !app.popupFrameCb) {
    app.popupFrameCb = wl_surface_frame(app.popupSurface);
    wl_callback_add_listener(app.popupFrameCb, &g_taskbar_popup_frame_listener, &app);
  }
  wl_surface_commit(app.popupSurface);
  wl_display_flush(app.display);
}

// Tooltip helpers.
void taskbar_tooltip_destroy(TaskbarApp& app) {
  app.tooltipShm.set_release_hook(nullptr, nullptr);
  app.tooltipShm.destroy();
  if (app.tooltipLayer) { zwlr_layer_surface_v1_destroy(app.tooltipLayer); app.tooltipLayer = nullptr; }
  if (app.tooltipSurface) { wl_surface_destroy(app.tooltipSurface); app.tooltipSurface = nullptr; }
  app.tooltipConfigured = false;
  app.tooltipCfgW = 0;
  app.tooltipCfgH = 0;
  app.tooltipText.clear();
  app.tooltipShownSlot = -1;
}

namespace {

// Settings persistence.

static void taskbar_save_settings(TaskbarApp& app) {
  debug_log("taskbar", "pin_drag: taskbar_save_settings pinnedApps=%zu", app.settings.pinnedApps.size());
  eh::config::ShellConfig sc = eh::config::shell_config_snapshot_skip_matugen();
  sc.taskbar = app.settings;
  debug_log("taskbar", "pin_drag: taskbar_save_settings calling write_state_settings_toml");
  (void)eh::config::write_state_settings_toml(sc);
  debug_log("taskbar", "pin_drag: taskbar_save_settings calling shell_config_apply_from_memory");
  eh::config::shell_config_apply_from_memory(std::move(sc));
}

// Popup click dispatch.

static void taskbar_cc_click_handler(TaskbarApp& app) {
  const double px = app.pointerX;
  const double py = app.pointerY;
  auto& s = app.ccState;

  auto in_rect = [&](double rx, double ry, double rw, double rh) -> bool {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
  };

  // Grid card clicks.
  if (s.activeModal == ControlCenterActiveModal::None) {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double colW = cc_layout_grid_col_w(W);
    const double gridY = cc_layout_grid_y();
    const double tileH = cc_layout_grid_tile_h();
    const double gap = cc_layout_row_gap();
    const double row2Y = gridY + tileH + gap;

    // Row 0, col 0: Network
    if (in_rect(pad, gridY, colW, tileH)) {
      s.activeModal = ControlCenterActiveModal::Network;
      (void)eh::shell::dock_slot_hooks::control_center_wifi_scan(true);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    // Row 0, col 1: Bluetooth
    if (in_rect(pad + colW + gap, gridY, colW, tileH)) {
      s.activeModal = ControlCenterActiveModal::Bluetooth;
      eh::shell::dock_slot_hooks::bluetooth_ensure_service();
      eh::shell::dock_slot_hooks::bluetooth_start_discovery();
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    // Row 1, col 0: Audio Output
    if (in_rect(pad, row2Y, colW, tileH)) {
      s.activeModal = ControlCenterActiveModal::AudioOutput;
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    // Row 1, col 1: Audio Input
    if (in_rect(pad + colW + gap, row2Y, colW, tileH)) {
      s.activeModal = ControlCenterActiveModal::AudioInput;
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  }

  // Modal backdrop / close dismiss.
  if (s.activeModal != ControlCenterActiveModal::None) {
    // Close button
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double btnSize = 28.0;
    const double closeX = W - pad - btnSize;
    const double closeY = cc_layout_grid_y() + 4.0;
    if (in_rect(closeX, closeY, btnSize, btnSize)) {
      s.activeModal = ControlCenterActiveModal::None;
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    // Backdrop: click anywhere else in modal content area
    {
      const double my = cc_layout_grid_y();
      const double mh = app.popupH - my - pad;
      const double mx = pad;
      const double mw = W - pad * 2.0;
      if (in_rect(mx, my, mw, mh)) {
        s.activeModal = ControlCenterActiveModal::None;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
  }

  // Modal row selections.
  const uint64_t nowMs = eh::shell::now_mono_ms();

  if (s.activeModal == ControlCenterActiveModal::Network) {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    constexpr int kMaxRows = 8;
    const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
    const int rows = std::min(kMaxRows, static_cast<int>(aps.size()));
    const double rowH = 34.0;
    const double headerH = 32.0;
    const double baseY = cc_layout_grid_y() + cc_layout_row_gap() + headerH;
    for (int i = 0; i < rows; ++i) {
      const double ry = baseY + static_cast<double>(i) * rowH;
      if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
        if (aps[static_cast<size_t>(i)].needs_password) {
          s.wifiPendingSsid = aps[static_cast<size_t>(i)].ssid;
          s.wifiPasswordPrompt = true;
          s.wifiPassword.clear();
          s.wifiLastError.clear();
          taskbar_popup_draw(app);
        } else {
          std::string err;
          if (eh::shell::dock_slot_hooks::control_center_wifi_connect(
                  aps[static_cast<size_t>(i)].ssid, "", &err)) {
            s.wifiPasswordPrompt = false;
            s.wifiPendingSsid.clear();
            s.activeModal = ControlCenterActiveModal::None;
            taskbar_popup_draw(app);
          } else {
            s.wifiLastError = err;
            taskbar_popup_draw(app);
          }
        }
        wl_display_flush(app.display);
        return;
      }
    }
  }

  if (s.activeModal == ControlCenterActiveModal::Bluetooth) {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    constexpr int kMaxRows = 8;
    const auto devs = eh::shell::dock_slot_hooks::bluetooth_devices();
    const int rows = std::min(kMaxRows, static_cast<int>(devs.size()));
    const double rowH = 34.0;
    const double headerH = 32.0;
    const double baseY = cc_layout_grid_y() + cc_layout_row_gap() + headerH;
    const double rightEdge = W - pad;
    for (int i = 0; i < rows; ++i) {
      const double ry = baseY + static_cast<double>(i) * rowH;
      if (!in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) continue;
      const auto& d = devs[static_cast<size_t>(i)];
      const double actionBtnW = d.connected ? 74.0 : (d.paired ? 60.0 : 40.0);
      const bool showForget = d.paired;
      const double forgetBtnW = 48.0;
      const double btnGap = 6.0;
      const double btnAreaW = actionBtnW + (showForget ? (btnGap + forgetBtnW) : 0.0);
      const double btnStartX = rightEdge - btnAreaW - 8.0;
      const double forgetX = btnStartX + actionBtnW + btnGap;
      if (px >= forgetX && px < forgetX + forgetBtnW) {
        if (d.paired)
          eh::shell::dock_slot_hooks::bluetooth_forget_device(d.path);
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
      if (d.connected)
        eh::shell::dock_slot_hooks::bluetooth_disconnect_device(d.path);
      else if (d.paired)
        eh::shell::dock_slot_hooks::bluetooth_connect_device(d.path);
      else
        eh::shell::dock_slot_hooks::bluetooth_pair_device(d.path);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  }

  if (s.activeModal == ControlCenterActiveModal::AudioOutput) {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    constexpr int kMaxRows = 12;
    const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
    const int rows = std::min(kMaxRows, static_cast<int>(devs.size()));
    const double rowH = 34.0;
    const double headerH = 32.0;
    const double baseY = cc_layout_grid_y() + cc_layout_row_gap() + headerH;
    for (int i = 0; i < rows; ++i) {
      const double ry = baseY + static_cast<double>(i) * rowH;
      if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
        eh::shell::dock_slot_hooks::control_center_set_default_sink(
            devs[static_cast<size_t>(i)].sink_name);
        s.outputDevicesPendingSink = devs[static_cast<size_t>(i)].sink_name;
        s.outputDevicesIgnoreUntilMs = nowMs + 500;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
  }

  if (s.activeModal == ControlCenterActiveModal::AudioInput) {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    constexpr int kMaxRows = 12;
    const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
    const int rows = std::min(kMaxRows, static_cast<int>(devs.size()));
    const double rowH = 34.0;
    const double headerH = 32.0;
    const double baseY = cc_layout_grid_y() + cc_layout_row_gap() + headerH;
    for (int i = 0; i < rows; ++i) {
      const double ry = baseY + static_cast<double>(i) * rowH;
      if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
        eh::shell::dock_slot_hooks::control_center_set_default_source(
            devs[static_cast<size_t>(i)].source_name);
        s.inputDevicesPendingSource = devs[static_cast<size_t>(i)].source_name;
        s.inputDevicesIgnoreUntilMs = nowMs + 500;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
  }

  // Audio controls (always available).
  {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double audioCardW = cc_layout_audio_card_w(W);
    const double audioY = cc_layout_audio_card_y();

    // Output audio card
    {
      const double cardX = pad;
      const double cardY = audioY;
      const double tx = cardX + eh::shell::cc_slider::kAudioTrackXPad;
      const double ty = cardY + eh::shell::cc_slider::kAudioTrackYFromCardTop;
      const double tw = std::max(1.0, audioCardW - 2.0 * eh::shell::cc_slider::kAudioTrackXPad);
      const double sx = tx - eh::shell::cc_slider::kAudioHitPadH;
      const double sy = ty - eh::shell::cc_slider::kAudioHitPadV;
      const double sw = tw + 2.0 * eh::shell::cc_slider::kAudioHitPadH;
      const double sh = eh::shell::cc_slider::kAudioTrackH + 2.0 * eh::shell::cc_slider::kAudioHitPadV;

      if (in_rect(sx, sy, sw, sh)) {
        const double tVal = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
        const int pct = std::clamp(static_cast<int>(std::lround(tVal * 100.0)), 0, 150);
        s.audioDragActive = true;
        s.audioDragVisualT = tVal;
        s.audioDragUiPct = pct;
        s.audioLastAppliedPct = pct;
        s.audioLastApplyMs = nowMs;
        s.audioIgnoreStateUntilMs = nowMs + 180;
        s.audioLastLoggedPct = pct;
        eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(tVal);
        s.inputDragActive = false;
        s.mixerDragActive = false;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
      const double muteX = cardX + 24.0 - 14.0;
      const double muteY = cardY + 28.0 - 14.0;
      if (in_rect(muteX, muteY, 28.0, 28.0)) {
        const auto state = eh::shell::dock_slot_hooks::control_center_audio_output_state();
        eh::shell::dock_slot_hooks::control_center_set_audio_output_mute(!state.muted);
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }

    // Input audio card
    {
      const double cardX = pad + audioCardW + pad;
      const double cardY = audioY;
      const double tx = cardX + eh::shell::cc_slider::kAudioTrackXPad;
      const double ty = cardY + eh::shell::cc_slider::kAudioTrackYFromCardTop;
      const double tw = std::max(1.0, audioCardW - 2.0 * eh::shell::cc_slider::kAudioTrackXPad);
      const double sx = tx - eh::shell::cc_slider::kAudioHitPadH;
      const double sy = ty - eh::shell::cc_slider::kAudioHitPadV;
      const double sw = tw + 2.0 * eh::shell::cc_slider::kAudioHitPadH;
      const double sh = eh::shell::cc_slider::kAudioTrackH + 2.0 * eh::shell::cc_slider::kAudioHitPadV;

      if (in_rect(sx, sy, sw, sh)) {
        const double tVal = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
        const int pct = std::clamp(static_cast<int>(std::lround(tVal * 100.0)), 0, 150);
        s.inputDragActive = true;
        s.inputDragVisualT = tVal;
        s.inputDragUiPct = pct;
        s.inputLastAppliedPct = pct;
        s.inputLastApplyMs = nowMs;
        s.inputIgnoreStateUntilMs = nowMs + 180;
        s.inputLastLoggedPct = pct;
        eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(tVal);
        s.audioDragActive = false;
        s.mixerDragActive = false;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
      const double muteX = cardX + 24.0 - 14.0;
      const double muteY = cardY + 28.0 - 14.0;
      if (in_rect(muteX, muteY, 28.0, 28.0)) {
        const auto state = eh::shell::dock_slot_hooks::control_center_audio_input_state();
        eh::shell::dock_slot_hooks::control_center_set_audio_input_mute(!state.muted);
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
  }

  // Mixer sliders.
  {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double mixerY = cc_layout_mixer_y();
    const double ry0 = mixerY + eh::shell::cc_slider::kMixerHeaderH;
    const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();

    auto mixer_slider_hit_test = [&](double ry, double& out_t) -> bool {
      const double tx = pad + 10.0;
      const double ty = ry + eh::shell::cc_slider::kMixerRowTrackYOffset
                      + (10.0 - eh::shell::cc_slider::kMixerTrackH) * 0.5;
      const double tw = W - pad * 2.0 - 20.0;
      const double hx = tx - eh::shell::cc_slider::kMixerHitPadH;
      const double hy = ty - eh::shell::cc_slider::kMixerHitPadV;
      const double hw = tw + 2.0 * eh::shell::cc_slider::kMixerHitPadH;
      const double hh = eh::shell::cc_slider::kMixerTrackH + 2.0 * eh::shell::cc_slider::kMixerHitPadV;
      if (!in_rect(hx, hy, hw, hh)) return false;
      out_t = std::clamp((px - tx) / std::max(1.0, tw), 0.0, 1.0);
      return true;
    };

    // Show up to 2 mixer rows (collapsed view)
    const int maxRows = 2;
    const int rows = std::min(static_cast<int>(streams.size()), maxRows);
    for (int i = 0; i < rows; ++i) {
      double t = 0.0;
      if (mixer_slider_hit_test(ry0 + static_cast<double>(i) * 40.0, t)) {
        s.mixerDragActive = true;
        s.mixerDragStreamId = streams[static_cast<size_t>(i)].sink_input_id;
        s.mixerDragIsInput = false;
        const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
        s.mixerDragUiPct = pct;
        s.mixerDragVisualT = t;
        s.mixerDragSx = pad + 10.0;
        s.mixerDragSw = W - pad * 2.0 - 20.0;
        s.mixerLastAppliedPct = pct;
        s.mixerLastApplyMs = nowMs;
        s.mixerIgnoreStateUntilMs = nowMs + 180;
        s.mixerLastLoggedPct = pct;
        eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(
            streams[static_cast<size_t>(i)].sink_input_id, t);
        s.audioDragActive = false;
        s.inputDragActive = false;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
  }

  // Media buttons.
  {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double cardY = cc_layout_media_y();
    constexpr double cardH = 92.0;
    const double btnR = 14.0;
    const double cy = cardY + cardH - 24.0;
    const double cx2 = pad + (W - pad * 2.0) - 22.0;
    const double cx1 = cx2 - (btnR * 2.0 + 10.0);
    const double cx0 = cx1 - (btnR * 2.0 + 10.0);
    auto hit_circle = [&](double cx, double cy0) -> bool {
      const double dx = px - cx;
      const double dy = py - cy0;
      return (dx * dx + dy * dy) <= (btnR * btnR);
    };
    if (hit_circle(cx0, cy) || hit_circle(cx1, cy) || hit_circle(cx2, cy)) {
      if (!app.mpris) return;
      if (hit_circle(cx0, cy)) app.mpris->previous();
      else if (hit_circle(cx1, cy)) app.mpris->play_pause();
      else app.mpris->next();
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  }

  // Weather card.
  {
    const double pad = cc_layout_pad();
    const double W = static_cast<double>(app.popupW);
    const double mediaY = cc_layout_media_y();
    constexpr double kCardH = 92.0;
    const double weatherY = mediaY + kCardH + cc_layout_row_gap();
    if (in_rect(pad, weatherY, W - pad * 2.0, kCardH)) {
      // Weather card click — handled
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  }
}

// Forward declarations
static const eh::wayland::ForeignToplevels::Toplevel* toplevel_by_serial(
    const eh::wayland::ForeignToplevels& tl, std::uint64_t serial);

static void taskbar_handle_popup_click(TaskbarApp& app, uint32_t serial) {
  (void)serial;
  const double py = app.pointerY;

  switch (app.popupKind) {
    case TaskbarPopupKind::Calendar:
    case TaskbarPopupKind::Weather:
    case TaskbarPopupKind::VolumeMixer:
    case TaskbarPopupKind::MediaPlayer:
    case TaskbarPopupKind::Vpn: {
      const double py = app.pointerY, px = app.pointerX;
      constexpr double kVpnW = 320;
      constexpr double kVpnKPad = 16.0;
      constexpr double kVpnKHeaderH = 48.0;
      constexpr double kVpnKRowH = 44.0;
      constexpr double kVpnKBtnW = 84.0;
      constexpr double kVpnKBtnH = 26.0;
      constexpr double kVpnKRemoveSz = 22.0;
      const int vpnCount = eh::shell::dock::popup::vpn::vpn_popup_entry_count();
      const double H = kVpnKPad + kVpnKHeaderH + static_cast<double>(std::max(vpnCount, 1)) * kVpnKRowH + kVpnKPad;
      if (px < 0 || px >= kVpnW || py < 0 || py >= H) { taskbar_popup_close(app); return; }
      const double cbX = kVpnW - kVpnKPad - 24.0, cbY = kVpnKPad;
      if (px >= cbX && px < cbX + 24.0 && py >= cbY && py < cbY + 24.0) { taskbar_popup_close(app); return; }
      for (int vi = 0; vi < vpnCount; ++vi) {
        const double rowY = kVpnKPad + kVpnKHeaderH + static_cast<double>(vi) * kVpnKRowH;
        const double rowMidY = rowY + kVpnKRowH * 0.5;
        const double remX = kVpnW - kVpnKPad - kVpnKRemoveSz;
        const double remY = rowMidY - kVpnKRemoveSz * 0.5;
        const double btnX = remX - 6.0 - kVpnKBtnW;
        const double btnY = rowMidY - kVpnKBtnH * 0.5;
        if (px >= remX && px < remX + kVpnKRemoveSz && py >= remY && py < remY + kVpnKRemoveSz) {
          eh::shell::dock::popup::vpn::vpn_popup_remove_entry(vi);
          taskbar_popup_draw(app);
          return;
        }
        if (px >= btnX && px < btnX + kVpnKBtnW && py >= btnY && py < btnY + kVpnKBtnH) {
          eh::shell::dock::popup::vpn::vpn_popup_toggle_entry(vi);
          taskbar_popup_draw(app);
          return;
        }
      }
      taskbar_popup_close(app);
      return;
    }
    case TaskbarPopupKind::Battery:
      taskbar_popup_close(app);
      return;
    case TaskbarPopupKind::Bluetooth:
      taskbar_popup_close(app);
      return;
    case TaskbarPopupKind::ControlCenter:
      taskbar_cc_click_handler(app);
      return;
    case TaskbarPopupKind::Tray: {
      double y = 4.0;
      for (const auto& it : app.popupItems) {
        if (it.id < 0) { y += 13.0; continue; }
        if (py >= y && py < (y + 26.0)) {
          if (!it.enabled) return;
          try {
            auto menu = sdbus::createProxy(*app.trayBus,
                sdbus::ServiceName{app.popupService},
                sdbus::ObjectPath{app.popupMenuPath});
            menu->callMethod("Event")
                .onInterface("com.canonical.dbusmenu")
                .withArguments(int32_t{it.id}, std::string("clicked"),
                               sdbus::Variant(int32_t{0}), uint32_t{0});
          } catch (const std::exception& e) {
            eh::shell_log::dbus_tray("menu event failed: ", e.what());
          }
          taskbar_popup_close(app);
          return;
        }
        y += 26.0;
      }
      return;
    }
    case TaskbarPopupKind::App: {
      double y = 4.0;
      for (const auto& it : app.popupItems) {
        if (it.id < 0) { y += 13.0; continue; }
        if (py >= y && py < (y + 26.0)) {
          if (!it.enabled) return;
          const int32_t action = it.id;
          if (action >= 1000 && action < 2000) {
            const size_t idx = static_cast<size_t>(action - 1000);
            if (idx < app.popupAppDesktopActions.size()) {
              const auto& exec = app.popupAppDesktopActions[idx].exec;
              if (!exec.empty()) {
                launch_exec_command(exec);
              }
            }
          } else if (action >= 2000 && action < 3000) {
            const size_t idx = static_cast<size_t>(action - 2000);
            if (idx < app.popupAppWindows.size() && app.toplevels) {
              const uint64_t s = app.popupAppWindows[idx].first;
              const auto* tl = toplevel_by_serial(*app.toplevels, s);
              if (tl && tl->handle) { zwlr_foreign_toplevel_handle_v1_activate(tl->handle, app.seat); wl_display_flush(app.display); }
            }
          } else if (action == 3000) {
            if (!app.popupAppDesktopExec.empty()) {
              launch_exec_command(app.popupAppDesktopExec);
            }
          } else if (action == 9001) {
            const std::string normKey = eh::shell::paths::normalize_desktop_app_id(app.popupAppKey);
            if (normKey == "unknown" || normKey == eh::shell::kSettingsAppId) break;
            auto& pins = app.settings.pinnedApps;
            bool wasPinned = false;
            for (auto it = pins.begin(); it != pins.end(); ++it) {
              if (pin_identity_pin_raw_matches_key(*it, normKey)) {
                pins.erase(it);
                wasPinned = true;
                break;
              }
            }
            if (!wasPinned) {
              std::string store;
              if (auto desktop = find_desktop_file_for_appid(normKey)) {
                const std::string& p = *desktop;
                const auto slash = p.rfind('/');
                const std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
                if (base.size() > 8 && base.ends_with(".desktop")) store = base.substr(0, base.size() - 8);
                else if (!base.empty()) store = base;
              }
              if (store.empty()) {
                store = app.popupAppKey;
                if (app.toplevels) {
                  for (const auto& tl : *app.toplevels) {
                    if (!tl.handle || tl.closed) continue;
                    if (pin_identity_pin_raw_matches_key(tl.appId, normKey)) {
                      store = tl.appId;
                      break;
                    }
                  }
                }
              }
              pins.push_back(store);
            }
            taskbar_save_settings(app);
          } else if (action == 9002) {
            if (app.toplevels) {
              const auto* tl = toplevel_by_serial(*app.toplevels, app.popupAppChosenSerial);
              if (tl && tl->handle) { zwlr_foreign_toplevel_handle_v1_set_minimized(tl->handle); wl_display_flush(app.display); }
            }
          } else if (action == 9003) {
            if (app.toplevels) {
              const auto* tl = toplevel_by_serial(*app.toplevels, app.popupAppChosenSerial);
              if (tl && tl->handle) { zwlr_foreign_toplevel_handle_v1_close(tl->handle); wl_display_flush(app.display); }
            }
          } else if (action == 9004) {
            if (app.toplevels) {
              for (const auto& tl : *app.toplevels) {
                if (!tl.handle || tl.closed) continue;
                if (eh::shell::paths::normalize_desktop_app_id(tl.appId) == app.popupAppKey)
                  zwlr_foreign_toplevel_handle_v1_close(tl.handle);
              }
            }
            wl_display_flush(app.display);
          } else if (action == 8001) {
            launch_exec_command("xdg-open trash:///");
          } else if (action == 8002) {
            const char* home = std::getenv("HOME");
            if (home) {
              const std::string trashPath = std::string(home) + "/.local/share/Trash";
              launch_exec_command("gio trash --empty \"" + trashPath + "\"");
            }
            app.trashFull = false;
          }
          taskbar_popup_close(app);
          return;
        }
        y += 26.0;
      }
      return;
    }
    case TaskbarPopupKind::AppDrawer: {
      auto& s = app.appDrawerState;
      const double px = app.pointerX;
      const double py = app.pointerY;

      if (app.appDrawerPowerConfirmOpen) {
        using P = eh::appdrawer::PowerConfirmPick;
        const P pick = eh::appdrawer::pick_power_confirm_modal(
            static_cast<double>(app.popupW), static_cast<double>(app.popupH),
            app.appDrawerPowerConfirmIdx, px, py);
        if (pick == P::Confirm) {
          const int idx = app.appDrawerPowerConfirmIdx;
          app.appDrawerPowerConfirmOpen = false;
          app.appDrawerPowerConfirmIdx = -1;
          taskbar_popup_close(app);
          eh::appdrawer::app_drawer_power_exec(app.compositorKind, idx);
        } else if (pick == P::Cancel || pick == P::CloseX) {
          app.appDrawerPowerConfirmOpen = false;
          taskbar_popup_draw(app);
        }
        wl_display_flush(app.display);
        return;
      }

      s.pointerX = px;
      s.pointerY = py;

      const auto zone = eh::appdrawer::app_drawer_hit_zone(s, px, py);

      if (zone == eh::appdrawer::AppDrawerHitZone::SearchField) {
        s.searchFieldFocused = true;
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }

      if (zone == eh::appdrawer::AppDrawerHitZone::CategoryTab) {
        const int cat = eh::appdrawer::app_drawer_pick_category_tab(s, px, py);
        s.selectedCategory = cat;
        eh::appdrawer::app_drawer_refresh_hits(s);
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }

      if (zone == eh::appdrawer::AppDrawerHitZone::AppListRow) {
        s.searchFieldFocused = false;
        const int row = eh::appdrawer::app_drawer_pick_row_index(s, px, py);
        if (row >= 0 && row < static_cast<int>(s.hits.size())) {
          const auto& hit = s.hits[static_cast<size_t>(row)];
          if (!hit.exec.empty()) {
            launch_exec_command(hit.exec);
            taskbar_popup_close(app);
          }
        }
        wl_display_flush(app.display);
        return;
      }

      if (zone == eh::appdrawer::AppDrawerHitZone::PinnedApp) {
        const int pinIdx = eh::appdrawer::app_drawer_pick_pinned_index(s, px, py);
        if (pinIdx >= 0 && pinIdx < static_cast<int>(s.pinnedApps.size())) {
          const std::string& rawId = s.pinnedApps[static_cast<size_t>(pinIdx)];
          const std::string normId = eh::shell::paths::normalize_desktop_app_id(rawId);
          if (auto desktop = find_desktop_file_for_appid(normId)) {
            if (auto info = read_desktop_entry_info(*desktop)) {
              launch_exec_command(info->exec);
              taskbar_popup_close(app);
            }
          }
        }
        wl_display_flush(app.display);
        return;
      }

      if (zone == eh::appdrawer::AppDrawerHitZone::PowerButton) {
        const int pwr = eh::appdrawer::app_drawer_pick_power_index(s, px, py);
        if (pwr == 0) {
          taskbar_popup_close(app);
          eh::appdrawer::app_drawer_power_exec(app.compositorKind, pwr);
        } else if (pwr >= 1 && pwr <= 3) {
          taskbar_popup_close(app);
          app.popupKind = TaskbarPopupKind::PowerConfirm;
          app.popupW = 1;
          app.popupH = 1;
          app.popupConfiguredW = 0;
          app.popupConfiguredH = 0;
          app.popupConfigured = false;
          if (!taskbar_popup_create(app, 0, 1, 1)) {
            app.popupKind = TaskbarPopupKind::None;
          } else {
            app.appDrawerPowerConfirmOpen = true;
            app.appDrawerPowerConfirmIdx = pwr;
            app.appDrawerPowerConfirmStartMs = eh::shell::monotonic_ms();
          }
        }
        wl_display_flush(app.display);
        return;
      }

      if (zone == eh::appdrawer::AppDrawerHitZone::NightlightButton) {
        if (app.gammaService_) {
          const bool next = !app.gammaService_->enabled();
          app.gammaService_->set_enabled(next);
          if (next) app.gammaService_->set_temperature(4000);
          eh::appdrawer::set_nightlight_active(next);
        } else {
          // Split-out child (horizon-taskbar, §0.11): no gamma control — flip
          // the paint state locally and publish the bus command so the
          // supervisor toggles the display.
          eh::appdrawer::set_nightlight_active(!eh::appdrawer::get_nightlight_active());
          taskbar_request_nightlight_toggle();
        }
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }

      // Click outside any hit zone → close
      taskbar_popup_close(app);
      wl_display_flush(app.display);
      return;
    }
    case TaskbarPopupKind::PowerConfirm: {
      const double px = app.pointerX;
      const double py = app.pointerY;
      if (app.appDrawerPowerConfirmOpen && app.appDrawerPowerConfirmIdx >= 1 &&
          app.appDrawerPowerConfirmIdx <= 3) {
        using P = eh::power_confirm::Pick;
        const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
        const double us = dock_ui_scale(sc.dock);
        const double pw = app.popupConfiguredW > 0 ? static_cast<double>(app.popupConfiguredW)
                                                   : static_cast<double>(app.popupW);
        const double ph = app.popupConfiguredH > 0 ? static_cast<double>(app.popupConfiguredH)
                                                   : static_cast<double>(app.popupH);
        const P pick = eh::power_confirm::pick(
            pw, ph,
            app.appDrawerPowerConfirmIdx, px, py, us);
        if (pick == P::Confirm) {
          const int idx = app.appDrawerPowerConfirmIdx;
          app.appDrawerPowerConfirmOpen = false;
          app.appDrawerPowerConfirmIdx = -1;
          taskbar_popup_close(app);
          eh::appdrawer::app_drawer_power_exec(app.compositorKind, idx);
        } else if (pick == P::Cancel || pick == P::Close) {
          app.appDrawerPowerConfirmOpen = false;
          app.appDrawerPowerConfirmIdx = -1;
          taskbar_popup_close(app);
        }
      } else {
        taskbar_popup_close(app);
      }
      wl_display_flush(app.display);
      return;
    }
    default:
      break;
  }
}

// Helper: toplevel lookup.

static const eh::wayland::ForeignToplevels::Toplevel* toplevel_by_serial(
    const eh::wayland::ForeignToplevels& tl, std::uint64_t serial) {
  if (serial == 0) return nullptr;
  auto it = std::find_if(tl.begin(), tl.end(), [serial](const auto& t) {
    return t.serial == serial && t.handle && !t.closed;
  });
  return it != tl.end() ? &*it : nullptr;
}

// Tray menu helpers.

static void show_tray_popup_inline(TaskbarApp& app, const TaskbarTrayItem& ti,
                                    int anchorX, uint32_t serial) {
  (void)serial;
  const std::string menuPath = ti.proxy ? eh::shell::dock::tray_menu::dock_get_menu_object_path(*ti.proxy) : std::string{};
  if (menuPath.empty()) return;
  auto build_items = [&]() -> std::vector<TaskbarPopupItem> {
    std::vector<TaskbarPopupItem> out;
    try {
      auto menu = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti.service},
                                      sdbus::ObjectPath{menuPath});
      using Props = std::map<std::string, sdbus::Variant>;
      using Layout = sdbus::Struct<int32_t, Props, std::vector<sdbus::Variant>>;
      uint32_t revision = 0;
      Layout root{};
      menu->callMethod("GetLayout")
          .onInterface("com.canonical.dbusmenu")
          .withArguments(int32_t{0}, int32_t{1},
                         std::vector<std::string>{"label", "enabled", "visible", "type"})
          .storeResultsTo(revision, root);
      (void)revision;
      for (const auto& vChild : std::get<2>(root)) {
        Layout child = vChild.get<Layout>();
        const int32_t id = std::get<0>(child);
        const auto& props = std::get<1>(child);
        auto getStr = [&](const char* k) -> std::string {
          auto it = props.find(k);
          if (it == props.end()) return {};
          try { return it->second.template get<std::string>(); } catch (const sdbus::Error&) { return {}; }
        };
        auto getBool = [&](const char* k, bool def) -> bool {
          auto it = props.find(k);
          if (it == props.end()) return def;
          try {
            if (it->second.containsValueOfType<bool>()) return it->second.template get<bool>();
            if (it->second.containsValueOfType<int32_t>()) return it->second.template get<int32_t>() != 0;
            return def;
          } catch (const sdbus::Error&) { return def; }
        };
        if (getStr("type") == "separator") {
          out.push_back({.id = -1, .label = "", .enabled = false}); continue;
        }
        if (!getBool("visible", true)) continue;
        std::string label = getStr("label");
        label.erase(std::remove(label.begin(), label.end(), '_'), label.end());
        out.push_back({.id = id, .label = label.empty() ? "(unnamed)" : label,
                       .enabled = getBool("enabled", true)});
      }
    } catch (const std::exception& e) {
      eh::shell_log::dbus_tray("dbusmenu GetLayout failed: ", e.what());
    }
    return out;
  };

  auto items = build_items();
  if (items.empty()) return;

  int maxw = 0;
  {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    for (const auto& it : items) {
      if (it.id < 0) continue;
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, it.label.c_str(), &ex);
      maxw = std::max(maxw, static_cast<int>(ex.x_advance));
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
  }

  int itemCount = 0, sepCount = 0;
  for (const auto& it : items) { if (it.id < 0) ++sepCount; else ++itemCount; }
  const int popupH = 4 + 26 * itemCount + 13 * sepCount + 4;
  const int popupW = std::clamp(12 + maxw + 10, 160, 360);

  app.popupKind = TaskbarPopupKind::Tray;
  app.popupItems = std::move(items);
  app.popupService = ti.service;
  app.popupPath = ti.path;
  app.popupMenuPath = menuPath;

  taskbar_popup_close(app);
  if (app.display) (void)wl_display_roundtrip(app.display);
  taskbar_popup_create(app, anchorX, popupW, popupH);
}

// App context menu.

static void show_app_menu(TaskbarApp& app, const std::string& normWid,
                           uint64_t chosenSerial, bool isPinned,
                           int anchorX, uint32_t serial) {
  (void)serial;
  app.popupAppKey = normWid;
  app.popupAppChosenSerial = chosenSerial;

  std::string lookupId = normWid;
  if (app.toplevels && app.popupAppChosenSerial != 0) {
    for (const auto& tl : *app.toplevels) {
      if (tl.serial == app.popupAppChosenSerial && tl.handle && !tl.closed) {
        lookupId = tl.appId;
        break;
      }
    }
  }
  app.popupAppDesktopExec.clear();
  app.popupAppDesktopActions.clear();
  if (!lookupId.empty() && lookupId != kSlotKeySettings) {
    if (auto desktop = find_desktop_file_for_appid(lookupId)) {
      if (auto info = read_desktop_entry_info(*desktop)) {
        app.popupAppDesktopExec = info->exec;
        app.popupAppDesktopActions = info->actions;
      }
    }
  }

  app.popupAppWindows.clear();
  if (app.toplevels) {
    for (const auto& tl : *app.toplevels) {
      if (!tl.handle || tl.closed) continue;
      const std::string key = eh::shell::paths::normalize_desktop_app_id(tl.appId);
        if (key == normWid || key == lookupId) {
          app.popupAppWindows.emplace_back(tl.serial, tl.title);
        }
    }
  }

  std::vector<TaskbarPopupItem> items;
  for (size_t i = 0; i < app.popupAppDesktopActions.size(); i++) {
    const auto& a = app.popupAppDesktopActions[i];
    items.push_back({.id = static_cast<int32_t>(1000 + i),
                     .label = a.name.empty() ? a.id : a.name,
                     .enabled = !a.exec.empty()});
  }
  if (!items.empty()) items.push_back({.id = -1, .label = "", .enabled = false});

  items.push_back({.id = 9001, .label = isPinned ? "Unpin" : "Pin",
                   .enabled = (normWid != "unknown" && normWid != eh::shell::kSettingsAppId)});
  items.push_back({.id = -1, .label = "", .enabled = false});

  if (app.popupAppChosenSerial != 0) {
    items.push_back({.id = 9002, .label = "Minimize window", .enabled = true});
    items.push_back({.id = 9003, .label = "Close window", .enabled = true});
    items.push_back({.id = 9004, .label = "Close all windows", .enabled = true});
    items.push_back({.id = -1, .label = "", .enabled = false});
  }

  for (size_t i = 0; i < app.popupAppWindows.size(); i++) {
    const auto& w = app.popupAppWindows[i];
    items.push_back({.id = static_cast<int32_t>(2000 + i),
                     .label = w.second.empty() ? "(untitled)" : w.second,
                     .enabled = (w.first != 0)});
  }
  if (!app.popupAppWindows.empty()) items.push_back({.id = -1, .label = "", .enabled = false});

  if (!app.popupAppDesktopExec.empty()) {
    items.push_back({.id = 3000, .label = "New Window", .enabled = true});
    items.push_back({.id = -1, .label = "", .enabled = false});
  }

  int popupW = 200, popupH = 200;
  {
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
    cairo_t* cr = cairo_create(surf);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13.0);
    double maxw = 0;
    for (const auto& it : items) {
      if (it.id < 0) continue;
      cairo_text_extents_t ex{};
      cairo_text_extents(cr, it.label.c_str(), &ex);
      maxw = std::max(maxw, ex.x_advance);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    int itemCount = 0, sepCount = 0;
    for (const auto& it : items) { if (it.id < 0) ++sepCount; else ++itemCount; }
    popupW = std::clamp(static_cast<int>(12 + maxw + 10), 160, 360);
    popupH = 4 + 26 * itemCount + 13 * sepCount + 4;
  }

  app.popupKind = TaskbarPopupKind::App;
  app.popupItems = std::move(items);
  taskbar_popup_close(app);
  taskbar_popup_create(app, anchorX, popupW, popupH);
}

// Keyboard listener.

static void taskbar_keyboard_keymap(void* data, wl_keyboard*, uint32_t format,
                                     int32_t fd, uint32_t size) {
  auto& app = *static_cast<TaskbarApp*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
  char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;
  if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
  if (app.xkbState) xkb_state_unref(app.xkbState);
  app.xkbKeymap = xkb_keymap_new_from_buffer(app.xkbCtx, map_str, size - 1,
                                              XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  app.xkbState = app.xkbKeymap ? xkb_state_new(app.xkbKeymap) : nullptr;
}

static void taskbar_keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
static void taskbar_keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

static void taskbar_keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t,
                                  uint32_t keycode, uint32_t state) {
  auto& app = *static_cast<TaskbarApp*>(data);
  if (!app.xkbState) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;
  if (!app.popupSurface) return;

  if (app.popupKind == TaskbarPopupKind::PowerConfirm) {
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, keycode + 8);
    if (sym == XKB_KEY_Escape) {
      app.appDrawerPowerConfirmOpen = false;
      app.appDrawerPowerConfirmIdx = -1;
      taskbar_popup_close(app);
      wl_display_flush(app.display);
      return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      const int idx = app.appDrawerPowerConfirmIdx;
      app.appDrawerPowerConfirmOpen = false;
      app.appDrawerPowerConfirmIdx = -1;
      taskbar_popup_close(app);
      eh::appdrawer::app_drawer_power_exec(app.compositorKind, idx);
      wl_display_flush(app.display);
      return;
    }
    return;
  }

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, keycode + 8);

  if (sym == XKB_KEY_Escape) {
    taskbar_popup_close(app);
    wl_display_flush(app.display);
    return;
  }

  if (app.popupKind != TaskbarPopupKind::AppDrawer) return;

  auto& s = app.appDrawerState;

  if (s.searchFieldFocused) {
    if (sym == XKB_KEY_Down) {
      s.searchFieldFocused = false;
      if (!s.hits.empty() && s.sel < 0) s.sel = 0;
      eh::appdrawer::app_drawer_ensure_sel_visible(s);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    if (sym == XKB_KEY_BackSpace) {
      eh::shell::str::utf8_pop_back(s.query);
      eh::appdrawer::app_drawer_refresh_hits(s);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    char utf8[128]{};
    const int n = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8, sizeof(utf8) - 1);
    if (n > 0) {
      s.query.append(utf8, static_cast<size_t>(n));
      eh::appdrawer::app_drawer_refresh_hits(s);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  } else {
    if (sym == XKB_KEY_Up) {
      if (!s.hits.empty()) {
        if (s.sel <= 0) s.sel = static_cast<int>(s.hits.size()) - 1;
        else s.sel--;
        eh::appdrawer::app_drawer_ensure_sel_visible(s);
      }
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    if (sym == XKB_KEY_Down) {
      if (!s.hits.empty()) {
        if (s.sel < 0) s.sel = 0;
        else if (s.sel >= static_cast<int>(s.hits.size()) - 1) s.sel = 0;
        else s.sel++;
        eh::appdrawer::app_drawer_ensure_sel_visible(s);
      }
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      if (s.sel >= 0 && s.sel < static_cast<int>(s.hits.size())) {
        const auto& hit = s.hits[static_cast<size_t>(s.sel)];
        if (!hit.exec.empty()) {
          launch_exec_command(hit.exec);
          taskbar_popup_close(app);
        }
      }
      wl_display_flush(app.display);
      return;
    }
    // Re-focus search field on any text input while not focused
    char utf8[128]{};
    const int n = xkb_state_key_get_utf8(app.xkbState, keycode + 8, utf8, sizeof(utf8) - 1);
    if (n > 0) {
      s.searchFieldFocused = true;
      s.query.append(utf8, static_cast<size_t>(n));
      eh::appdrawer::app_drawer_refresh_hits(s);
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }
  }
}

static void taskbar_keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                        uint32_t latched, uint32_t locked, uint32_t group) {
  auto& app = *static_cast<TaskbarApp*>(data);
  if (!app.xkbState) return;
  const auto gl = static_cast<xkb_layout_index_t>(group);
  xkb_state_update_mask(app.xkbState, depressed, latched, locked, gl, gl, gl);
}

static void taskbar_keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

static const wl_keyboard_listener kTaskbarKeyboardListener = {
  .keymap = taskbar_keyboard_keymap,
  .enter = taskbar_keyboard_enter,
  .leave = taskbar_keyboard_leave,
  .key = taskbar_keyboard_key,
  .modifiers = taskbar_keyboard_modifiers,
  .repeat_info = taskbar_keyboard_repeat_info,
};

// Pointer listener.

static void taskbar_pointer_enter(void* data, wl_pointer* p, uint32_t serial,
                                   wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
  auto& app = *static_cast<TaskbarApp*>(data);
  (void)p; (void)serial;
  app.pointerSurface = surface;
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  for (size_t i = 0; i < app.layers.size(); ++i) {
    if (app.layers[i] && app.layers[i]->surface == surface) {
      app.pointerTaskbarLayerIdx = i; break;
    }
  }
  // Auto-hide reveal
  if (app.settings.autoHide && !app.reveal) {
    app.reveal = true;
    app.animTargetPx = 0.0;
    taskbar_anim_start_slide(app);
  }
  taskbar_draw(app);
}

static void taskbar_pointer_leave(void* data, wl_pointer* p, uint32_t serial,
                                   wl_surface* surface) {
  auto& app = *static_cast<TaskbarApp*>(data);
  (void)p; (void)serial; (void)surface;
  if (app.popupKind == TaskbarPopupKind::AppDrawer) {
    auto& ds = app.appDrawerState;
    ds.hoverListRow = -1;
    ds.hoverDrawerPinIdx = -1;
    ds.hoverPowerIdx = -1;
  } else if (app.popupKind == TaskbarPopupKind::ControlCenter) {
    auto& cs = app.ccState;
    cs.hoverTarget = eh::shell::controlcenter::CcHoverTarget::None;
    cs.hoverRowIdx = -1;
    cs.hoverStreamId = -1;
  }
  app.pointerSurface = nullptr;
  if (app.hoverSlot >= 0) { app.hoverSlot = -1; app.hoverLiftPx = 0.0; }
  if (app.pressedSlot >= 0) { app.pressedSlot = -1; }
  taskbar_tooltip_cancel(app);
  // Auto-hide hide
  if (app.settings.autoHide && app.reveal && app.popupKind == TaskbarPopupKind::None) {
    app.reveal = false;
    app.animTargetPx = static_cast<double>(app.settings.height - app.triggerHeight);
    taskbar_anim_start_slide(app);
  }
  taskbar_draw(app);
}

static void taskbar_pointer_motion(void* data, wl_pointer* p, uint32_t time,
                                    wl_fixed_t sx, wl_fixed_t sy) {
  auto& app = *static_cast<TaskbarApp*>(data);
  (void)p; (void)time;
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);

  // Popup surface: power-confirm hover redraw (coalesced).
  if (taskbar_popup_surface(app, app.pointerSurface) &&
      app.popupKind == TaskbarPopupKind::PowerConfirm) {
    app.popupMotionDirty = true;
    if (!app.popupFrameCb) {
      app.popupFrameCb = wl_surface_frame(app.popupSurface);
      wl_callback_add_listener(app.popupFrameCb, &g_taskbar_popup_frame_listener, &app);
      wl_surface_commit(app.popupSurface);
    }
    wl_display_flush(app.display);
    return;
  }

  // Popup surface: CC slider drag update.
  if (taskbar_popup_surface(app, app.pointerSurface) &&
      app.popupKind == TaskbarPopupKind::ControlCenter) {
    auto& cs = app.ccState;
    const uint64_t nowMs = eh::shell::now_mono_ms();
    const double W = static_cast<double>(app.popupW);
    const double pad = 18.0;
    const double tileW = (W - pad * 2.0 - 12.0) * 0.5;

    // Mixer drag update
    if (cs.mixerDragActive) {
      const double t = std::clamp(
          (app.pointerX - cs.mixerDragSx) / std::max(1.0, cs.mixerDragSw), 0.0, 1.0);
      cs.mixerDragVisualT = t;
      const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 100);
      cs.mixerDragUiPct = pct;
      if (pct != cs.mixerLastLoggedPct) cs.mixerLastLoggedPct = pct;
      if (cs.mixerLastApplyMs == 0 || nowMs - cs.mixerLastApplyMs >= 16) {
        if (cs.mixerDragIsInput)
          eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(
              cs.mixerDragStreamId, t);
        else
          eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(
              cs.mixerDragStreamId, t);
        cs.mixerLastAppliedPct = pct;
        cs.mixerLastApplyMs = nowMs;
        cs.mixerIgnoreStateUntilMs = nowMs + 180;
      }
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }

    // Output/input audio slider drag update
    if (cs.audioDragActive || cs.inputDragActive) {
      const bool isOutput = cs.audioDragActive;
      const double cardX = isOutput ? pad : (pad + tileW + 12.0);
      const double trackX = cardX + eh::shell::cc_slider::kAudioTrackXPad;
      const double trackW = tileW - 2.0 * eh::shell::cc_slider::kAudioTrackXPad;
      const double t = std::clamp(
          (app.pointerX - trackX) / std::max(1.0, trackW), 0.0, 1.0);
      const int pct = std::clamp(static_cast<int>(std::lround(t * 100.0)), 0, 150);
      if (isOutput) {
        cs.audioDragVisualT = t;
        cs.audioDragUiPct = pct;
        if (pct != cs.audioLastLoggedPct) cs.audioLastLoggedPct = pct;
        if (cs.audioLastApplyMs == 0 || nowMs - cs.audioLastApplyMs >= 16) {
          eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(t);
          cs.audioLastAppliedPct = pct;
          cs.audioLastApplyMs = nowMs;
          cs.audioIgnoreStateUntilMs = nowMs + 180;
        }
      } else {
        cs.inputDragVisualT = t;
        cs.inputDragUiPct = pct;
        if (pct != cs.inputLastLoggedPct) cs.inputLastLoggedPct = pct;
        if (cs.inputLastApplyMs == 0 || nowMs - cs.inputLastApplyMs >= 16) {
          eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(t);
          cs.inputLastAppliedPct = pct;
          cs.inputLastApplyMs = nowMs;
          cs.inputIgnoreStateUntilMs = nowMs + 180;
        }
      }
      taskbar_popup_draw(app);
      wl_display_flush(app.display);
      return;
    }

    // CC popup: hover zone detection (when not dragging).
    {
      using CcHT = eh::shell::controlcenter::CcHoverTarget;
      const double colGap = 12.0, rowGap = 12.0, tileH = 92.0;
      const double gridTop = pad + 24.0;

      auto in_rect = [&](double rx, double ry, double rw, double rh) -> bool {
        return (app.pointerX >= rx && app.pointerX <= rx + rw &&
                app.pointerY >= ry && app.pointerY <= ry + rh);
      };

      const double netOffset = control_center_network_offset_y(cs);
      const double audioY = gridTop + tileH + rowGap + netOffset;
  const double baseDevY = audioY + tileH;

      double outPanelH = 0.0, inPanelH = 0.0;
      if (cs.outputDevicesExpanded) outPanelH = control_center_output_devices_panel_h(cs);
      if (cs.inputDevicesExpanded) inPanelH = control_center_input_devices_panel_h(cs);

      const double mixerY = control_center_mixer_card_y(cs, nowMs, app.popupW, app.popupH);
      constexpr double kMixerH = 90.0;

      double mediaY = mixerY + kMixerH + rowGap;
      if (cs.mixerExpanded) {
        const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
        const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams();
        const int outRows = std::min(4, static_cast<int>(streams.size()));
        const int inRows = std::min(3, static_cast<int>(inStreams.size()));
        const double expH = 48.0 + outRows * 48.0 + 30.0 + inRows * 48.0 + 10.0;
        mediaY = mixerY + kMixerH + 8.0 + expH + rowGap;
      }
      constexpr double kCardH = 92.0;
      const double weatherY = mediaY + kCardH + rowGap;

      const auto oldTarget = cs.hoverTarget;
      const int oldRowIdx = cs.hoverRowIdx;
      cs.hoverTarget = CcHT::None;
      cs.hoverRowIdx = -1;
      cs.hoverStreamId = -1;

      // Network card body
      if (cs.hoverTarget == CcHT::None &&
          in_rect(pad, gridTop, W - pad * 2.0, tileH)) {
        cs.hoverTarget = CcHT::NetworkCard;
      }

      // Network panel rows
      if (cs.hoverTarget == CcHT::None && cs.networkExpanded) {
        const double panelY = gridTop + tileH + rowGap;
        const double headerH = 32.0;
        constexpr int kMaxRows = 8;
        const auto aps = eh::shell::dock_slot_hooks::control_center_wifi_scan(false);
        const int rows = std::min(kMaxRows, static_cast<int>(aps.size()));
        const double rowH = 34.0;
        double ry = panelY + headerH;
        for (int i = 0; i < rows; ++i) {
          if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
            cs.hoverTarget = CcHT::NetworkRow;
            cs.hoverRowIdx = i;
            break;
          }
          ry += rowH;
        }
        if (cs.hoverTarget == CcHT::None && in_rect(pad, panelY, W - pad * 2.0, headerH)) {
          cs.hoverTarget = CcHT::NetworkCard;
        }
      }

      // Output audio card
      if (cs.hoverTarget == CcHT::None) {
        const double cardX = pad;
        const double cardY = audioY;
        const double tx = cardX + eh::shell::cc_slider::kAudioTrackXPad;
        const double ty = cardY + eh::shell::cc_slider::kAudioTrackYFromCardTop;
        const double sx = tx - eh::shell::cc_slider::kAudioHitPadH;
        const double sy = ty - eh::shell::cc_slider::kAudioHitPadV;
        const double sw = (tileW - 2.0 * eh::shell::cc_slider::kAudioTrackXPad) + 2.0 * eh::shell::cc_slider::kAudioHitPadH;
        const double sh = eh::shell::cc_slider::kAudioTrackH + 2.0 * eh::shell::cc_slider::kAudioHitPadV;
        const double mx = cardX + 24.0 - 14.0;
        const double my = cardY + 28.0 - 14.0;

        if (in_rect(sx, sy, sw, sh)) cs.hoverTarget = CcHT::OutputAudioSlider;
        else if (in_rect(mx, my, 28.0, 28.0)) cs.hoverTarget = CcHT::OutputAudioMute;
        else if (in_rect(cardX, cardY, tileW, tileH)) cs.hoverTarget = CcHT::OutputAudioSlider;
      }

      // Input audio card
      if (cs.hoverTarget == CcHT::None) {
        const double cardX = pad + tileW + colGap;
        const double cardY = audioY;
        const double tx = cardX + eh::shell::cc_slider::kAudioTrackXPad;
        const double ty = cardY + eh::shell::cc_slider::kAudioTrackYFromCardTop;
        const double sx = tx - eh::shell::cc_slider::kAudioHitPadH;
        const double sy = ty - eh::shell::cc_slider::kAudioHitPadV;
        const double sw = (tileW - 2.0 * eh::shell::cc_slider::kAudioTrackXPad) + 2.0 * eh::shell::cc_slider::kAudioHitPadH;
        const double sh = eh::shell::cc_slider::kAudioTrackH + 2.0 * eh::shell::cc_slider::kAudioHitPadV;
        const double mx = cardX + 24.0 - 14.0;
        const double my = cardY + 28.0 - 14.0;

        if (in_rect(sx, sy, sw, sh)) cs.hoverTarget = CcHT::InputAudioSlider;
        else if (in_rect(mx, my, 28.0, 28.0)) cs.hoverTarget = CcHT::InputAudioMute;
        else if (in_rect(cardX, cardY, tileW, tileH)) cs.hoverTarget = CcHT::InputAudioSlider;
      }

      // Output device rows
      if (cs.hoverTarget == CcHT::None && cs.outputDevicesExpanded && outPanelH > 0.0) {
        const double headerH = 28.0;
        const double rowH = 34.0;
        const auto devs = eh::shell::dock_slot_hooks::control_center_output_devices();
        constexpr int kMaxDeviceRows = 12;
        const int rows = std::min(kMaxDeviceRows, static_cast<int>(devs.size()));
        double ry = baseDevY + headerH;
        for (int i = 0; i < rows; ++i) {
          if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
            cs.hoverTarget = CcHT::OutputDeviceRow;
            cs.hoverRowIdx = i;
            break;
          }
          ry += rowH;
        }
      }

      // Input device rows
      if (cs.hoverTarget == CcHT::None && cs.inputDevicesExpanded && inPanelH > 0.0) {
        const double inPanelY = baseDevY;
        const double headerH = 28.0;
        const double rowH = 34.0;
        const auto devs = eh::shell::dock_slot_hooks::control_center_input_devices();
        constexpr int kMaxDeviceRows = 12;
        const int rows = std::min(kMaxDeviceRows, static_cast<int>(devs.size()));
        double ry = inPanelY + headerH;
        for (int i = 0; i < rows; ++i) {
          if (in_rect(pad + 10.0, ry + 6.0, W - pad * 2.0 - 20.0, rowH - 10.0)) {
            cs.hoverTarget = CcHT::InputDeviceRow;
            cs.hoverRowIdx = i;
            break;
          }
          ry += rowH;
        }
      }

      // Mixer card header
      if (cs.hoverTarget == CcHT::None) {
        constexpr double headerH = 28.0;
        if (in_rect(pad, mixerY, W - pad * 2.0, headerH)) cs.hoverTarget = CcHT::MixerCard;
      }

      // Mixer sliders
      if (cs.hoverTarget == CcHT::None) {
        auto mixer_slider_hover = [&](double ry) -> bool {
          const double tx = pad + 10.0;
          const double ty = ry + eh::shell::cc_slider::kMixerRowTrackYOffset
                          + (10.0 - eh::shell::cc_slider::kMixerTrackH) * 0.5;
          const double tw = W - pad * 2.0 - 20.0;
          const double hx = tx - eh::shell::cc_slider::kMixerHitPadH;
          const double hy = ty - eh::shell::cc_slider::kMixerHitPadV;
          const double hw = tw + 2.0 * eh::shell::cc_slider::kMixerHitPadH;
          const double hh = eh::shell::cc_slider::kMixerTrackH + 2.0 * eh::shell::cc_slider::kMixerHitPadV;
          return in_rect(hx, hy, hw, hh);
        };

        if (cs.mixerExpanded) {
          const auto outStreams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
          const auto inStreams = eh::shell::dock_slot_hooks::control_center_input_mixer_streams();
          const int outRows = std::min(4, static_cast<int>(outStreams.size()));
          const int inRows = std::min(3, static_cast<int>(inStreams.size()));
          const double expY = mixerY + kMixerH + 8.0;
          double ry = expY + 44.0;
          for (int i = 0; i < outRows; ++i) {
            if (mixer_slider_hover(ry)) {
              cs.hoverTarget = CcHT::MixerSlider;
              cs.hoverRowIdx = i;
              cs.hoverStreamId = outStreams[static_cast<size_t>(i)].sink_input_id;
              break;
            }
            ry += 48.0;
          }
          if (cs.hoverTarget == CcHT::None) {
            ry += 14.0;
            for (int i = 0; i < inRows; ++i) {
              if (mixer_slider_hover(ry)) {
                cs.hoverTarget = CcHT::MixerSlider;
                cs.hoverRowIdx = outRows + i;
                cs.hoverStreamId = inStreams[static_cast<size_t>(i)].sink_input_id;
                break;
              }
              ry += 48.0;
            }
          }
        } else {
          const auto streams = eh::shell::dock_slot_hooks::control_center_mixer_streams();
          const int maxRows = 1;
          const int rows = std::min(static_cast<int>(streams.size()), maxRows);
          const double ry0 = mixerY + 28.0;
          for (int i = 0; i < rows; ++i) {
            if (mixer_slider_hover(ry0 + static_cast<double>(i) * 40.0)) {
              cs.hoverTarget = CcHT::MixerSlider;
              cs.hoverRowIdx = i;
              cs.hoverStreamId = streams[static_cast<size_t>(i)].sink_input_id;
              break;
            }
          }
        }
      }

      // Media buttons
      if (cs.hoverTarget == CcHT::None) {
        const double btnR = 14.0;
        const double cy = mediaY + kCardH - 24.0;
        const double cx2 = pad + (W - pad * 2.0) - 22.0;
        const double cx1 = cx2 - (btnR * 2.0 + 10.0);
        const double cx0 = cx1 - (btnR * 2.0 + 10.0);
        auto hit_circle = [&](double cx, double cy0) -> bool {
          const double dx = app.pointerX - cx;
          const double dy = app.pointerY - cy0;
          return (dx * dx + dy * dy) <= (btnR * btnR);
        };
        if (hit_circle(cx0, cy)) cs.hoverTarget = CcHT::MediaPrev;
        else if (hit_circle(cx1, cy)) cs.hoverTarget = CcHT::MediaPlayPause;
        else if (hit_circle(cx2, cy)) cs.hoverTarget = CcHT::MediaNext;
      }

      // Weather card
      if (cs.hoverTarget == CcHT::None &&
          in_rect(pad, weatherY, W - pad * 2.0, kCardH)) {
        cs.hoverTarget = CcHT::WeatherCard;
      }

      if (oldTarget != cs.hoverTarget || oldRowIdx != cs.hoverRowIdx) {
        taskbar_popup_draw(app);
      }
    }
    return;
  }

  // Popup surface: app drawer hover.
  if (taskbar_popup_surface(app, app.pointerSurface) &&
      app.popupKind == TaskbarPopupKind::AppDrawer) {
    auto& ds = app.appDrawerState;
    ds.pointerX = app.pointerX;
    ds.pointerY = app.pointerY;
    const int oldHoverRow = ds.hoverListRow;
    const int oldHoverPin = ds.hoverDrawerPinIdx;
    const int oldHoverPwr = ds.hoverPowerIdx;
    const int oldHoverCat = ds.hoverCategoryIdx;
    const auto zone = eh::appdrawer::app_drawer_hit_zone(ds, app.pointerX, app.pointerY);
    if (zone == eh::appdrawer::AppDrawerHitZone::AppListRow) {
      ds.hoverListRow = eh::appdrawer::app_drawer_pick_row_index(ds, app.pointerX, app.pointerY);
      ds.hoverDrawerPinIdx = -1;
      ds.hoverPowerIdx = -1;
      ds.hoverCategoryIdx = -1;
    } else if (zone == eh::appdrawer::AppDrawerHitZone::CategoryTab) {
      ds.hoverCategoryIdx = eh::appdrawer::app_drawer_pick_category_tab(ds, app.pointerX, app.pointerY);
      ds.hoverListRow = -1;
      ds.hoverDrawerPinIdx = -1;
      ds.hoverPowerIdx = -1;
    } else if (zone == eh::appdrawer::AppDrawerHitZone::PinnedApp) {
      ds.hoverDrawerPinIdx = eh::appdrawer::app_drawer_pick_pinned_index(ds, app.pointerX, app.pointerY);
      ds.hoverListRow = -1;
      ds.hoverPowerIdx = -1;
      ds.hoverCategoryIdx = -1;
    } else if (zone == eh::appdrawer::AppDrawerHitZone::PowerButton) {
      ds.hoverPowerIdx = eh::appdrawer::app_drawer_pick_power_index(ds, app.pointerX, app.pointerY);
      ds.hoverListRow = -1;
      ds.hoverDrawerPinIdx = -1;
      ds.hoverCategoryIdx = -1;
    } else if (zone == eh::appdrawer::AppDrawerHitZone::NightlightButton) {
      ds.hoverPowerIdx = 4;
      ds.hoverListRow = -1;
      ds.hoverDrawerPinIdx = -1;
      ds.hoverCategoryIdx = -1;
    } else {
      ds.hoverListRow = -1;
      ds.hoverDrawerPinIdx = -1;
      ds.hoverPowerIdx = -1;
      ds.hoverCategoryIdx = -1;
    }
    if (oldHoverRow != ds.hoverListRow || oldHoverPin != ds.hoverDrawerPinIdx || oldHoverPwr != ds.hoverPowerIdx || oldHoverCat != ds.hoverCategoryIdx) {
      taskbar_popup_draw(app);
    }
    return;
  }

  // Taskbar bar surface hover.
  if (!taskbar_layer_surface(app, app.pointerSurface)) return;

  int newHover = -1;
  for (size_t i = 0; i < g_widgetHits.size(); i++) {
    if (point_in(app.pointerX, app.pointerY, g_widgetHits[i].x, g_widgetHits[i].y,
                 g_widgetHits[i].w, g_widgetHits[i].h)) {
      newHover = static_cast<int>(i); break;
    }
  }
  if (newHover != app.hoverSlot) {
    app.hoverSlot = newHover;
    app.hoverLiftPx = (newHover >= 0) ? 5.0 : 0.0;
    if (app.settings.tooltipsEnabled) {
      if (newHover >= 0) {
        app.tooltipHoverSlot = newHover;
        app.tooltipHoverStart = std::chrono::steady_clock::now();
        if (app.tooltipShownSlot >= 0 && app.tooltipShownSlot != newHover)
          taskbar_tooltip_destroy(app);
      } else {
        taskbar_tooltip_cancel(app);
      }
    }
    taskbar_draw(app);
  }

  // Pin drag motion
  taskbar_pin_drag_motion(app);
}

static void taskbar_pointer_button(void* data, wl_pointer* p, uint32_t serial,
                                    uint32_t time, uint32_t button, uint32_t state) {
  auto& app = *static_cast<TaskbarApp*>(data);
  (void)p; (void)time;

  const bool left = (button == 0x110);
  const bool right = (button == 0x111 || button == 0x112 || button == 0x113);
  if (!left && !right) return;

  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    // Finalize CC slider drag
    if (taskbar_popup_surface(app, app.pointerSurface) &&
        app.popupKind == TaskbarPopupKind::ControlCenter) {
      auto& cs = app.ccState;
      if (cs.audioDragActive || cs.inputDragActive || cs.mixerDragActive) {
        auto finalize = [](double pct) { return std::clamp(pct / 100.0, 0.0, 1.5); };
        if (cs.audioDragActive) {
          eh::shell::dock_slot_hooks::control_center_set_audio_output_volume(finalize(cs.audioDragUiPct));
          cs.audioDragActive = false;
          cs.audioDragVisualT = -1.0;
          cs.audioDragUiPct = -1;
          cs.audioLastLoggedPct = -1;
        }
        if (cs.inputDragActive) {
          eh::shell::dock_slot_hooks::control_center_set_audio_input_volume(finalize(cs.inputDragUiPct));
          cs.inputDragActive = false;
          cs.inputDragVisualT = -1.0;
          cs.inputDragUiPct = -1;
          cs.inputLastLoggedPct = -1;
        }
        if (cs.mixerDragActive) {
          const double t = finalize(cs.mixerDragUiPct);
          if (cs.mixerDragIsInput)
            eh::shell::dock_slot_hooks::control_center_set_input_mixer_stream_volume(cs.mixerDragStreamId, t);
          else
            eh::shell::dock_slot_hooks::control_center_set_mixer_stream_volume(cs.mixerDragStreamId, t);
          cs.mixerDragActive = false;
          cs.mixerDragVisualT = -1.0;
          cs.mixerDragUiPct = -1;
          cs.mixerDragStreamId = -1;
          cs.mixerDragSx = 0.0;
          cs.mixerDragSw = 1.0;
          cs.mixerLastLoggedPct = -1;
        }
        taskbar_popup_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
    // Pin drag release
    {
      const auto result = taskbar_pin_drag_release(app, left);
      if (result.wasDragging || result.wasClick) {
        if (result.wasClick && !result.key.empty()) {
          bool found = false;
          if (app.toplevels) for (const auto& tl : *app.toplevels) {
            if (tl.handle && !tl.closed &&
                eh::shell::paths::normalize_desktop_app_id(tl.appId) == result.key) {
              zwlr_foreign_toplevel_handle_v1_activate(tl.handle, app.seat);
              wl_display_flush(app.display);
              found = true;
              break;
            }
          }
          if (!found) {
            if (auto desktop = find_desktop_file_for_appid(result.key)) {
              if (auto info = read_desktop_entry_info(*desktop)) {
                launch_exec_command(info->exec);
              }
            }
          }
        }

        debug_log("taskbar", "pin_drag: save triggered wasDragging=%d dragDirty=%d", (int)result.wasDragging, (int)result.dragDirty);
        if (result.wasDragging && result.dragDirty) {
          // IMPORTANT: save (and thus refresh the global config snapshot via
          // shell_config_apply_from_memory) BEFORE drawing. taskbar_draw()
          // begins with taskbar_maybe_reload_settings(), which compares
          // app.settings against the global snapshot and overwrites
          // app.settings wholesale if they differ. If we draw first, the
          // snapshot is still stale (old pin order), so the reload logic
          // clobbers the just-committed reorder right back to the old order
          // before it ever reaches disk — this was why nothing was written.
          debug_log("taskbar", "pin_drag: calling taskbar_save_settings");
          taskbar_save_settings(app);
        }

        taskbar_draw(app);
        wl_display_flush(app.display);
        return;
      }
    }
    if (app.pressedSlot >= 0) { app.pressedSlot = -1; taskbar_draw(app); }
    return;
  }

  // Popup surface click
  if (taskbar_popup_surface(app, app.pointerSurface)) {
    if (right && app.popupKind == TaskbarPopupKind::AppDrawer) {
      auto& s = app.appDrawerState;
      const double px = app.pointerX;
      const double py = app.pointerY;
      const auto zone = eh::appdrawer::app_drawer_hit_zone(s, px, py);

      std::string appRawId;
      std::string appExec;
      bool isCurrentlyPinned = false;

      if (zone == eh::appdrawer::AppDrawerHitZone::AppListRow) {
        const int row = eh::appdrawer::app_drawer_pick_row_index(s, px, py);
        if (row >= 0 && row < static_cast<int>(s.hits.size())) {
          const auto& hit = s.hits[static_cast<size_t>(row)];
          appRawId = eh_app_drawer_desktop_stem_from_path(hit.path);
          appExec = hit.exec;
          const std::string norm = eh::shell::paths::normalize_desktop_app_id(appRawId);
          for (const auto& p : app.settings.pinnedApps) {
            if (pin_identity_pin_raw_matches_key(p, norm)) {
              isCurrentlyPinned = true;
              break;
            }
          }
        }
      } else if (zone == eh::appdrawer::AppDrawerHitZone::PinnedApp) {
        const int pinIdx = eh::appdrawer::app_drawer_pick_pinned_index(s, px, py);
        if (pinIdx >= 0 && pinIdx < static_cast<int>(s.pinnedApps.size())) {
          appRawId = s.pinnedApps[static_cast<size_t>(pinIdx)];
          isCurrentlyPinned = true;
          const std::string normId = eh::shell::paths::normalize_desktop_app_id(appRawId);
          if (auto desktop = find_desktop_file_for_appid(normId)) {
            if (auto info = read_desktop_entry_info(*desktop)) {
              appExec = info->exec;
            }
          }
        }
      }

      if (!appRawId.empty() && appRawId != "unknown" && appRawId != eh::shell::kSettingsAppId) {
        app.popupAppKey = appRawId;
        app.popupAppDesktopExec = appExec;

        std::vector<TaskbarPopupItem> items;
        items.push_back({.id = 9001,
                         .label = isCurrentlyPinned ? "Unpin" : "Pin",
                         .enabled = true});

        if (!appExec.empty()) {
          items.push_back({.id = -1, .label = "", .enabled = false});
          items.push_back({.id = 3000, .label = "New Window", .enabled = true});
        }

        int popupW = 200, popupH = 200;
        {
          cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
          cairo_t* cr = cairo_create(surf);
          cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
          cairo_set_font_size(cr, 13.0);
          double maxw = 0;
          for (const auto& it : items) {
            if (it.id < 0) continue;
            cairo_text_extents_t ex{};
            cairo_text_extents(cr, it.label.c_str(), &ex);
            maxw = std::max(maxw, ex.x_advance);
          }
          cairo_destroy(cr);
          cairo_surface_destroy(surf);
          int itemCount = 0, sepCount = 0;
          for (const auto& it : items) { if (it.id < 0) ++sepCount; else ++itemCount; }
          popupW = std::clamp(static_cast<int>(12 + maxw + 10), 160, 360);
          popupH = 4 + 26 * itemCount + 13 * sepCount + 4;
        }

        app.popupItems = std::move(items);
        taskbar_popup_close(app);
        app.popupKind = TaskbarPopupKind::App;
        taskbar_popup_create(app, static_cast<int>(app.pointerX), popupW, popupH);
        wl_display_flush(app.display);
        return;
      }
    }

    taskbar_handle_popup_click(app, serial);
    return;
  }

  // Click outside popup → dismiss it
  if (app.popupSurface) {
    taskbar_popup_close(app);
    wl_display_flush(app.display);
  }

  if (!taskbar_layer_surface(app, app.pointerSurface)) return;

  int hitIdx = -1;
  for (size_t i = 0; i < g_widgetHits.size(); i++) {
    if (point_in(app.pointerX, app.pointerY, g_widgetHits[i].x, g_widgetHits[i].y,
                 g_widgetHits[i].w, g_widgetHits[i].h)) {
      hitIdx = static_cast<int>(i); break;
    }
  }
  if (hitIdx < 0) return;

  app.pressedSlot = hitIdx;
  taskbar_draw(app);
  wl_display_flush(app.display);

  const auto& hit = g_widgetHits[static_cast<size_t>(hitIdx)];
  const std::string& wid = hit.widgetId;

  // ════ PIN DRAG CANDIDATE ════
  if (hit.isPinned && hit.slotKind == 0 && left) {
    taskbar_pin_drag_init(app, wid);
    return;
  }

  // ════ CLICK DISPATCH ════

  // Tray
  if (wid.rfind(kSlotKeyTray, 0) == 0) {
    const std::string want = wid.substr(std::string(kSlotKeyTray).size());
    std::lock_guard<std::mutex> lock(app.trayMutex);
    for (const auto& ti : app.trayItems) {
      if ((ti.service + ti.path) == want) {
        if (right) {
          show_tray_popup_inline(app, ti, static_cast<int>(app.pointerX), serial);
        } else {
          try {
            auto proxy = sdbus::createProxy(*app.trayBus, sdbus::ServiceName{ti.service},
                                            sdbus::ObjectPath{ti.path});
            proxy->callMethod("Activate")
                .onInterface("org.kde.StatusNotifierItem")
                .withArguments(static_cast<int32_t>(app.pointerX),
                               static_cast<int32_t>(app.pointerY));
          } catch (const std::exception& e) {
            eh::shell_log::dbus_tray("activate failed: ", e.what());
          }
        }
        break;
      }
    }
    return;
  }

  // Right-click on trash → Open Trash / Empty Trash
  if (right && (wid == "trash" || eh::config::widget_implementation_type(wid) == "trash")) {
    app.popupItems.clear();
    app.popupItems.push_back({.id = 8001, .label = "Open Trash", .enabled = true});
    app.popupItems.push_back({.id = -1, .label = "", .enabled = false});
    app.popupItems.push_back({.id = 8002, .label = "Empty Trash", .enabled = app.trashFull});

    int popupW = 200, popupH = 200;
    {
      cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
      cairo_t* cr = cairo_create(surf);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13.0);
      double maxw = 0;
      for (const auto& it : app.popupItems) {
        if (it.id < 0) continue;
        cairo_text_extents_t ex{};
        cairo_text_extents(cr, it.label.c_str(), &ex);
        maxw = std::max(maxw, ex.x_advance);
      }
      cairo_destroy(cr);
      cairo_surface_destroy(surf);
      int itemCount = 0, sepCount = 0;
      for (const auto& it : app.popupItems) { if (it.id < 0) ++sepCount; else ++itemCount; }
      popupW = std::clamp(static_cast<int>(12 + maxw + 10), 160, 360);
      popupH = 4 + 26 * itemCount + 13 * sepCount + 4;
    }

    app.popupKind = TaskbarPopupKind::App;
    taskbar_popup_close(app);
    if (app.display) (void)wl_display_roundtrip(app.display);
    taskbar_popup_create(app, static_cast<int>(app.pointerX), popupW, popupH);
    wl_display_flush(app.display);
    return;
  }

  // Right-click on app → context menu
  if (right) {
    if (hit.slotKind == 0) {
      const std::string normWid = eh::shell::paths::normalize_desktop_app_id(wid);
      show_app_menu(app, normWid, hit.chosenSerial, hit.isPinned,
                     static_cast<int>(app.pointerX), serial);
    }
    return;
  }

  // Settings — toggle the external horizon-settings child (spawn if not
  // running, otherwise publish a toggle command over the IPC bus). The
  // foreign-toplevel path is bypassed because the compositor
  // reports our xdg-toplevel via foreign_toplevel_list, which would turn
  // the left click into an "activate" instead of a toggle-close.
  if (wid == kSlotKeySettings) {
    debug_log("taskbar", "settings click: serial=%u px=%.0f py=%.0f init=%d",
              serial, app.pointerX, app.pointerY,
              (int)eh::settings::embed_is_initialized());
    debug_log("taskbar", "settings: requesting launch (external process)");
    eh::settings::request_launch_settings();
    debug_log("taskbar", "settings: request_launch_settings returned");
    wl_display_flush(app.display);
    debug_log("taskbar", "settings: flush done, returning from click handler");
    return;
  }

  if (wid == kSlotKeySpotlight)
    return;

  if (wid == kSlotKeyAppMenu || wid == kSlotKeyAppDrawer || eh::config::widget_implementation_type(wid) == "smenu") {
    if (app.popupSurface) {
      taskbar_popup_close(app);
    } else {
      const bool smenuMode = eh::config::widget_implementation_type(wid) == "smenu";
      app.appDrawerState = eh::appdrawer::AppDrawerState{};
      app.appDrawerState.smenuMode = smenuMode;
      app.appDrawerState.popupW = eh::appdrawer::app_drawer_popup_width();
      app.appDrawerState.popupH = eh::appdrawer::app_drawer_popup_height();
      app.appDrawerState.pinnedApps = app.settings.pinnedApps;
      app.appDrawerState.startMenuPinnedApps = app.settings.pinnedApps;
      app.appDrawerState.pointerX = app.pointerX;
      app.appDrawerState.pointerY = app.pointerY;
      app.appDrawerState.viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
      if (smenuMode) eh::appdrawer::app_drawer_update_categories(app.appDrawerState);
      eh::shell::dock::app_drawer::invalidate_desktop_entries_cache();
      eh::appdrawer::app_drawer_refresh_hits(app.appDrawerState);
      app.popupKind = TaskbarPopupKind::AppDrawer;
      app.appDrawerPowerConfirmOpen = false;
      app.appDrawerPowerConfirmIdx = -1;
      taskbar_popup_create(app, static_cast<int>(app.pointerX),
                           app.appDrawerState.popupW, app.appDrawerState.popupH);
    }
    wl_display_flush(app.display);
    return;
  }

  const std::string type = eh::config::widget_implementation_type(wid);

  if (type == "clock") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::Calendar;
      std::time_t t = std::time(nullptr);
      std::tm local{};
      localtime_r(&t, &local);
      app.calSelectedDate = local;
      app.calDisplayDate = local;
      app.calDisplayDate.tm_mday = 1;
      app.calDisplayDate.tm_hour = 0; app.calDisplayDate.tm_min = 0; app.calDisplayDate.tm_sec = 0;
      app.calDisplayDate.tm_isdst = -1;
      std::mktime(&app.calDisplayDate);
      taskbar_popup_create(app, static_cast<int>(app.pointerX), 320, 380);
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "weather") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::Weather;
      app.weatherInstanceId = wid;
      taskbar_popup_create(app, static_cast<int>(app.pointerX), eh::widgets::popup::weather::kWeatherPopupW, eh::widgets::popup::weather::kWeatherPopupH);
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "media") {
    if (!app.mpris) return;
    const double iconSize = static_cast<double>(app.settings.iconSize);
    const int zone = eh::mpris::DockMpris::media_hit_zone(app.pointerX - hit.x, hit.w, iconSize);
    if (zone == 0) app.mpris->previous();
    else if (zone == 2) app.mpris->next();
    else if (zone == 1) app.mpris->play_pause();
    else {
      if (app.popupSurface) { taskbar_popup_close(app); }
      else {
        app.popupKind = TaskbarPopupKind::MediaPlayer;
        taskbar_popup_create(app, static_cast<int>(app.pointerX),
                             eh::widgets::popup::media_player::kMediaPlayerPopupW,
                             eh::widgets::popup::media_player::kMediaPlayerPopupH);
      }
    }
    return;
  }

  if (type == "workspaces") {
    const auto& scAct = eh::config::shell_config_snapshot();
    const double globalScale = std::clamp(scAct.dock.shellUiScale, 0.5, 2.0);
    const double taskbarUIScale = std::clamp(app.settings.scale, 0.5, 2.0) * globalScale;
    const double iconRaw = static_cast<double>(app.settings.iconSize) * taskbarUIScale;
    const double boxH = static_cast<double>(app.settings.height) * taskbarUIScale;
    const double icon = std::clamp(iconRaw, 8.0, std::max(8.0, boxH - 8.0));
    const int pick = eh::shell::dock_slot_hooks::workspaces_pick_index(
        app.pointerX - hit.x, hit.w, scAct, wid, icon, app.workspaceStrip);
    if (pick >= 0 && static_cast<size_t>(pick) < app.workspaceStrip.size())
      eh::shell::dock_slot_hooks::workspace_activate_entry(
          app.workspaceStrip[static_cast<size_t>(pick)], app.compositorKind);
    return;
  }

  if (type == "control_center") {
    if (app.popupSurface) {
      taskbar_popup_close(app);
    } else {
      app.popupKind = TaskbarPopupKind::ControlCenter;
      app.ccState = eh::shell::controlcenter::ControlCenterState{};
      {
        const std::string wid = app.ccWidgetId.empty() ? std::string("control_center") : app.ccWidgetId;
        static_cast<void>(eh::shell::dock_slot_hooks::control_center_weather_state(eh::config::shell_config_snapshot(), wid));
      }
      taskbar_popup_create(app, static_cast<int>(app.pointerX), 620, 640);
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "volume_mixer") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::VolumeMixer;
      taskbar_popup_create(app, static_cast<int>(app.pointerX), 300, 440);
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "vpn") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::Vpn;
      auto& nm = eh::net::NetworkManagerService::instance();
      const int vpnCount = static_cast<int>(nm.state().vpnConnections.size());
      const int popupH = eh::shell::dock::popup::vpn::vpn_popup_height(vpnCount);
      taskbar_popup_create(app, static_cast<int>(app.pointerX),
                           eh::shell::dock::popup::vpn::kVpnPopupW, popupH);
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "battery") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::Battery;
      taskbar_popup_create(app, static_cast<int>(app.pointerX),
                           eh::widgets::kBatteryPopupW, eh::widgets::battery_popup_height());
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "bluetooth") {
    if (app.popupSurface) { taskbar_popup_close(app); }
    else {
      app.popupKind = TaskbarPopupKind::Bluetooth;
      taskbar_popup_create(app, static_cast<int>(app.pointerX),
                           eh::widgets::kBluetoothPopupW, eh::widgets::bluetooth_popup_height());
    }
    wl_display_flush(app.display);
    return;
  }

  if (type == "launchpad") {
    ipc_send_command("launchpad");
    wl_display_flush(app.display);
    return;
  }
  if (type == "trash") { launch_exec_command("xdg-open trash:///"); wl_display_flush(app.display); return; }

  // Default: app activation / launch
  {
    const std::string normWid = eh::shell::paths::normalize_desktop_app_id(wid);
    bool found = false;
    if (app.toplevels) for (const auto& tl : *app.toplevels) {
      if (tl.handle && !tl.closed &&
          eh::shell::paths::normalize_desktop_app_id(tl.appId) == normWid) {
        zwlr_foreign_toplevel_handle_v1_activate(tl.handle, app.seat);
        wl_display_flush(app.display);
        found = true;
        break;
      }
    }
    if (!found && hit.isPinned) {
      if (auto desktop = find_desktop_file_for_appid(normWid)) {
        if (auto info = read_desktop_entry_info(*desktop)) {
          launch_exec_command(info->exec);
        }
      }
    }
  }
}

static void taskbar_pointer_frame(void*, wl_pointer*) {}
static void taskbar_pointer_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
  auto& app = *static_cast<TaskbarApp*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (!taskbar_popup_surface(app, app.pointerSurface) || app.popupKind != TaskbarPopupKind::AppDrawer) return;
  const double dv = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dv * 20.0));
  eh::appdrawer::app_drawer_scroll_pixels(app.appDrawerState, -deltaPx);
  taskbar_popup_draw(app);
  wl_display_flush(app.display);
}
static void taskbar_pointer_axis_source(void*, wl_pointer*, uint32_t) {}
static void taskbar_pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void taskbar_pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
static void taskbar_pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
static void taskbar_pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}
#ifdef EH_HAVE_POINTER_WARP
static void taskbar_pointer_warp(void* data, wl_pointer*, wl_fixed_t sx, wl_fixed_t sy) {
  taskbar_pointer_motion(data, nullptr, 0, sx, sy);
}
#endif

static const wl_pointer_listener kTaskbarPointerListener = {
  .enter = taskbar_pointer_enter,
  .leave = taskbar_pointer_leave,
  .motion = taskbar_pointer_motion,
  .button = taskbar_pointer_button,
  .axis = taskbar_pointer_axis,
  .frame = taskbar_pointer_frame,
  .axis_source = taskbar_pointer_axis_source,
  .axis_stop = taskbar_pointer_axis_stop,
  .axis_discrete = taskbar_pointer_axis_discrete,
  .axis_value120 = taskbar_pointer_axis_value120,
  .axis_relative_direction = taskbar_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
  .warp = taskbar_pointer_warp,
#endif
};

// Tray bus setup.

static void sync_taskbar_tray_items(TaskbarApp& app) {
  auto newItems = eh::tray::TrayManager::instance().copy_items();
  std::lock_guard<std::mutex> lock(app.trayMutex);
  for (auto& it : app.trayItems) {
    if (it.pixSurface) {
      cairo_surface_destroy(it.pixSurface);
      it.pixSurface = nullptr;
    }
    it.proxy.reset();
  }
  app.trayItems = std::move(newItems);
}

static void setup_tray_bus(TaskbarApp& app) {
  try {
    app.trayBus = sdbus::createSessionBusConnection();
  } catch (const std::exception& e) {
    eh::shell_log::dbus_tray("tray bus connection failed: ", e.what());
  }
  app.trayEventFd = eh::tray::TrayManager::instance().subscribe();
  if (app.trayEventFd < 0) {
    eh::shell_log::dbus_tray("failed to subscribe to TrayManager");
    return;
  }
  sync_taskbar_tray_items(app);
}

}

// ══════════════════════════════════════════════════════════════════
// Public API
// ══════════════════════════════════════════════════════════════════

void taskbar_handle_tray(TaskbarApp& app) {
   
  uint64_t v = 0;
  while (read(app.trayEventFd, &v, sizeof(v)) > 0) {}
  sync_taskbar_tray_items(app);
  taskbar_draw(app);
}

namespace {

TaskbarNightlightToggleFn g_nightlight_toggle_fn;

}  // namespace

void taskbar_set_nightlight_toggle_fn(TaskbarNightlightToggleFn fn) {
  g_nightlight_toggle_fn = std::move(fn);
}

void taskbar_request_nightlight_toggle() {
  if (g_nightlight_toggle_fn) g_nightlight_toggle_fn();
}

bool taskbar_init_on_display(TaskbarApp& app) {
   
  MANGOWM_INFO("taskbar_init_on_display app=%p", (void*)&app);
  debug_log("taskbar", "init_on_display: BEGIN");

  // Create own isolated Wayland connection
  app.wl = std::make_unique<eh::wayland::WaylandConnection>();
  debug_log("taskbar", "init_on_display: connecting WaylandConnection");
  if (!app.wl->connect(true)) {
    std::cerr << "[taskbar] Failed to connect own wl_display\n";
    debug_log("taskbar", "init_on_display FAIL: WaylandConnection::connect returned false");
    return false;
  }

  // Extract raw pointers for backward compat with existing code
  app.display = app.wl->display();
  app.compositor = app.wl->compositor();
  app.shm = app.wl->shm();
  app.seat = app.wl->seat();
  app.layerShell = app.wl->layer_shell();
  debug_log("taskbar", "init_on_display: display=%p compositor=%p shm=%p seat=%p layerShell=%p",
            (void*)app.display, (void*)app.compositor, (void*)app.shm,
            (void*)app.seat, (void*)app.layerShell);

  // Toplevel tracking is shared from the dock's ForeignToplevels (set up in
  // UnifiedShellSession). The taskbar does NOT use its own Wayland connection
  // for toplevel tracking because wspace often invalidate handles on
  // secondary connections. The dock's tracking on the main display works
  // reliably.

  eh::shell::dock_slot_hooks::battery_widget_init();
  app.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

  if (app.seat) {
    app.pointer = wl_seat_get_pointer(app.seat);
    if (app.pointer) wl_pointer_add_listener(app.pointer, &kTaskbarPointerListener, &app);
    app.keyboard = wl_seat_get_keyboard(app.seat);
    if (app.keyboard) wl_keyboard_add_listener(app.keyboard, &kTaskbarKeyboardListener, &app);
  }

  {
    const auto& sc_init = eh::config::shell_config_snapshot_skip_matugen();
    taskbar_maybe_reload_settings(app, sc_init);
    app.icons.set_icon_theme(sc_init.dock.iconTheme);
  }
  app.enabled = app.settings.enabled;
  if (app.display) wl_display_flush(app.display);
  app.wlError = false;
  debug_log("taskbar", "init_on_display: END enabled=%d ok=1", (int)app.enabled);
  return true;
}

void taskbar_init_deferred_startup(TaskbarApp& app) {
   
  setup_tray_bus(app);

  try {
    app.mpris = std::make_unique<eh::mpris::DockMpris>();
    (void)app.mpris->poll_refresh();
  } catch (const std::exception& e) {
    eh::shell_log::dock_mpris("mpris init failed: ", e.what());
    app.mpris.reset();
  } catch (...) {
    app.mpris.reset();
  }

  eh::shell::dock_slot_hooks::bluetooth_widget_init();
}

void taskbar_add_layer(TaskbarApp& app, wl_output* output) {
   
  debug_log("taskbar", "add_layer: BEGIN output=%p compositor=%p layerShell=%p",
            (void*)output, (void*)app.compositor, (void*)app.layerShell);
  if (!app.compositor || !app.layerShell || !output) {
    debug_log("taskbar", "add_layer: missing compositor/layerShell/output, SKIP");
    return;
  }
  for (const auto& up : app.layers)
    if (up && up->wlOut == output) {
      debug_log("taskbar", "add_layer: already have layer for this output, SKIP");
      return;
    }

  auto L = std::make_unique<TaskbarOutputLayer>();
  L->taskbar = &app;
  L->wlOut = output;

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kTaskbarNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, output, cfg,
                                          &g_taskbar_layer_listener, L.get(),
                                          &surf, &layer)) {
    std::cerr << "[taskbar] failed to create layer surface\n";
    return;
  }
  L->surface = surf;
  L->layer = layer;
  if (auto* mgr = app.wl->background_effect_manager()) {
    L->bgEffect = ext_background_effect_manager_v1_get_background_effect(mgr, surf);
  }
  if (L->surface) {
    if (auto* vp = app.wl->viewporter()) {
      L->surfExt.viewport = wp_viewporter_get_viewport(vp, L->surface);
      wl_surface_set_buffer_scale(L->surface, 1);
    }
    if (auto* fsm = app.wl->fractional_scale_manager()) {
      L->surfExt.fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, L->surface);
    }
  }
  app.layers.push_back(std::move(L));

  {
    uint32_t anchor = app.settings.positionTop
        ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
        : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(layer, anchor);
  }
  if (app.settings.positionTop)
    zwlr_layer_surface_v1_set_margin(layer,
      app.settings.widthMode == 0 ? static_cast<uint32_t>(app.settings.floatingAmount) + static_cast<uint32_t>(eh::shell::taskbar::edge_gap_px(app.settings)) : static_cast<uint32_t>(eh::shell::taskbar::edge_gap_px(app.settings)), 0, 0, 0);
  else
    zwlr_layer_surface_v1_set_margin(layer, 0, 0,
      app.settings.widthMode == 0 ? static_cast<uint32_t>(app.settings.floatingAmount) + static_cast<uint32_t>(eh::shell::taskbar::edge_gap_px(app.settings)) : static_cast<uint32_t>(eh::shell::taskbar::edge_gap_px(app.settings)), 0);
  const int exZone = app.settings.enabled
      ? (app.settings.autoHide ? 0 : (app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height) + eh::shell::taskbar::exclusive_zone_gap_px(app.settings))
      : 0;
  zwlr_layer_surface_v1_set_exclusive_zone(layer, exZone);
  zwlr_layer_surface_v1_set_size(layer, 0,
    static_cast<uint32_t>(app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height));

  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);
  debug_log("taskbar", "add_layer: END surface=%p layer=%p n_layers=%zu",
            (void*)surf, (void*)layer, app.layers.size());
}

void taskbar_tooltip_tick(TaskbarApp& app);

// Frame callback (vsync alignment).

static void taskbar_frame_done(void* data, wl_callback* cb, uint32_t /*compositor_time_ms*/) {
  auto& app = *static_cast<TaskbarApp*>(data);
  wl_callback_destroy(cb);
  app.frameCallback = nullptr;
  app.frameCallbackRequestedMs = 0;
  if (app.frameRedrawPending) {
    app.frameRedrawPending = false;
    taskbar_draw(app);
  }
}

static const wl_callback_listener g_taskbar_frame_listener = {
  .done = taskbar_frame_done,
};

static wl_surface* taskbar_pick_frame_surface(TaskbarApp& app) {
  if (app.layers.empty()) return nullptr;
  if (app.pointerSurface) {
    for (const auto& up : app.layers) {
      if (up && up->surface == app.pointerSurface) return up->surface;
    }
  }
  if (app.pointerTaskbarLayerIdx < app.layers.size() && app.layers[app.pointerTaskbarLayerIdx] &&
      app.layers[app.pointerTaskbarLayerIdx]->surface)
    return app.layers[app.pointerTaskbarLayerIdx]->surface;
  return app.layers[0] ? app.layers[0]->surface : nullptr;
}

static bool taskbar_pick_frame_surface_ready(TaskbarApp& app, wl_surface* surf) {
  if (!surf) return false;
  if (app.layers.empty()) return app.configured;
  for (const auto& up : app.layers) {
    if (up && up->surface == surf)
      return up->configured && up->configuredWidth > 0 && up->configuredHeight > 0;
  }
  return app.configured;
}

static uint64_t taskbar_now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void taskbar_schedule_frame(TaskbarApp& app) {
  if (app.frameCallback) {
    // Watchdog: some compositors never fire wl_callback::done for a frame
    // requested on a surface that wasn't fully configured yet (or drop it
    // outright under certain conditions), which would otherwise permanently
    // stall every future redraw for the rest of the drag — the reorder math
    // keeps running correctly, but nothing ever gets painted or coalesced
    // again. If a callback has been outstanding unreasonably long, abandon
    // tracking it (it's harmless if it fires later) and let a fresh one be
    // requested below.
    if (app.frameCallbackRequestedMs != 0 &&
        taskbar_now_ms() - app.frameCallbackRequestedMs > 100) {
      debug_log("taskbar", "schedule_frame: watchdog — abandoning stalled frame callback");
      app.frameCallback = nullptr;
    } else {
      return;
    }
  }
  wl_surface* surf = taskbar_pick_frame_surface(app);
  if (!surf || !taskbar_pick_frame_surface_ready(app, surf)) return;
  app.frameCallback = wl_surface_frame(surf);
  app.frameCallbackRequestedMs = taskbar_now_ms();
  wl_callback_add_listener(app.frameCallback, &g_taskbar_frame_listener, &app);
  wl_surface_commit(surf);
  if (app.display) wl_display_flush(app.display);
}

// Main draw.

void taskbar_draw(TaskbarApp& app) {
   
  static uint64_t drawCallCount = 0;
  drawCallCount++;
  const auto tDraw0 = std::chrono::steady_clock::now();
  debug_log("taskbar", "draw: #%llu BEGIN enabled=%d layers=%zu vk=%p vkFailed=%d",
            (unsigned long long)drawCallCount, (int)app.enabled,
            app.layers.size(), (void*)app.taskbarVk.get(), (int)app.vkFailed);
  const auto& sc = eh::config::shell_config_snapshot();
  taskbar_maybe_reload_settings(app, sc);
  const auto tReload = std::chrono::steady_clock::now();
  const uint64_t drawId = drawCallCount;

  // Per-call diagnostic snapshot.
  const auto diag = [&](const char* stage) {
    debug_log("taskbar", "draw: #%llu %s enabled=%d layers=%zu pendingRebind=%d fastStartPh=%d deferRedraw=%d",
              (unsigned long long)drawId, stage,
              (int)app.enabled, app.layers.size(),
              (int)app.pendingOutputRebind, (int)app.taskbarFastStartDidPlaceholder,
              (int)app.deferTaskbarRedraw);
    std::cerr << "[taskbar-dbg2] #" << drawId << " " << stage
              << " enabled=" << app.enabled
              << " layers=" << app.layers.size()
              << " pendingRebind=" << app.pendingOutputRebind
              << " fastStartPh=" << app.taskbarFastStartDidPlaceholder
              << " deferRedraw=" << app.deferTaskbarRedraw
              << "\n";
  };
  diag("enter");

  if (!app.enabled) { diag("exit:disabled"); debug_log("taskbar", "draw: #%llu disabled, exit early", (unsigned long long)drawCallCount); return; }
  app.frameRedrawPending = false;
  eh::shell::dock_slot_hooks::battery_widget_poll();
  eh::shell::dock_slot_hooks::bluetooth_widget_poll();
  if (app.mpris) {
    try { (void)app.mpris->poll_refresh(); } catch (const std::exception&) {}
  }
  taskbar_tooltip_tick(app);
  if (app.layers.empty() || app.pendingOutputRebind) { diag("exit:no_layers"); return; }

  // EH_TASKBAR_FAST_START: first draw → commit cleared buffer, defer full widget paint
  if (!app.taskbarFastStartDidPlaceholder && eh_taskbar_fast_start()) {
    app.taskbarFastStartDidPlaceholder = true;
    app.deferTaskbarRedraw = true;
    for (auto& up : app.layers) {
      if (!up || !up->surface || !up->layer || !up->configured) continue;
      if (up->configuredWidth <= 0 || up->configuredHeight <= 0) continue;
      const int logW = up->configuredWidth;
      const int logH = up->configuredHeight;
      const double bufScale = up->surfExt.preferred_scale();
      const int bufW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logW) * bufScale)));
      const int bufH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logH) * bufScale)));
      if (!app.taskbarVk || !app.taskbarVk->valid()) {
        if (app.vkFailed) continue;
        if (!app.taskbarVk) app.taskbarVk = std::make_shared<eh::wayland::VulkanDisplayContext>();
        if (!app.taskbarVk->init(app.display)) { app.vkFailed = true; continue; }
      }
      if (!up->vkLayer || !up->vkLayer->valid()) {
        if (!up->vkLayer) up->vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
        if (!up->vkLayer->create(*app.taskbarVk, app.display, up->surface, bufW, bufH)) continue;
      } else {
        up->vkLayer->resize(bufW, bufH);
      }
      if (!up->glRaster.ensure(bufW, bufH)) continue;
      cairo_t* cr = up->glRaster.cairo();
      cairo_save(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cr);
      cairo_restore(cr);
      cairo_surface_flush(up->glRaster.cairo_surface());
      if (up->surfExt.viewport && up->surface) {
        wl_surface_set_buffer_scale(up->surface, 1);
        wp_viewport_set_destination(up->surfExt.viewport, logW, logH);
      }
      bool transient = false;
      if (!up->vkLayer->present_cpu_bgra(*app.taskbarVk, up->glRaster.data(), bufW, bufH, up->glRaster.stride(), &transient)) {
        if (transient) {
          up->vkLayer.reset();
        } else {
          app.vkFailed = true;
        }
        continue;
      }
      wl_surface_damage_buffer(up->surface, 0, 0, INT32_MAX, INT32_MAX);
      wl_surface_commit(up->surface);
    }
    if (app.display) wl_display_flush(app.display);
    std::cerr << "[taskbar-dbg2] #" << drawId << " fast_start_placeholder\n";
    taskbar_schedule_frame(app);
    return;
  }

  if (app.deferTaskbarRedraw) {
    app.deferTaskbarRedraw = false;
    std::cerr << "[taskbar-dbg2] #" << drawId << " clear_defer\n";
  }

  const auto& ts = app.settings;

  const double bgAlpha = static_cast<double>(ts.opacity) / 100.0;
  const bool isFloating = ts.widthMode == 0;
  const bool isFill = ts.widthMode == 2;
  const int radius = ts.radius;
  const int margin = isFloating ? ts.floatingAmount : 0;

  // Pre-measure content width for fill mode
  double fillContentW = 0;
  if (isFill) {
    const auto tM0 = std::chrono::steady_clock::now();
    fillContentW = eh::shell::taskbar::taskbar_measure_content_width(
        app, ts.leftWidgets, ts.centerWidgets, ts.rightWidgets);
    const double tM = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tM0).count();
    if (tM > 0.5) std::cerr << "[taskbar-bench] measure_content_width=" << tM << "ms\n";
  }

  for (auto& up : app.layers) {
    diag("draw_loop_top");
    if (!up || !up->surface || !up->layer || !up->configured) { diag("skip:unconfigured"); continue; }
    if (up->configuredWidth <= 0 || up->configuredHeight <= 0) { diag("skip:bad_size"); continue; }

    const int logW = up->configuredWidth;
    const int logH = up->configuredHeight;
    const int barH = isFloating ? ts.height + 6 : ts.height;
    const double bufScale = up->surfExt.preferred_scale();
    const int bufW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logW) * bufScale)));
    const int bufH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logH) * bufScale)));

    if (!app.taskbarVk || !app.taskbarVk->valid()) {
      if (app.vkFailed) { diag("skip:vkFailed"); continue; }
      if (!app.taskbarVk) app.taskbarVk = std::make_shared<eh::wayland::VulkanDisplayContext>();
      if (!app.taskbarVk->init(app.display)) { app.vkFailed = true; diag("skip:vkInitFail"); continue; }
    }
    if (!up->vkLayer || !up->vkLayer->valid()) {
      if (!up->vkLayer) up->vkLayer = std::make_unique<eh::wayland::VulkanLayerSurface>();
      if (!up->vkLayer->create(*app.taskbarVk, app.display, up->surface, bufW, bufH)) { diag("skip:vkCreateFail"); continue; }
    } else {
      up->vkLayer->resize(bufW, bufH);
    }
    if (!up->glRaster.ensure(bufW, bufH)) { diag("skip:glRasterFail"); continue; }

    cairo_t* cr = up->glRaster.cairo();
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (bufScale != 1.0) cairo_scale(cr, bufScale, bufScale);

    const bool use_panel = !isFill;
    const double barW = [&]() -> double {
      if (isFloating) return static_cast<double>(logW) - static_cast<double>(margin) * 2.0;
      if (isFill) {
        const double cw2 = fillContentW + 8.0;
        return std::clamp(cw2, 80.0, static_cast<double>(logW));
      }
      return static_cast<double>(logW);
    }();
    const double barX = isFloating ? static_cast<double>(margin)
                      : isFill   ? (static_cast<double>(logW) - barW) / 2.0
                      : 0.0;
    double barY = 0.0;

    // Auto-hide clip: reduce visible bar area based on animOffsetPx
    const double autoHideClip = app.settings.autoHide ? app.animOffsetPx : 0.0;
    double barHVisible = static_cast<double>(barH) - autoHideClip;
    if (barHVisible < 1.0) barHVisible = 0.0;
    if (ts.positionTop) {
      barHVisible = std::max(0.0, static_cast<double>(barH) - autoHideClip);
    } else {
      barY += autoHideClip;
      barHVisible = std::max(0.0, static_cast<double>(barH) - autoHideClip);
    }

    if (barW > 0 && barHVisible > 0) {
      cairo_save(cr);
      cairo_rectangle(cr, barX, barY, barW, barHVisible);
      cairo_clip(cr);

      const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);
      static uint64_t s_tb_chrome_prev = 0;
      {
        const uint64_t key = static_cast<uint64_t>(std::round(mc.accentR * 255.0)) << 16 |
                             static_cast<uint64_t>(std::round(mc.accentG * 255.0)) << 8 |
                             static_cast<uint64_t>(std::round(mc.accentB * 255.0));
        if (key != s_tb_chrome_prev) {
          s_tb_chrome_prev = key;
          debug_log("taskbar", "paint_chrome hc_native=%d hc_ok=%d matugen=%d accent=%02x%02x%02x fill=%02x%02x%02x",
                    sc.appearance.horizonColorsNative ? 1 : 0, sc.appearance.horizonColorsPaletteOk ? 1 : 0,
                    sc.appearance.matugenThemingEnabled ? 1 : 0,
                    static_cast<unsigned>(std::round(mc.accentR * 255.0)),
                    static_cast<unsigned>(std::round(mc.accentG * 255.0)),
                    static_cast<unsigned>(std::round(mc.accentB * 255.0)),
                    static_cast<unsigned>(std::round(mc.dockFillR * 255.0)),
                    static_cast<unsigned>(std::round(mc.dockFillG * 255.0)),
                    static_cast<unsigned>(std::round(mc.dockFillB * 255.0)));
        }
      }
      eh::shell::shared::paint_glass_card(cr, barX, barY, barW, barHVisible,
                                          static_cast<double>(radius), mc, bgAlpha);

      if (ts.border && ts.borderSize > 0) {
        const double bw = static_cast<double>(ts.borderSize);
        const double inset = bw * 0.5;
        const double rr = std::max(0.0, static_cast<double>(radius) - inset);
        path_rounded_rect(cr, barX + inset, barY + inset, std::max(1.0, barW - bw),
                          std::max(1.0, barHVisible - bw), rr);
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 1.0);
        cairo_set_line_width(cr, bw);
        cairo_stroke(cr);
      }

      if (!ts.leftWidgets.empty() || !ts.centerWidgets.empty() || !ts.rightWidgets.empty()) {
        g_widgetHits.clear();
        taskbar_paint_widget_bar(app, cr, barX, barY, barW, barHVisible,
                                 ts.leftWidgets, ts.centerWidgets, ts.rightWidgets,
                                 &g_widgetHits,
                                 app.hoverSlot, app.pressedSlot, app.hoverLiftPx,
                                 use_panel);
      }
      cairo_restore(cr);
    }

    cairo_restore(cr);
    cairo_surface_flush(up->glRaster.cairo_surface());
    if (up->bgEffect && barW > 0 && barHVisible > 0) {
      wl_region* rgn = wl_compositor_create_region(app.compositor);
      wl_region_add(rgn, static_cast<int32_t>(barX), static_cast<int32_t>(barY),
                    static_cast<int32_t>(barW), static_cast<int32_t>(barHVisible));
      ext_background_effect_surface_v1_set_blur_region(up->bgEffect, rgn);
      wl_region_destroy(rgn);
    }
    // Set input region to match visible bar area
    {
      wl_region* inputRgn = wl_compositor_create_region(app.compositor);
      wl_region_add(inputRgn, static_cast<int32_t>(barX), static_cast<int32_t>(barY),
                    static_cast<int32_t>(barW), static_cast<int32_t>(barHVisible));
      wl_surface_set_input_region(up->surface, inputRgn);
      wl_region_destroy(inputRgn);
    }
    if (up->surfExt.viewport && up->surface) {
      wl_surface_set_buffer_scale(up->surface, 1);
      wp_viewport_set_destination(up->surfExt.viewport, logW, logH);
    }
    bool transient = false;
    if (!up->vkLayer->present_cpu_bgra(*app.taskbarVk, up->glRaster.data(), bufW, bufH, up->glRaster.stride(), &transient)) {
      diag("skip:vkPresentFail");
      if (transient) {
        up->vkLayer.reset();
      } else {
        app.vkFailed = true;
      }
      continue;
    }
    wl_surface_damage_buffer(up->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(up->surface);
    std::cerr << "[taskbar-dbg2] #" << drawId << " committed layer " << (up.get())
              << " w=" << logW << " h=" << logH
              << " radius=" << radius << " opacity=" << ts.opacity << " iconSize=" << ts.iconSize << "\n";
  }
  if (app.display) wl_display_flush(app.display);
  const double tDrawTotal = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tDraw0).count();
  const double tReloadMs = std::chrono::duration<double, std::milli>(tReload - tDraw0).count();
  std::cerr << "[taskbar-dbg2] #" << drawId << " done draw_total=" << tDrawTotal << "ms  reload=" << tReloadMs << "ms\n";
  std::cerr << "[taskbar-bench] draw_total=" << tDrawTotal << "ms  reload=" << tReloadMs << "ms\n";
}

void taskbar_maybe_reload_settings(TaskbarApp& app, const eh::config::ShellConfig& sc) {
   
  const auto& t = sc.taskbar;

  TaskbarSettings next;
  next.enabled = t.enabled;
  next.widthMode = t.widthMode;
  next.height = t.height;
  next.radius = t.radius;
  next.opacity = t.opacity;
  next.iconSize = t.iconSize;
  next.iconSpacing = t.iconSpacing;
  next.floatingAmount = t.floatingAmount;
  next.edgeGap = t.edgeGap;
  next.exclusiveZoneGap = t.exclusiveZoneGap;
  next.scale = t.scale;
  next.leftWidgets = t.leftWidgets;
  next.centerWidgets = t.centerWidgets;
  next.rightWidgets = t.rightWidgets;
  next.pinnedApps = sc.taskbar.pinnedApps;
  next.groupApps = t.groupApps;
  next.slotPillOpacity = sc.taskbar.slotPillOpacity;
  next.autoHide = t.autoHide;
  next.tooltipsEnabled = t.tooltipsEnabled;
  next.pinnedAppsTrayPill = t.pinnedAppsTrayPill;
  next.widgetsEnabled = t.widgetsEnabled;
  next.border = t.border;
  next.borderSize = t.borderSize;
  next.positionTop = t.positionTop;
  next.outputName = t.outputName;
  // Theme follows the dock's setting (single source of truth for the shell).
  next.iconTheme = sc.dock.iconTheme;

  const bool geometryChanged =
      next.enabled        != app.settings.enabled       ||
      next.widthMode      != app.settings.widthMode      ||
      next.height         != app.settings.height        ||
      next.floatingAmount != app.settings.floatingAmount ||
      next.edgeGap        != app.settings.edgeGap        ||
      next.exclusiveZoneGap != app.settings.exclusiveZoneGap ||
      next.positionTop    != app.settings.positionTop;

  const bool visualChanged =
      next.radius             != app.settings.radius             ||
      next.opacity            != app.settings.opacity            ||
      next.iconSize           != app.settings.iconSize           ||
      next.iconSpacing        != app.settings.iconSpacing        ||
      next.scale              != app.settings.scale              ||
      next.leftWidgets        != app.settings.leftWidgets        ||
      next.centerWidgets      != app.settings.centerWidgets      ||
      next.rightWidgets       != app.settings.rightWidgets       ||
      next.pinnedApps         != app.settings.pinnedApps         ||
      next.slotPillOpacity    != app.settings.slotPillOpacity    ||
      next.autoHide           != app.settings.autoHide           ||
      next.tooltipsEnabled    != app.settings.tooltipsEnabled    ||
      next.pinnedAppsTrayPill != app.settings.pinnedAppsTrayPill ||
      next.widgetsEnabled     != app.settings.widgetsEnabled ||
      next.border             != app.settings.border ||
      next.borderSize         != app.settings.borderSize;

  const bool outputChanged = next.outputName != app.settings.outputName;

  bool autoHideChanged = next.autoHide != app.settings.autoHide;

  // Icon theme must be reconciled BEFORE any early-return below: a
  // theme-only change trips none of the geometry/visual/output flags but
  // still requires the icon cache to switch.
  if (app.icons.icon_theme() != sc.dock.iconTheme) {
    app.icons.set_icon_theme(sc.dock.iconTheme);
    app.settings.iconTheme = sc.dock.iconTheme;
  }

  if (!geometryChanged && !visualChanged && !outputChanged && !autoHideChanged) {
    static uint64_t lastSame = 0;
    const uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (nowMs - lastSame > 2000) {
      lastSame = nowMs;
      std::cerr << "[taskbar-dbg] reload: no change — geometry=" << geometryChanged << " visual=" << visualChanged
                << " radius=" << next.radius << " vs " << app.settings.radius
                << " opacity=" << next.opacity << " vs " << app.settings.opacity
                << " iconSize=" << next.iconSize << " vs " << app.settings.iconSize
                << "\n";
    }
    return;
  }

  std::cerr << "[taskbar-dbg] reload: CHANGED — geometry=" << geometryChanged << " visual=" << visualChanged
            << " radius=" << next.radius << " vs " << app.settings.radius
            << " opacity=" << next.opacity << " vs " << app.settings.opacity
            << " iconSize=" << next.iconSize << " vs " << app.settings.iconSize
            << " height=" << next.height << " vs " << app.settings.height
            << "\n";

  const bool prev_enabled = app.settings.enabled;
  const int prev_height = app.settings.height;
  const int prev_widthMode = app.settings.widthMode;
  const int prev_floatingAmount = app.settings.floatingAmount;
  const int prev_edge_gap = app.settings.edgeGap;
  const int prev_exclusive_zone_gap = app.settings.exclusiveZoneGap;
  const bool prev_positionTop = app.settings.positionTop;
  app.settings = next;
  app.enabled = app.settings.enabled;

  if (autoHideChanged) taskbar_apply_autohide_state(app, app.settings.autoHide);

  if (prev_enabled != app.enabled) {
    eh::bench::bench_write("taskbar", app.enabled ? "turn_on" : "turn_off");
  }

  if (outputChanged) {
    app.pendingOutputRebind = true;
    for (auto& up : app.layers) {
      if (!up) continue;
      up->surfExt.destroy();
      if (up->bgEffect) {
        ext_background_effect_surface_v1_destroy(up->bgEffect);
        up->bgEffect = nullptr;
      }
      up->shmBuf.destroy();
      up->shmBuf2.destroy();
      if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
      if (up->surface) wl_surface_destroy(up->surface);
    }
    app.layers.clear();
  }

  if (geometryChanged) {
    const bool heightChanged =
        (prev_widthMode == 0 ? prev_height + 6 : prev_height) !=
        (app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height);
    const bool sizeChanged =
        heightChanged ||
        prev_widthMode != app.settings.widthMode ||
        prev_floatingAmount != app.settings.floatingAmount ||
        prev_edge_gap != app.settings.edgeGap ||
        prev_exclusive_zone_gap != app.settings.exclusiveZoneGap ||
        prev_positionTop != app.settings.positionTop;
    const bool justToggle = prev_enabled != app.enabled && !sizeChanged;
    for (auto& up : app.layers) {
      if (!up || !up->layer) continue;

      if (justToggle) {
        const auto t0 = std::chrono::steady_clock::now();
        const int exZone = app.settings.enabled
            ? (app.settings.autoHide ? 0 : (app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height) + eh::shell::taskbar::exclusive_zone_gap_px(app.settings))
            : 0;
        zwlr_layer_surface_v1_set_exclusive_zone(up->layer, exZone);
        if (!app.settings.enabled && up->everConfigured) {
          // Destroy Vulkan swapchain first to remove
          // wp_linux_drm_syncobj_surface_v1 association, otherwise
          // the Cairo commit below would miss the acquire/release
          // timelines required by explicit sync.
          if (up->vkLayer) {
            up->vkLayer->destroy();
            up->vkLayer.reset();
          }
          // Don't wl_surface_attach(null) — some wspace treat
          // a null-buffer commit on a mapped layer surface as a
          // protocol error.  Instead paint a transparent buffer
          // to clear the old content.
          auto& jtBuf = !up->shmBuf.busy() ? up->shmBuf : up->shmBuf2;
          if (!jtBuf.busy() && up->configuredWidth > 0 && up->configuredHeight > 0 &&
              jtBuf.ensure(app.shm, "eh_shell_taskbar", up->configuredWidth, up->configuredHeight)) {
            cairo_t* cr = jtBuf.cairo();
            cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_surface_flush(jtBuf.cairo_surface());
            wl_surface_attach(up->surface, jtBuf.wl(), 0, 0);
            wl_surface_damage_buffer(up->surface, 0, 0, up->configuredWidth, up->configuredHeight);
            jtBuf.mark_busy();
          }
          wl_surface_commit(up->surface);
        } else if (app.settings.enabled && up->everConfigured) {
          // Surface geometry hasn't changed, only exclusive zone.
          // Don't set configured = false — the compositor may not send a
          // configure for an exclusive-zone-only change, leaving the layer
          // stuck unconfigured.  The next taskbar_draw() will attach a buffer.
          // If the compositor does send a configure, the callback updates sizes.
          wl_surface_commit(up->surface);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::cerr << "[taskbar-toggle] " << (app.settings.enabled ? "turn_on" : "turn_off") << " commit=" << ms << "ms\n";
        continue;
      }

      const uint32_t anchor = (app.settings.positionTop
          ? ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
          : ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
        | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

      zwlr_layer_surface_v1_set_anchor(up->layer, anchor);
      const uint32_t edgeMargin = static_cast<uint32_t>(eh::shell::taskbar::edge_gap_px(app.settings));
      const uint32_t floatMargin = (app.settings.widthMode == 0) ? static_cast<uint32_t>(app.settings.floatingAmount) + edgeMargin : edgeMargin;
      if (app.settings.positionTop) {
        zwlr_layer_surface_v1_set_margin(up->layer, floatMargin, 0, 0, 0);
      } else {
        zwlr_layer_surface_v1_set_margin(up->layer, 0, 0, floatMargin, 0);
      }
      const int exZone = app.settings.enabled
          ? (app.settings.autoHide ? 0 : (app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height) + eh::shell::taskbar::exclusive_zone_gap_px(app.settings))
          : 0;
      zwlr_layer_surface_v1_set_exclusive_zone(up->layer, exZone);
      if (sizeChanged) {
        zwlr_layer_surface_v1_set_size(up->layer, 0,
          static_cast<uint32_t>(app.settings.widthMode == 0 ? app.settings.height + 6 : app.settings.height));
        if (heightChanged)
          up->configured = false;
      }
      if (up->everConfigured) {
        wl_surface_commit(up->surface);
        if (app.display) wl_display_roundtrip(app.display);
      }
    }
  }
}

// Auto-hide.
void taskbar_apply_autohide_state(TaskbarApp& app, bool autoHide) {
   
  app.anim.cancel(app.slideAnimId);
  app.slideAnimId = 0;

  if (!autoHide) {
    app.reveal = true;
    app.animTargetPx = 0.0;
    app.animOffsetPx = 0.0;
    for (auto& up : app.layers) {
      if (!up || !up->layer) continue;
      zwlr_layer_surface_v1_set_exclusive_zone(up->layer, app.settings.height + eh::shell::taskbar::exclusive_zone_gap_px(app.settings));
      wl_surface_commit(up->surface);
    }
    taskbar_draw(app);
    return;
  }

  app.reveal = false;
  app.animTargetPx = static_cast<double>(app.settings.height - app.triggerHeight);
  app.animOffsetPx = app.animTargetPx;
  for (auto& up : app.layers) {
    if (!up || !up->layer) continue;
    zwlr_layer_surface_v1_set_exclusive_zone(up->layer, 0);
    wl_surface_commit(up->surface);
  }
  taskbar_draw(app);
}

void taskbar_anim_start_slide(TaskbarApp& app) {
   
  app.anim.cancel(app.slideAnimId);
  app.slideAnimId = app.anim.animate(
      static_cast<float>(app.animOffsetPx), static_cast<float>(app.animTargetPx), 360.f,
      eh::shell::Easing::EaseOutCubic,
      [&app](float v) { app.animOffsetPx = static_cast<double>(v); },
      [&app] {
        app.slideAnimId = 0;
        app.animOffsetPx = app.animTargetPx;
      });
  taskbar_draw(app);
}

void taskbar_tooltip_cancel(TaskbarApp& app) {
  app.tooltipHoverSlot = -1;
  taskbar_tooltip_destroy(app);
}

static constexpr std::chrono::milliseconds kTbTooltipDelayMs{500};
static constexpr int kTbTooltipPadH = 10;
static constexpr int kTbTooltipPadV = 5;
static constexpr int kTbTooltipFontSize = 12;
static constexpr int kTbTooltipRadius = 4;
static constexpr int kTbTooltipGap = 4;

static std::string tb_tooltip_text(int slotIdx) {
  if (slotIdx < 0 || static_cast<size_t>(slotIdx) >= g_widgetHits.size()) return {};
  const auto& hit = g_widgetHits[static_cast<size_t>(slotIdx)];
  auto desktop = find_desktop_file_for_appid(hit.widgetId);
  if (desktop) {
    auto info = read_desktop_entry_info(*desktop);
    if (info && !info->name.empty()) return info->name;
  }
  return hit.widgetId;
}

static void tb_tooltip_layer_configure(void* data, zwlr_layer_surface_v1* layer,
                                        uint32_t serial, uint32_t width, uint32_t height) {
  auto* app = static_cast<TaskbarApp*>(data);
  zwlr_layer_surface_v1_ack_configure(layer, serial);
  app->tooltipCfgW = static_cast<int>(width);
  app->tooltipCfgH = static_cast<int>(height);
  app->tooltipConfigured = true;
}

static void tb_tooltip_layer_closed(void* data, zwlr_layer_surface_v1*) {
  auto* app = static_cast<TaskbarApp*>(data);
  if (!app) return;
  app->tooltipLayer = nullptr;
  app->tooltipSurface = nullptr;
  app->tooltipConfigured = false;
}

static const zwlr_layer_surface_v1_listener kTbTooltipLayerListener = {
  .configure = tb_tooltip_layer_configure,
  .closed = tb_tooltip_layer_closed,
};

static void tb_tooltip_draw(TaskbarApp& app) {
  if (!app.tooltipConfigured || !app.tooltipSurface) return;
  cairo_t* cr = app.tooltipShm.cairo();
  if (!cr) return;

  cairo_surface_t* surf = cairo_get_target(cr);
  int w = app.tooltipShm.width();
  int h = app.tooltipShm.height();

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  double r = kTbTooltipRadius;
  cairo_new_sub_path(cr);
  cairo_arc(cr, w - r, r, r, -M_PI_2, 0);
  cairo_arc(cr, w - r, h - r, r, 0, M_PI_2);
  cairo_arc(cr, r, h - r, r, M_PI_2, M_PI);
  cairo_arc(cr, r, r, r, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);

  cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 0.9);
  cairo_fill(cr);

  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kTbTooltipFontSize);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, app.tooltipText.c_str(), &ex);
  cairo_move_to(cr, (w - ex.width) / 2.0 - ex.x_bearing,
                (h - ex.height) / 2.0 - ex.y_bearing);
  cairo_show_text(cr, app.tooltipText.c_str());

  cairo_surface_flush(surf);
  wl_surface_attach(app.tooltipSurface, app.tooltipShm.wl(), 0, 0);
  wl_surface_damage(app.tooltipSurface, 0, 0, w, h);
  wl_surface_commit(app.tooltipSurface);
  if (app.display) wl_display_flush(app.display);
}

static void tb_tooltip_create(TaskbarApp& app, const std::string& text, const TaskbarWidgetHit& hit) {
  if (!app.compositor || !app.layerShell) return;
  if (app.tooltipSurface) return;

  cairo_surface_t* measure = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  cairo_t* cr = cairo_create(measure);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, kTbTooltipFontSize);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, text.c_str(), &ex);
  int tw = static_cast<int>(std::ceil(ex.width)) + 1;
  int th = static_cast<int>(std::ceil(ex.height)) + 1;
  cairo_destroy(cr);
  cairo_surface_destroy(measure);

  int pw = tw + kTbTooltipPadH * 2;
  int ph = th + kTbTooltipPadV * 2;

  // Find the output for positioning
  wl_output* output = nullptr;
  if (app.pointerTaskbarLayerIdx < app.layers.size() && app.layers[app.pointerTaskbarLayerIdx])
    output = app.layers[app.pointerTaskbarLayerIdx]->wlOut;
  if (!output && !app.layers.empty() && app.layers[0])
    output = app.layers[0]->wlOut;
  if (!output) return;

  int centerX = static_cast<int>(hit.x + hit.w * 0.5);
  int marginLeft = std::max(0, centerX - pw / 2);

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace = eh::shell::kPopupNamespace;
  cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  cfg.anchor = app.settings.positionTop
      ? (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
      : (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
  cfg.width = static_cast<uint32_t>(pw);
  cfg.height = static_cast<uint32_t>(ph);
  cfg.exclusiveZone = 0;
  cfg.marginLeft = marginLeft;
  if (app.settings.positionTop)
    cfg.marginTop = app.settings.height + kTbTooltipGap;
  else
    cfg.marginBottom = app.settings.height + kTbTooltipGap;
  cfg.keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, output, cfg,
                                          &kTbTooltipLayerListener, &app, &surf, &layer))
    return;

  app.tooltipSurface = surf;
  app.tooltipLayer = layer;
  app.tooltipCfgW = 0;
  app.tooltipCfgH = 0;
  app.tooltipConfigured = false;
  app.tooltipText = text;
  app.tooltipShownSlot = app.tooltipHoverSlot;

  wl_surface_commit(surf);
  if (app.display) wl_display_roundtrip(app.display);

  if (!app.tooltipConfigured || app.tooltipCfgW <= 0 || app.tooltipCfgH <= 0) {
    taskbar_tooltip_destroy(app);
    return;
  }
  if (!app.tooltipShm.ensure(app.shm, "eh_taskbar_tooltip", app.tooltipCfgW, app.tooltipCfgH)) {
    taskbar_tooltip_destroy(app);
    return;
  }
  app.tooltipShm.set_release_hook(nullptr, nullptr);
  tb_tooltip_draw(app);
}

void taskbar_tooltip_tick(TaskbarApp& app) {
  if (app.tooltipHoverSlot < 0) return;
  if (!app.settings.tooltipsEnabled) return;
  if (app.tooltipShownSlot == app.tooltipHoverSlot) return;

  auto elapsed = std::chrono::steady_clock::now() - app.tooltipHoverStart;
  if (elapsed < kTbTooltipDelayMs) return;

  const int slotIdx = app.tooltipHoverSlot;
  if (static_cast<size_t>(slotIdx) >= g_widgetHits.size()) return;
  const auto& hit = g_widgetHits[static_cast<size_t>(slotIdx)];
  std::string text = tb_tooltip_text(slotIdx);
  if (text.empty()) return;

  tb_tooltip_create(app, text, hit);
}

void taskbar_cleanup(TaskbarApp& app) {
   
  debug_log("taskbar", "cleanup: BEGIN n_layers=%zu taskbarVk=%p display=%p",
            app.layers.size(), (void*)app.taskbarVk.get(), (void*)app.display);

  taskbar_popup_close(app);
  taskbar_tooltip_cancel(app);
  debug_log("taskbar", "cleanup: popup+tooltip closed");

  eh::shell::dock_slot_hooks::battery_widget_shutdown();
  eh::shell::dock_slot_hooks::bluetooth_widget_shutdown();

  // Clean up tray subscription and items
  eh::tray::TrayManager::instance().unsubscribe(app.trayEventFd);
  app.trayEventFd = -1;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    for (auto& it : app.trayItems) {
      if (it.pixSurface) {
        cairo_surface_destroy(it.pixSurface);
        it.pixSurface = nullptr;
      }
      it.proxy.reset();
    }
    app.trayItems.clear();
  }
  app.trayBus.reset();
  debug_log("taskbar", "cleanup: tray items cleared");

  // toplevels is a borrowed pointer from the dock's ForeignToplevels — do not shut it down here

  if (app.display && wl_display_get_error(app.display)) {
    debug_log("taskbar", "cleanup: display in error, skipping Wayland proxy destruction");
    app.display = nullptr;
    if (app.xkbState) { xkb_state_unref(app.xkbState); app.xkbState = nullptr; }
    if (app.xkbKeymap) { xkb_keymap_unref(app.xkbKeymap); app.xkbKeymap = nullptr; }
    if (app.xkbCtx) { xkb_context_unref(app.xkbCtx); app.xkbCtx = nullptr; }
    app.frameCallback = nullptr;
    app.frameCallbackRequestedMs = 0;
    app.frameRedrawPending = false;
    app.layers.clear();
    app.deferTaskbarRedraw = false;
    app.taskbarFastStartDidPlaceholder = false;
    if (app.taskbarVk) {
      app.taskbarVk->shutdown();
      app.taskbarVk.reset();
    }
    app.vkFailed = false;
    app.wl.reset();
    app.compositor = nullptr;
    app.shm = nullptr;
    app.seat = nullptr;
    app.layerShell = nullptr;
    debug_log("taskbar", "cleanup: END (error path)");
    return;
  }

  if (app.keyboard) { wl_keyboard_destroy(app.keyboard); app.keyboard = nullptr; }
  if (app.pointer) { wl_pointer_destroy(app.pointer); app.pointer = nullptr; }
  if (app.xkbState) { xkb_state_unref(app.xkbState); app.xkbState = nullptr; }
  if (app.xkbKeymap) { xkb_keymap_unref(app.xkbKeymap); app.xkbKeymap = nullptr; }
  if (app.xkbCtx) { xkb_context_unref(app.xkbCtx); app.xkbCtx = nullptr; }

  if (app.frameCallback) {
    wl_callback_destroy(app.frameCallback);
    app.frameCallback = nullptr;
  }
  app.frameCallbackRequestedMs = 0;
  app.frameRedrawPending = false;

  debug_log("taskbar", "cleanup: destroying %zu layer surfaces", app.layers.size());
  for (auto& up : app.layers) {
    if (!up) {
      debug_log("taskbar", "cleanup:  layer is null, skipping");
      continue;
    }
    debug_log("taskbar", "cleanup:  layer surface=%p vkLayer=%p", (void*)up->surface, (void*)up->vkLayer.get());
    if (up->vkLayer) {
      up->vkLayer->destroy();
      up->vkLayer.reset();
      debug_log("taskbar", "cleanup:  vkLayer destroyed");
    }
    debug_log("taskbar", "cleanup:  destroying glRaster");
    up->glRaster.destroy();
    debug_log("taskbar", "cleanup:  destroying surfExt");
    up->surfExt.destroy();
    if (up->bgEffect) {
      debug_log("taskbar", "cleanup:  destroying bgEffect");
      ext_background_effect_surface_v1_destroy(up->bgEffect);
    }
    if (up->layer) {
      debug_log("taskbar", "cleanup:  destroying layer surface");
      zwlr_layer_surface_v1_destroy(up->layer);
    }
    up->layer = nullptr;
    if (up->surface) {
      debug_log("taskbar", "cleanup:  destroying wl_surface");
      wl_surface_destroy(up->surface);
    }
    up->surface = nullptr;
    up->configured = false;
  }
  app.layers.clear();
  debug_log("taskbar", "cleanup: layers cleared");

  app.deferTaskbarRedraw = false;
  app.taskbarFastStartDidPlaceholder = false;

  // Vulkan cleanup
  debug_log("taskbar", "cleanup: vulkan shutdown taskbarVk=%p", (void*)app.taskbarVk.get());
  if (app.taskbarVk) {
    app.taskbarVk->shutdown();
    app.taskbarVk.reset();
    debug_log("taskbar", "cleanup: vulkan shutdown done");
  }
  app.vkFailed = false;

  // Destroy own Wayland connection last
  debug_log("taskbar", "cleanup: resetting WaylandConnection (wl=%p)", (void*)app.wl.get());
  app.wl.reset();
  app.display = nullptr;
  app.compositor = nullptr;
  app.shm = nullptr;
  app.seat = nullptr;
  app.layerShell = nullptr;
  debug_log("taskbar", "cleanup: END");
}

void taskbar_toggle_menu(TaskbarApp& app) {
  if (app.popupSurface) {
    taskbar_popup_close(app);
  } else {
    // Check if any taskbar widget is of type smenu
    bool smenuMode = false;
    auto check_smenu = [&](const std::vector<std::string>& list) {
      for (const auto& w : list) {
        if (eh::config::widget_implementation_type(w) == "smenu") {
          smenuMode = true;
          return true;
        }
      }
      return false;
    };
    check_smenu(app.settings.leftWidgets);
    check_smenu(app.settings.centerWidgets);
    check_smenu(app.settings.rightWidgets);

    app.appDrawerState = eh::appdrawer::AppDrawerState{};
    app.appDrawerState.smenuMode = smenuMode;
    app.appDrawerState.popupW = eh::appdrawer::app_drawer_popup_width();
    app.appDrawerState.popupH = eh::appdrawer::app_drawer_popup_height();
    app.appDrawerState.pinnedApps = app.settings.pinnedApps;
    app.appDrawerState.startMenuPinnedApps = app.settings.pinnedApps;
    app.appDrawerState.viewMode = eh::config::shell_config_snapshot().appearance.launchpadViewMode;
    if (smenuMode) eh::appdrawer::app_drawer_update_categories(app.appDrawerState);
    eh::shell::dock::app_drawer::invalidate_desktop_entries_cache();
    eh::appdrawer::app_drawer_refresh_hits(app.appDrawerState);
    app.popupKind = TaskbarPopupKind::AppDrawer;
    app.appDrawerPowerConfirmOpen = false;
    app.appDrawerPowerConfirmIdx = -1;
    taskbar_popup_create(app, 0, app.appDrawerState.popupW, app.appDrawerState.popupH);
  }
  if (app.display) wl_display_flush(app.display);
}

}
