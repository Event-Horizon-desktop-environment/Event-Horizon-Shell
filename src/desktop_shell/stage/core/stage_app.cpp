// horizon-stage core — brand-new lifecycle for the extracted stage process.
// Own Wayland connection, own toplevel tracking, overview Host wiring.

#include "desktop_shell/stage/core/stage_app.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/dashboard/dashboard_surface.hpp"

#include <iostream>
#include <wayland-client.h>

namespace eh::shell::stage {

namespace {

void stage_dash_frame_done(void* data, wl_callback* cb, std::uint32_t /*time*/) {
  auto& app = *static_cast<StageApp*>(data);
  wl_callback_destroy(cb);
  app.dashFrameCb = nullptr;
  app.shellAnim.tick();
  eh::shell::dashboard::dashboard_frame_draw(app);
  if (app.shellAnim.has_active() && app.dash.panelSurface && !app.dashFrameCb) {
    app.dashFrameCb = wl_surface_frame(app.dash.panelSurface);
    static const wl_callback_listener kListener = {.done = stage_dash_frame_done};
    wl_callback_add_listener(app.dashFrameCb, &kListener, &app);
    wl_surface_commit(app.dash.panelSurface);
    if (app.display) wl_display_flush(app.display);
  }
}

}  // namespace

void stage_dashboard_kick_frames(StageApp& app) {
  if (app.dashFrameCb || !app.shellAnim.has_active() || !app.dash.panelSurface) return;
  app.dashFrameCb = wl_surface_frame(app.dash.panelSurface);
  if (!app.dashFrameCb) return;
  static const wl_callback_listener kListener = {.done = stage_dash_frame_done};
  wl_callback_add_listener(app.dashFrameCb, &kListener, &app);
  wl_surface_commit(app.dash.panelSurface);
  if (app.display) wl_display_flush(app.display);
}

void stage_build_overview_ctx(StageApp& app) {
  auto& c = app.overviewCtx;
  c.compositorKind = app.compositorKind;
  c.workspaceStrip = &app.workspaceStrip;
  c.workspaceStripLastPoll = &app.workspaceStripLastPoll;
  c.toplevels = &app.toplevels;
  c.primaryOutputWidthPx = &app.primaryOutputWidthPx;
  c.primaryOutputHeightPx = &app.primaryOutputHeightPx;
  c.uiScale = [] {
    return std::clamp(eh::config::shell_config_snapshot().dock.shellUiScale, 0.5, 2.0);
  };
  c.bottomReserve = [] { return 0; };
  c.icons = &app.icons;
  c.seat = app.seat;
}

bool stage_init_on_display(StageApp& app) {
  app.wl = std::make_unique<eh::wayland::WaylandConnection>();
  if (!app.wl->connect(true)) {
    std::cerr << "[stage] Failed to connect own wl_display\n";
    return false;
  }
  app.display = app.wl->display();
  app.compositor = app.wl->compositor();
  app.shm = app.wl->shm();
  app.seat = app.wl->seat();
  app.layerShell = app.wl->layer_shell();
  app.wlError = false;

  stage_refresh_outputs(app);

  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  app.icons.set_icon_theme(sc.dock.iconTheme);
  return true;
}

void stage_refresh_outputs(StageApp& app) {
  if (!app.wl || app.wlError) return;
  app.stageOutputs.clear();
  int originW = 0, originH = 0, maxW = 0, maxH = 0;
  for (const auto& out : app.wl->logical_output_bounds()) {
    if (out.output) app.stageOutputs.push_back(out.output);
    if (out.global_x <= 0 && out.global_y <= 0 && out.width > 0) {
      originW = out.width;
      originH = out.height;
    }
    if (out.width > maxW) {
      maxW = out.width;
      maxH = out.height;
    }
  }
  // Prefer the origin output; fall back to the largest.
  const int bestW = originW > 0 ? originW : maxW;
  const int bestH = originW > 0 ? originH : maxH;
  if (bestW > 0) app.primaryOutputWidthPx = bestW;
  if (bestH > 0) app.primaryOutputHeightPx = bestH;
}

void stage_cleanup(StageApp& app) {
  if (app.dashFrameCb) {
    wl_callback_destroy(app.dashFrameCb);
    app.dashFrameCb = nullptr;
  }
  app.overview.reset();
  app.toplevels.shutdown();
  app.wl.reset();
  app.display = nullptr;
}

}  // namespace eh::shell::stage
