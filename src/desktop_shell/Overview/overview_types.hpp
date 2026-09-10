#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct DockApp;
struct zwlr_foreign_toplevel_handle_v1;

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

  AppGridLayout appGrid{};
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

} // namespace eh::shell::overview
