#include "desktop_shell/taskbar/Features/taskbar_pin_drag.h"
#include "desktop_shell/taskbar/Features/taskbar_pinned.h"

#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/taskbar/layout/taskbar_types.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "configuration/shell_config.hpp"

namespace eh::shell::taskbar {
extern std::vector<TaskbarWidgetHit> g_widgetHits;
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace eh::shell::taskbar {

void taskbar_pin_drag_init(TaskbarApp& app, const std::string& widgetId) {
  app.pinDragPinsSnapshot = app.settings.pinnedApps;
  app.pinDragPaintOrder.clear();
  app.pinDragCandidate = true;
  app.pinDragging = false;
  app.pinDragDirty = false;
  app.pinDragInsertIdx = -1;
  app.pinDragGeometryValid = false;
  app.pinDragStartX = app.pointerX;
  app.pinDragStartY = app.pointerY;

  // Snapshot pinned-section geometry from g_widgetHits (un-reordered state at press).
  // This avoids feedback when g_widgetHits shifts after reorder paint.
  double firstLeft = 0.0;
  double iconW = 0.0;
  double prevRight = 0.0;
  double gap = 0.0;
  int pinnedCount = 0;
  for (const auto& h : g_widgetHits) {
    if (h.slotKind != 0) continue;
    if (!h.isPinned) continue;
    if (pinnedCount == 0) {
      firstLeft = h.x;
      iconW = h.w;
      prevRight = h.x + h.w;
    } else if (pinnedCount == 1) {
      gap = h.x - prevRight;
    }
    ++pinnedCount;
  }
  app.pinDragFirstLeft = firstLeft;
  app.pinDragIconW = iconW;
  app.pinDragStride = (pinnedCount > 1) ? (iconW + gap) : iconW;
  // NOTE: firstLeft is legitimately 0.0 in panel layout mode (startX = 0.0 there),
  // so validity must NOT be inferred from firstLeft/iconW/stride == 0.0 — use an
  // explicit flag based on whether we actually found a usable pinned run instead.
  app.pinDragGeometryValid = (pinnedCount >= 2) && (iconW > 0.0) && (app.pinDragStride > 0.0);

  debug_log("taskbar", "pin_drag: init key=%s snapshotSize=%zu firstLeft=%.1f stride=%.1f iconW=%.1f",
            widgetId.c_str(), app.pinDragPinsSnapshot.size(), firstLeft, app.pinDragStride, iconW);

  std::string rawPin = widgetId;
  for (const auto& p : app.settings.pinnedApps) {
    if (eh::shell::paths::normalize_desktop_app_id(p) == widgetId) {
      rawPin = p;
      break;
    }
  }
  app.pinDragKeyRaw = rawPin;
  app.pinDragKey = eh::shell::paths::normalize_desktop_app_id(rawPin);
}

PinDragReleaseResult taskbar_pin_drag_release(TaskbarApp& app, bool left) {
  PinDragReleaseResult result;
  if (!left || (!app.pinDragCandidate && !app.pinDragging)) return result;

  result.wasDragging = app.pinDragging;
  result.dragDirty = app.pinDragDirty;
  result.key = app.pinDragKey;

  debug_log("taskbar", "pin_drag: release wasDragging=%d dragDirty=%d key=%s paintOrderSize=%zu",
            (int)result.wasDragging, (int)result.dragDirty, result.key.c_str(), app.pinDragPaintOrder.size());

  if (result.wasDragging && result.dragDirty && !app.pinDragPaintOrder.empty()) {
    debug_log("taskbar", "pin_drag: committing reorder to settings (size=%zu)", app.pinDragPaintOrder.size());
    app.settings.pinnedApps = app.pinDragPaintOrder;
  }

  app.pinDragPinsSnapshot.clear();
  app.pinDragPaintOrder.clear();
  app.pinDragCandidate = false;
  app.pinDragging = false;
  app.pinDragDirty = false;
  app.pinDragInsertIdx = -1;
  app.pinDragFirstLeft = 0.0;
  app.pinDragStride = 0.0;
  app.pinDragIconW = 0.0;
  app.pinDragGeometryValid = false;
  app.pinDragKey.clear();
  app.pinDragKeyRaw.clear();

  if (!result.wasDragging && !result.key.empty()) {
    result.wasClick = true;
  }

  return result;
}

void taskbar_pin_drag_motion(TaskbarApp& app) {
  if (!app.pinDragCandidate && !app.pinDragging) return;

  const double dx = app.pointerX - app.pinDragStartX;
  const double dy = app.pointerY - app.pinDragStartY;
  const double dist2 = dx * dx + dy * dy;

  if (app.pinDragCandidate && !app.pinDragging) {
    if (dist2 < (6.0 * 6.0)) return;
    debug_log("taskbar", "pin_drag: threshold crossed key=%s startXY=%.0f,%.0f pointerXY=%.0f,%.0f",
              app.pinDragKey.c_str(), app.pinDragStartX, app.pinDragStartY, app.pointerX, app.pointerY);
    app.pinDragging = true;
  }
  if (!app.pinDragging) return;
  if (app.pinDragKey.empty()) return;

  const double firstLeft = app.pinDragFirstLeft;
  const double iconW = app.pinDragIconW;
  const double stride = app.pinDragStride;
  // NOTE: firstLeft is legitimately 0.0 in panel layout mode, so we cannot use
  // "== 0.0" as an uninitialized-geometry sentinel — that silently disabled
  // reordering (and thus toml persistence) whenever the taskbar used panel mode.
  if (!app.pinDragGeometryValid) {
    debug_log("taskbar", "pin_drag: motion skipped — invalid geometry (firstLeft=%.1f iconW=%.1f stride=%.1f)", firstLeft, iconW, stride);
    return;
  }

  // Count how many pinned apps exist (excluding the dragged one) using g_widgetHits
  int reducedCount = 0;
  for (const auto& h : g_widgetHits) {
    if (h.slotKind != 0) continue;
    if (!h.isPinned) continue;
    if (eh::shell::paths::normalize_desktop_app_id(h.widgetId) == app.pinDragKey) continue;
    ++reducedCount;
  }
  if (reducedCount < 1) {
    debug_log("taskbar", "pin_drag: motion skipped — reducedCount=%d", reducedCount);
    return;
  }

  // Clamp pointer to the pinned section bounds (computed from saved geometry, not g_widgetHits)
  // lastRight = firstLeft + (reducedCount + 1) * stride - (stride - iconW)
  const double pinnedRight = firstLeft + static_cast<double>(reducedCount + 1) * stride - (stride - iconW);
  const double pxHit = std::clamp(app.pointerX, firstLeft, pinnedRight);

  // Find raw insertion index in the reduced (n-1) array using uniform stride
  int rawInsert = reducedCount;
  for (int i = 0; i < reducedCount; i++) {
    const double slotMid = firstLeft + static_cast<double>(i) * stride + iconW * 0.5;
    if (pxHit < slotMid) {
      rawInsert = i;
      break;
    }
  }
  rawInsert = std::clamp(rawInsert, 0, reducedCount);

  // Hysteresis: only accept a change when the pointer has moved past the midpoint + threshold
  int insertInReduced = rawInsert;
  const int committed = app.pinDragInsertIdx;
  if (committed >= 0 && rawInsert != committed) {
    const double hyst = std::clamp(iconW * 0.12, 4.0, 12.0);
    bool accept = false;
    if (rawInsert > committed) {
      if (committed < reducedCount) {
        const double mid = firstLeft + static_cast<double>(committed) * stride + iconW * 0.5;
        if (pxHit >= mid + hyst) accept = true;
      }
    } else {
      if (committed > 0) {
        const double mid = firstLeft + static_cast<double>(committed - 1) * stride + iconW * 0.5;
        if (pxHit <= mid - hyst) accept = true;
      }
    }
    if (!accept) insertInReduced = committed;
  }

  // Log every 5th insert change (avoid log spam)
  static int logCounter = 0;
  if (insertInReduced != app.pinDragInsertIdx || logCounter % 5 == 0) {
    debug_log("taskbar", "pin_drag: motion insert=%d→%d reducedCount=%d pointerX=%.1f pxHit=%.1f firstLeft=%.1f",
              app.pinDragInsertIdx, insertInReduced, reducedCount, app.pointerX, pxHit, firstLeft);
  }
  ++logCounter;

  if (insertInReduced == app.pinDragInsertIdx) {
    // No order change — still need the ghost icon to track the pointer, but
    // don't force a synchronous full repaint on every pointer-motion event.
    // Coalesce to the next compositor frame instead (this is what made
    // dragging feel slow: every motion event was doing a blocking full draw).
    app.frameRedrawPending = true;
    taskbar_schedule_frame(app);
    return;
  }

  app.pinDragInsertIdx = insertInReduced;
  taskbar_pin_drag_rebuild_paint_order(app);
  app.pinDragDirty = (app.pinDragPaintOrder != app.pinDragPinsSnapshot);
  debug_log("taskbar", "pin_drag: rebuild done insertIdx=%d dirty=%d paintOrderSize=%zu",
            insertInReduced, (int)app.pinDragDirty, app.pinDragPaintOrder.size());
  app.frameRedrawPending = true;
  taskbar_schedule_frame(app);
}

}
