#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "desktop_shell/common/workspace/workspace_strip_types.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/unified/compositor_kind.hpp"

struct wl_seat;
struct zwlr_foreign_toplevel_handle_v1;

namespace eh::wayland {
class ForeignToplevels;
}
namespace eh::icons {
class IconCache;
}

namespace eh::shell::overview {

enum class OverviewAxis : std::uint8_t { Vertical, Horizontal };

struct WorkspaceCapture {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> bgra;

  int thumbW = 0;
  int thumbH = 0;
  std::vector<uint8_t> thumbBgra;
};

struct OverviewWindow {
  std::string addr;
  std::string appId;
  std::string title;

  double x = 0, y = 0, w = 0, h = 0;
  double wx = 0, wy = 0, ww = 0, wh = 0;

  bool floating = false;
  bool focused = false;
  bool special = false;

  zwlr_foreign_toplevel_handle_v1* handle = nullptr;

  bool liveValid = false;
  int liveW = 0;
  int liveH = 0;
  std::vector<uint8_t> liveBgra;
};

struct OverviewWorkspace {
  int id = 0;
  std::string label;
  std::string monitor;
  double monX = 0, monY = 0, monW = 0, monH = 0;
  bool active = false;
  bool occupied = false;
  std::vector<OverviewWindow> windows;
  std::shared_ptr<const WorkspaceCapture> capture;
  bool overflow = false;
};

struct OverviewCardRect {
  double x = 0, y = 0, w = 0, h = 0;
};

// GNOME-style app folder: a named bucket of apps shown as one tile in the
// launchpad grid. Auto-built from freedesktop categories (see tahoe buckets);
// single-app buckets stay loose so the grid never shows one-item folders.
struct AppFolder {
  std::string id;
  std::string name;
  std::vector<SpotlightHit> apps;
};

// One visible cell of the launchpad grid: either a folder tile or an app.
struct GridItem {
  bool folder = false;
  size_t index = 0; // into Host folders_ (folder) or apps_ (app)
};

// Stable identity for shuffle animation ("a:"+desktop path / "f:"+folder id).
[[nodiscard]] inline std::string grid_item_key(bool folder, const std::string& id) {
  return (folder ? "f:" : "a:") + id;
}

// Snapshot of pre-drop cell origins for the reorganize animation.
struct GridShuffle {
  std::string key;
  double fromX = 0.0;
  double fromY = 0.0;
};

// Per-frame drag/animate state for one grid paint. Default = static grid.
struct GridDragState {
  int dragIdx = -1;      // item lifted under the pointer (slot left empty)
  double ghostX = 0.0;   // top-left of the floating ghost cell
  double ghostY = 0.0;
  // Reorder insertion marker (dock-style): vertical bar at the gap where
  // the dragged icon will land. Only for edge drops, never for folders.
  bool insertMark = false;
  double insX = 0.0;
  double insY = 0.0;
  double insH = 0.0;
  const std::vector<GridShuffle>* shuffle = nullptr;
  uint64_t shuffleStartMs = 0;
  uint64_t nowMs = 0;
};

struct AppGridLayout {
  double startX = 0, startY = 0;
  double cellW = 0, cellH = 0;
  double iconSize = 0;
  double fontSize = 0;
  int cols = 0;
  int rows = 0;
  // Vertical overflow beyond the visible rows (0 when everything fits).
  double scrollMax = 0;
};

struct QuickSelectLayout {
  double stripX = 0, stripY = 0, stripW = 0, stripH = 0;
  double thumbW = 0, thumbH = 0;
  double thumbGap = 8;
  double thumbRadius = 8;
  double padX = 16, padY = 12;
  bool horizontal = true;

  // "Add workspace" slot rendered after the last thumbnail. Dropping a
  // dragged window here creates a new workspace and moves it there.
  OverviewCardRect addRect{};
};

// hovered_qs / drop sentinel for the strip's add-workspace slot.
inline constexpr int kQuickSelectAddIdx = -2;

struct OverviewLayout {
  OverviewAxis axis = OverviewAxis::Vertical;
  double w = 0, h = 0;
  double uiScale = 1.0;

