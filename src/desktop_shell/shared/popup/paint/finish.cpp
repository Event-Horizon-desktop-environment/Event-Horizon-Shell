#include "desktop_shell/shared/popup/paint/finish.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/input/dock_position.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "desktop_shell/shared/popup/caret/caret.hpp"
#include "desktop_shell/dock/paint/dock_raster_backend.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

#include <iostream>
#include <wayland-client.h>

using eh::shell::dock::dock_popup_destroy_caret_frame;
using eh::shell::dock::dock_popup_queue_followup_frame;

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
  if (log_cc_first_paint && app.ccState.openBenchStartMs != 0 && !app.ccState.openBenchLoggedFirstPaint &&
      eh_cc_open_bench()) {
    app.ccState.openBenchLoggedFirstPaint = true;
    const uint64_t ms = eh::shell::monotonic_ms() - app.ccState.openBenchStartMs;
    std::cerr << "[control-center] open: to_first_buffer_commit_ms=" << ms << "\n";
  }
  wl_surface_commit(app.popupSurface);
}
