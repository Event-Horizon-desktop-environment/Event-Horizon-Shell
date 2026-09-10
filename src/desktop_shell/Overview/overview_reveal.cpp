#include "desktop_shell/Overview/overview_reveal.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/Overview/overview_capture.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/dock/core/dock_app.h"

#include <cstdio>

namespace eh::shell::overview {

constexpr uint64_t kRevealRevealMs = 500;
constexpr uint64_t kRevealCaptureWaitMs = 900;
constexpr uint64_t kRevealRestoreMs = 300;
constexpr uint64_t kRevealNoTargetRecheckMs = 1000;
constexpr int kRevealMaxAttempts = 2;

RevealPass::RevealPass(Host& host) : host_(host) {}

void RevealPass::reset() {
  stage = Stage::Idle;
  deadline_ms = 0;
  targetAddr.clear();
  wsId = -1;
  origAddr.clear();
  attempts = 0;
  next_ms_ = 0;
}

bool RevealPass::query_cursor_pos(int& outX, int& outY) const {
  auto resp = hyprland_ipc_request("cursorpos");
  if (!resp) return false;
  return std::sscanf(resp->c_str(), "%d, %d", &outX, &outY) == 2 ||
         std::sscanf(resp->c_str(), "%d %d", &outX, &outY) == 2;
}

bool RevealPass::pick_and_focus() {
  for (const auto& ws : host_.workspaces()) {
    if (!ws.active || !ws.overflow) continue;
    if (ws.monW <= 0.0 || ws.monH <= 0.0) continue;
    for (const auto& w : ws.windows) {
      if (w.addr.empty() || w.special || w.liveValid) continue;
      const bool intersects = w.x + w.w > ws.monX && w.x < ws.monX + ws.monW &&
                              w.y + w.h > ws.monY && w.y < ws.monY + ws.monH;
      if (intersects) continue;

      std::string orig;
      for (const auto& o : ws.windows)
        if (o.focused && !o.addr.empty()) { orig = o.addr; break; }
      targetAddr = w.addr;
      wsId = ws.id;
      origAddr = orig;
      attempts = 0;

      int cx = 0, cy = 0;
      const bool cursorKnown = query_cursor_pos(cx, cy);
      const auto r = hyprland_ipc_request("dispatch hl.dsp.focus({window = \"address:" + w.addr +
                                          "\"})");
      if (!r || r->empty() || r->find("ok") != 0) {
        debug_log("overview", "reveal: focus %s failed (%s) — skipping",
                  w.addr.c_str(), r ? r->c_str() : "<no reply>");
        targetAddr.clear();
        continue;
      }
      if (cursorKnown)
        (void)hyprland_ipc_request("dispatch hl.dsp.cursor.move({x = " + std::to_string(cx) +
                                   ", y = " + std::to_string(cy) + "})");
      debug_log("overview", "reveal: focusing off-screen window %s (ws %d, restore=%s)",
                w.addr.c_str(), ws.id, origAddr.empty() ? "<workspace>" : origAddr.c_str());
      return true;
    }
  }
  return false;
}

bool RevealPass::target_captured() const {
  for (const auto& ws : host_.workspaces())
    for (const auto& w : ws.windows)
      if (w.addr == targetAddr) return w.liveValid;
  return true;
}

void RevealPass::restore_focus() {
  if (host_.dock().compositorKind != CompositorKind::Hyprland) return;
  int cx = 0, cy = 0;
  const bool cursorKnown = query_cursor_pos(cx, cy);
  if (!origAddr.empty()) {
    (void)hyprland_ipc_request("dispatch hl.dsp.focus({window = \"address:" + origAddr + "\"})");
  } else if (wsId > 0) {
    (void)hyprland_ipc_request("dispatch hl.dsp.focus({workspace = " + std::to_string(wsId) + "})");
  }
  if (cursorKnown)
    (void)hyprland_ipc_request("dispatch hl.dsp.cursor.move({x = " + std::to_string(cx) +
                               ", y = " + std::to_string(cy) + "})");
  debug_log("overview", "reveal: restoring focus (%s)", origAddr.empty() ? "workspace" : "window");
}

void RevealPass::tick() {
  if (host_.dock().compositorKind != CompositorKind::Hyprland) return;
  if (!host_.open() || host_.show_apps() || host_.capture().live_enabled()) return;
  if (host_.anim().has_active() || host_.input().button_pressed || host_.input().drag_active) return;
  const uint64_t now = now_mono_ms();

  if (stage == Stage::Idle) {
    if (now < next_ms_) return;
    if (pick_and_focus()) {
      stage = Stage::Revealed;
      deadline_ms = now + kRevealRevealMs;
    } else {
      next_ms_ = now + kRevealNoTargetRecheckMs;
    }
    return;
  }

  if (now < deadline_ms) return;

  switch (stage) {
    case Stage::Revealed: {
      if (target_captured() || targetAddr.empty()) {
        restore_focus();
        stage = Stage::Restoring;
        deadline_ms = now + kRevealRestoreMs;
        break;
      }
      host_.capture().sync_snapshot_streams();
      stage = Stage::Syncing;
      deadline_ms = now + kRevealCaptureWaitMs;
      break;
    }
    case Stage::Syncing: {
      if (target_captured() || ++attempts >= kRevealMaxAttempts) {
        restore_focus();
        stage = Stage::Restoring;
        deadline_ms = now + kRevealRestoreMs;
        break;
      }
      host_.capture().sync_snapshot_streams();
      stage = Stage::Syncing;
      deadline_ms = now + kRevealCaptureWaitMs;
      break;
    }
    case Stage::Restoring: {
      stage = Stage::Idle;
      break;
    }
    case Stage::Idle:
    default:
      stage = Stage::Idle;
      break;
  }
}

} // namespace eh::shell::overview
