#include "desktop_shell/Overview/overview_actions.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"

#include <algorithm>

namespace eh::shell::overview {

Actions::Actions(Host& host) : host_(host) {}

void Actions::build_nav_windows() {
  nav_windows_.clear();
  const auto& workspaces = host_.workspaces();
  for (int ws = 0; ws < static_cast<int>(workspaces.size()); ++ws) {
    for (int wi = 0; wi < static_cast<int>(workspaces[static_cast<size_t>(ws)].windows.size()); ++wi) {
      const auto& win = workspaces[static_cast<size_t>(ws)].windows[static_cast<size_t>(wi)];
      if (win.special) continue;
      nav_windows_.emplace_back(ws, wi);
    }
  }
}

void Actions::select_workspace(int wsIndex) {
  const auto& workspaces = host_.workspaces();
  if (wsIndex < 0 || wsIndex >= static_cast<int>(workspaces.size())) return;
  const auto& ws = workspaces[static_cast<size_t>(wsIndex)];
  auto& dock = host_.dock();
  if (dock.compositorKind == CompositorKind::Hyprland) {
    pending_activation_ = std::make_unique<PendingActivation>();
    pending_activation_->active = true;
    pending_activation_->wsId = ws.id;
  } else {
    eh::shell::WorkspaceStripEntry e;
    e.id = ws.id;
    e.label = ws.label;
    e.active = ws.active;
    e.occupied = ws.occupied;
    eh::widgets::workspace_activate_entry(e, dock.compositorKind);
  }
  host_.close();
}

void Actions::activate_window(int flatIdx) {
  if (flatIdx < 0 || flatIdx >= static_cast<int>(nav_windows_.size())) return;
  const auto [wsIdx, winIdx] = nav_windows_[static_cast<size_t>(flatIdx)];
  const auto& workspaces = host_.workspaces();
  if (wsIdx < 0 || wsIdx >= static_cast<int>(workspaces.size())) return;
  const auto& ws = workspaces[static_cast<size_t>(wsIdx)];
  const auto& win = ws.windows[static_cast<size_t>(winIdx)];

  pending_activation_ = std::make_unique<PendingActivation>();
  pending_activation_->active = true;
  pending_activation_->wsId = ws.id;
  pending_activation_->addr = win.addr;
  pending_activation_->handle = win.handle;
  if (win.w > 0 && win.h > 0) {
    pending_activation_->cursorX = static_cast<int>(std::lround(win.x + win.w * 0.5));
    pending_activation_->cursorY = static_cast<int>(std::lround(win.y + win.h * 0.5));
    pending_activation_->moveCursor = true;
  }
  host_.close();
}

void Actions::close_window(int flatIdx) {
  if (flatIdx < 0 || flatIdx >= static_cast<int>(nav_windows_.size())) return;
  const auto [wsIdx, winIdx] = nav_windows_[static_cast<size_t>(flatIdx)];
  const auto& workspaces = host_.workspaces();
  if (wsIdx < 0 || wsIdx >= static_cast<int>(workspaces.size())) return;
  const auto& win = workspaces[static_cast<size_t>(wsIdx)].windows[static_cast<size_t>(winIdx)];
  auto& dock = host_.dock();
  if (dock.compositorKind == CompositorKind::Hyprland && !win.addr.empty()) {
    (void)hyprland_ipc_send("dispatch hl.dsp.window.close({window = \"address:" + win.addr + "\"})");
  } else if (win.handle) {
    zwlr_foreign_toplevel_handle_v1_close(win.handle);
  }
  host_.set_data_dirty();
  host_.refresh_workspace_data();
}

void Actions::move_window_to_workspace(int flatIdx, int targetWs) {
  if (flatIdx < 0 || flatIdx >= static_cast<int>(nav_windows_.size())) return;
  auto& workspaces = host_.workspaces_mut();
  if (targetWs < 0 || targetWs >= static_cast<int>(workspaces.size())) return;
  const auto [wsIdx, winIdx] = nav_windows_[static_cast<size_t>(flatIdx)];
  if (wsIdx < 0 || wsIdx >= static_cast<int>(workspaces.size())) return;
  const auto& win = workspaces[static_cast<size_t>(wsIdx)].windows[static_cast<size_t>(winIdx)];
  const auto& target = workspaces[static_cast<size_t>(targetWs)];
  auto& dock = host_.dock();

  if (dock.compositorKind == CompositorKind::Hyprland && !win.addr.empty()) {
    (void)hyprland_ipc_send("dispatch hl.dsp.window.move({workspace = " +
                            std::to_string(target.id) + ", window = \"address:" + win.addr + "\"})");
    host_.set_data_dirty();
    debug_log("overview", "move_window_to_workspace: %s -> ws %d (kind=%d)",
              win.addr.c_str(), target.id, static_cast<int>(dock.compositorKind));
    const int srcWsId = workspaces[static_cast<size_t>(wsIdx)].id;
    const int tgtWsId = target.id;
    host_.refresh_workspace_data();
    for (auto& ws : workspaces) {
      if ((ws.id == srcWsId || ws.id == tgtWsId) && ws.capture) {
        debug_log("overview", "move_window_to_workspace: dropped capture for ws %d", ws.id);
        ws.capture.reset();
      }
    }
    host_.set_recapture();
  } else {
    eh::shell::WorkspaceStripEntry e;
    e.id = target.id;
    e.label = target.label;
    e.active = target.active;
    e.occupied = target.occupied;
    eh::widgets::workspace_activate_entry(e, dock.compositorKind);
    if (win.handle) zwlr_foreign_toplevel_handle_v1_activate(win.handle, dock.seat);
    host_.close();
  }
}

void Actions::move_window_to_new_workspace(int flatIdx) {
  if (flatIdx < 0 || flatIdx >= static_cast<int>(nav_windows_.size())) return;
  auto& workspaces = host_.workspaces_mut();
  const auto [wsIdx, winIdx] = nav_windows_[static_cast<size_t>(flatIdx)];
  if (wsIdx < 0 || wsIdx >= static_cast<int>(workspaces.size())) return;
  const auto& win = workspaces[static_cast<size_t>(wsIdx)].windows[static_cast<size_t>(winIdx)];
  auto& dock = host_.dock();
  if (dock.compositorKind != CompositorKind::Hyprland || win.addr.empty()) return;

  // Always append a brand-new workspace past the highest known id; the compositor
  // creates it on demand when the window is dispatched there.
  int maxId = 0;
  for (const auto& ws : workspaces) maxId = std::max(maxId, ws.id);
  OverviewWorkspace ph{};
  ph.id = maxId + 1;
  ph.label = std::to_string(ph.id);
  workspaces.push_back(std::move(ph));
  move_window_to_workspace(flatIdx, static_cast<int>(workspaces.size()) - 1);
}

void Actions::swap_windows_in_place(int flatIdx, int targetFlatIdx) {
  if (flatIdx < 0 || flatIdx >= static_cast<int>(nav_windows_.size())) return;
  if (targetFlatIdx < 0 || targetFlatIdx >= static_cast<int>(nav_windows_.size())) return;
  if (flatIdx == targetFlatIdx) return;
  const auto [wsIdxA, winIdxA] = nav_windows_[static_cast<size_t>(flatIdx)];
  const auto [wsIdxB, winIdxB] = nav_windows_[static_cast<size_t>(targetFlatIdx)];
  auto& workspaces = host_.workspaces_mut();
  if (wsIdxA < 0 || wsIdxA >= static_cast<int>(workspaces.size())) return;
  if (wsIdxB < 0 || wsIdxB >= static_cast<int>(workspaces.size())) return;
  if (wsIdxA != wsIdxB) return;
  const auto& winA = workspaces[static_cast<size_t>(wsIdxA)].windows[static_cast<size_t>(winIdxA)];
  const auto& winB = workspaces[static_cast<size_t>(wsIdxB)].windows[static_cast<size_t>(winIdxB)];
  if (winA.addr.empty() || winB.addr.empty()) return;

  auto& dock = host_.dock();
  if (dock.compositorKind == CompositorKind::Hyprland) {
    debug_log("overview", "swap_windows_in_place: %s <-> %s", winA.addr.c_str(), winB.addr.c_str());
    (void)hyprland_ipc_send("dispatch hl.dsp.window.swap({window = \"address:" + winA.addr +
                            "\", target = \"address:" + winB.addr + "\"})");
    host_.set_data_dirty();
    const int wsId = workspaces[static_cast<size_t>(wsIdxA)].id;
    host_.refresh_workspace_data();
    for (auto& ws : workspaces) {
      if (ws.id == wsId && ws.capture) {
        debug_log("overview", "swap_windows_in_place: dropped capture for ws %d", wsId);
        ws.capture.reset();
      }
    }
    host_.set_recapture();
  }
}

void Actions::run_pending_activation() {
  if (!pending_activation_ || !pending_activation_->active) return;
  const PendingActivation p = std::move(*pending_activation_);
  pending_activation_.reset();

  auto& dock = host_.dock();
  const bool hypr = (dock.compositorKind == CompositorKind::Hyprland);

  if (hypr && p.wsId > 0) {
    const auto r = hyprland_ipc_request("dispatch hl.dsp.focus({workspace = " + std::to_string(p.wsId) +
                                        "})");
    if (!r || r->empty() || r->find("ok") != 0)
      debug_log("overview", "focus workspace %d failed: %s",
                p.wsId, r ? r->c_str() : "<no reply>");
  }

  if (hypr && !p.addr.empty()) {
    const auto f = hyprland_ipc_request("dispatch hl.dsp.focus({window = \"address:" + p.addr +
                                        "\"})");
    if (!f || f->empty() || f->find("ok") != 0)
      debug_log("overview", "focus window %s failed: %s",
                p.addr.c_str(), f ? f->c_str() : "<no reply>");
    if (p.moveCursor)
      (void)hyprland_ipc_request("dispatch hl.dsp.cursor.move({x = " + std::to_string(p.cursorX) +
                                 ", y = " + std::to_string(p.cursorY) + "})");
    return;
  }

  if (p.handle && dock.toplevels.find(p.handle) != nullptr) {
    zwlr_foreign_toplevel_handle_v1_activate(p.handle, dock.seat);
    if (host_.wl()) wl_display_flush(host_.wl()->display());
  }
}

} // namespace eh::shell::overview