  double scale = 0.5;
  double cardW = 0, cardH = 0;
  double cardGap = 24;
  double pitch = 0;

  double stripCenterX = 0, stripCenterY = 0;

  double closeBtnSz = 34;

  double searchY = 20, searchW = 420, searchH = 40;

  // "Show all apps" pill (workspace view only). Positioned by
  // compute_overview_layout: just above the quick-select strip when the
  // strip is at the bottom, bottom-centered when the strip is on the left.
  OverviewCardRect appsBtn{};

  AppGridLayout appGrid{};
  // Folder modal grid (open folder popup): fixed 4 columns, up to 4 visible
  // rows, own scroll. Computed only while a folder is open.
  AppGridLayout modalGrid{};
  QuickSelectLayout qs{};
};

struct OverviewColors {
  double bgR = 0.102, bgG = 0.102, bgB = 0.125;
  double searchBgR = 0.165, searchBgG = 0.165, searchBgB = 0.196;
  double thumbBgR = 0.184, thumbBgG = 0.184, thumbBgB = 0.220;
  double thumbActiveR = 0.208, thumbActiveG = 0.208, thumbActiveB = 0.247;
  double thumbActiveBorderR = 0.431, thumbActiveBorderG = 0.620, thumbActiveBorderB = 1.0;
  double wsBgR = 0.133, wsBgG = 0.133, wsBgB = 0.157;
  double glassBgR = 0.036, glassBgG = 0.036, glassBgB = 0.044;
  double winBgR = 0.145, winBgG = 0.145, winBgB = 0.169;
  double winTitleBgR = 0.122, winTitleBgG = 0.122, winTitleBgB = 0.141;
  double closeBtnBgR = 0.235, closeBtnBgG = 0.235, closeBtnBgB = 0.267;
  double dashBgR = 0.118, dashBgG = 0.118, dashBgB = 0.141;
  double accentR = 0.431, accentG = 0.620, accentB = 1.0;
  double fgR = 0.867, fgG = 0.867, fgB = 0.878;
  double dimFgR = 0.627, dimFgG = 0.627, dimFgB = 0.647;
};

struct PendingActivation {
  bool active = false;
  int wsId = -1;
  std::string addr;
  bool moveCursor = false;
  int cursorX = 0;
  int cursorY = 0;
  zwlr_foreign_toplevel_handle_v1* handle = nullptr;
};

// Shell-provided context for the overview Host. The overview used to run
// inside the dock process against DockApp; it now runs in horizon-stage (or
// anywhere else) against this narrow context instead. Owners keep the
// pointed-to state alive for the Host's lifetime and refresh the scalars on
// output/config change.
struct ShellCtx {
  CompositorKind compositorKind = CompositorKind::Unknown;
  std::vector<eh::shell::WorkspaceStripEntry>* workspaceStrip = nullptr;
  timespec* workspaceStripLastPoll = nullptr;
  eh::wayland::ForeignToplevels* toplevels = nullptr;
  int* primaryOutputWidthPx = nullptr;
  int* primaryOutputHeightPx = nullptr;
  // Live UI scale (dock: dock_ui_scale(settings); stage: global shell scale).
  std::function<double()> uiScale = [] { return 1.0; };
  // Bottom screen reserve the overview must leave alone (dock: bar height +
  // spacing; stage: none — the overview covers the full output).
  std::function<int()> bottomReserve = [] { return 0; };
  eh::icons::IconCache* icons = nullptr;
  wl_seat* seat = nullptr;
};

// Gap kept above the dock bar by the overview grid (dock-reserve unit).
inline constexpr double kDockSpacingPx = 25.0;

// "Show all apps" pill metrics (logical px, scaled by uiScale). In horizontal
// mode the quick-select strip is lifted by the full reserve so the pill sits
// below it, exactly like the approved HTML mock.
inline constexpr double kAppsBtnHPx = 38.0;
inline constexpr double kAppsBtnGapPx = 10.0;
inline constexpr double kAppsBtnBottomPx = 12.0;

} // namespace eh::shell::overview
