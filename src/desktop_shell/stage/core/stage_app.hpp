#pragma once

// horizon-stage application state — brand-new host for the extracted stage
// surfaces (overview; dashboard and OSD follow in later phases).
//
// The stage owns its own Wayland connection plus a second one handed to the
// overview Host (mirroring how the dock used to host it), its own toplevel
// tracking, workspace strip, icon cache, and the ShellCtx the overview Host
// consumes. Nothing here is shared with the dock: killing horizon-dock no
// longer takes the overview down with it.

#include "desktop_shell/Overview/overview_types.hpp"

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/workspace/workspace_strip_types.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/dashboard/dashboard_types.hpp"
#include "desktop_shell/osd/host/osd_host.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"
#include "services/mpris/mpris_player.hpp"
#include "wl/core/connection.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"
#include "wl/toplevel/ext_foreign_toplevels.hpp"

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct zwlr_layer_shell_v1;

namespace eh::shell::overview {
class Host;
}

namespace eh::shell::stage {

struct StageApp {
  // Own Wayland connection (outputs, seat, toplevel tracking).
  std::unique_ptr<eh::wayland::WaylandConnection> wl;
  bool wlError = false;
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_seat* seat = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;

  // Toplevel tracking for the overview window lists.
  eh::wayland::ForeignToplevels toplevels{};
  eh::wayland::ExtForeignToplevels* extToplevels = nullptr;
  bool toplevelsBound = false;

  // Workspace strip for the overview grid.
  CompositorKind compositorKind = CompositorKind::Unknown;
  std::vector<eh::shell::WorkspaceStripEntry> workspaceStrip{};
  timespec workspaceStripLastPoll{};

  eh::icons::IconCache icons{};

  // OSD host (extracted from the dock child).
  std::unique_ptr<eh::shell::osd::OsdHost> osd{};

  // Dashboard state + services (extracted from the dock child).
  eh::shell::dashboard::DashboardState dash{};
  std::unique_ptr<eh::mpris::DockMpris> mpris{};
  eh::shell::AnimationManager shellAnim{};
  double pointerX = 0.0;
  double pointerY = 0.0;
  wl_surface* pointerSurface = nullptr;
  std::string weatherInstanceId{};
  // Output list for dashboard surface placement (refreshed on the poll tick
  // from the connection's logical bounds).
  std::vector<wl_output*> stageOutputs{};

  // Pending dashboard animation frame callback (drives shellAnim ticks +
  // dashboard_frame_draw until the animation settles; null when idle).
  wl_callback* dashFrameCb = nullptr;

  // Primary output size for overview capture fallbacks (refreshed on the
  // poll tick from the connection's logical bounds).
  int primaryOutputWidthPx = 0;
  int primaryOutputHeightPx = 0;
  int bottomReservePx = 0;

  // Shell context handed to the overview Host (points into the above).
  eh::shell::overview::ShellCtx overviewCtx{};
  std::unique_ptr<eh::shell::overview::Host> overview;

  bool running = true;
};

void stage_build_overview_ctx(StageApp& app);
bool stage_init_on_display(StageApp& app);
void stage_refresh_outputs(StageApp& app);
void stage_cleanup(StageApp& app);
// Chains dashboard animation frames: ticks shellAnim and repaints the panel
// until no animation is active. Safe to call any time (no-ops when idle or
// when the panel surface is gone).
void stage_dashboard_kick_frames(StageApp& app);

}  // namespace eh::shell::stage
