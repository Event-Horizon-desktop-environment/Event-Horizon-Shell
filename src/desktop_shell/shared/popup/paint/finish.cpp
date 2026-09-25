#include "desktop_shell/shared/popup/paint/finish.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/input/dock_position.hpp"
#include "desktop_shell/controlcenter/layout/control_center_layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_pear_layout.hpp"
#include "desktop_shell/controlcenter/state/control_center_pear_config.hpp"
#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"
#include "desktop_shell/controlcenter/debug/control_center_log.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "desktop_shell/shared/popup/caret/caret.hpp"
#include "desktop_shell/dock/paint/dock_raster_backend.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

#include <iostream>
#include <wayland-client.h>

using eh::shell::dock::dock_popup_destroy_caret_frame;
using eh::shell::dock::dock_popup_queue_followup_frame;

namespace {
// Reentrancy guard: the settle check below reopens + redraws, which nests
// back through here. Sizes match after reopen so one level is enough.
bool g_ccSettleResize = false;
} // namespace

void popup_finish_draw(DockApp& app, bool vk_path, bool queue_caret_followup, bool log_cc_first_paint) {

  dock_popup_sync_layer_margins_if_open(app);
  const int pw = app.popupW;
  const int ph = app.popupH;
  const bool shm_popup = app.dockRendererBackend != eh::config::ShellRendererBackend::Vulkan;
  (void)vk_path;
  if (!shm_popup) cairo_surface_flush(app.popupGlRaster.cairo_surface());
  dock_popup_destroy_caret_frame(app);
  bool transient = false;
  bool presented = false;
  if (shm_popup) {
    presented = eh::dock::present_popup_cairo_raster(app, pw, ph);
  } else {
    presented = eh::dock::present_popup_vk_raster(app, pw, ph, &transient);
    if (!presented && transient) app.deferDockRedraw = true;
  }
  if (!presented) return;
  if (queue_caret_followup) dock_popup_queue_followup_frame(app);
  // Control center: if content outgrew the surface after it settled (expand
  // animations finished, late Wi-Fi/BT scan rows, error/prompt changes),
  // re-create at the new resting height. Debounced; animations in flight are
  // left alone. Reentrancy-safe: after reopen sizes match so this fires once.
  if (app.popupKind == DockApp::PopupKind::ControlCenter && !g_ccSettleResize) {
    namespace ccl = eh::shell::dock::control_center;
    auto& cs = app.ccState;
    const uint64_t nowMs = eh::shell::monotonic_ms();
    const bool settled =
        cs.netAnimStartMs == 0 && cs.btAnimStartMs == 0 && cs.weatherAnimStartMs == 0;
    if (settled) {
      // Height must come from the same layout the popup paints with:
      // comparing a PearCenter surface against the legacy height would
      // recreate the popup on nearly every draw (visible flicker).
      const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
      std::string wid = dock_active_control_center_widget_id(app);
      if (wid.empty()) wid = "control_center";
      int wantH;
      if (ccl::pear_layout_enabled(sc, wid)) {
        const auto cfg = ccl::pear_center_config(sc, wid);
        wantH = static_cast<int>(std::ceil(
            ccl::cc_compute_pear_layout(static_cast<double>(app.popupW), cs, cfg,
                                        dock_ui_scale(sc.dock))
                .totalH));
      } else {
        wantH = static_cast<int>(std::ceil(
            ccl::cc_compute_layout(static_cast<double>(app.popupW), cs, nowMs, true).totalH));
      }
      if (wantH != app.popupH && nowMs - cs.ccLastSettleResizeMs > 800) {
        cs.ccLastSettleResizeMs = nowMs;
        ccl::cc_log("settle-resize surface=" + std::to_string(app.popupH) +
                    " want=" + std::to_string(wantH));
        g_ccSettleResize = true;
        popup_open_control_center(app, app.popupAnchorX, 0, false);
        popup_draw_surface(app);
        wl_display_flush(app.display);
        g_ccSettleResize = false;
      }
    }
  }
  if (log_cc_first_paint && app.ccState.openBenchStartMs != 0 && !app.ccState.openBenchLoggedFirstPaint &&
      eh_cc_open_bench()) {
    app.ccState.openBenchLoggedFirstPaint = true;
    const uint64_t ms = eh::shell::monotonic_ms() - app.ccState.openBenchStartMs;
    std::cerr << "[control-center] open: to_first_buffer_commit_ms=" << ms << "\n";
  }
  wl_surface_commit(app.popupSurface);
}
