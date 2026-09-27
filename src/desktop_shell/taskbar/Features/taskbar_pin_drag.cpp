#include "desktop_shell/taskbar/Features/taskbar_pin_drag.h"
#include "desktop_shell/taskbar/Features/taskbar_pinned.h"

#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/taskbar/layout/taskbar_types.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "configuration/shell_config.hpp"

namespace eh::shell::taskbar {
extern std::vector<std::vector<TaskbarWidgetHit>> g_layerHits;
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

  // Snapshot pinned-section geometry from this layer's hits (un-reordered
  // state at press). This avoids feedback when hits shift after reorder
  // paint. Rects (not just stride) are recorded so variable-width labeled
  // slots still reorder correctly.
  double firstLeft = 0.0;
  double iconW = 0.0;
  double prevRight = 0.0;
  double gap = 0.0;
  int pinnedCount = 0;
  app.pinDragRects.clear();
  const auto& initHits = taskbar_layer_hits(app, app.pressedLayerIdx);
  for (const auto& h : initHits) {
    if (h.slotKind != 0) continue;
    if (!h.isPinned) continue;
    app.pinDragRects.emplace_back(h.x, h.w);
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
  app.pinDragRects.clear();
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
    // A drag is not a click: drop any preview popup the press just opened.
    taskbar_thumbs_dismiss(app);
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

  // Count how many pinned apps exist (excluding the dragged one) using the
  // press layer's hits.
  const auto& dragHits = taskbar_layer_hits(app, app.pressedLayerIdx);
  int reducedCount = 0;
  for (const auto& h : dragHits) {
    if (h.slotKind != 0) continue;
    if (!h.isPinned) continue;
    if (eh::shell::paths::normalize_desktop_app_id(h.widgetId) == app.pinDragKey) continue;
    ++reducedCount;
  }
  if (reducedCount < 1) {
    debug_log("taskbar", "pin_drag: motion skipped — reducedCount=%d", reducedCount);
    return;
  }

  // Insertion math over the actual hit rects (variable-width safe): midpoint
  // of each non-dragged pinned slot decides the slot boundary.
  std::vector<std::pair<double, double>> rects;
  rects.reserve(app.pinDragRects.size());
  for (const auto& h : dragHits) {
    if (h.slotKind != 0 || !h.isPinned) continue;
    if (eh::shell::paths::normalize_desktop_app_id(h.widgetId) == app.pinDragKey) continue;
    rects.emplace_back(h.x, h.w);
  }
  // Fall back to the init-time snapshot when live hits shifted mid-drag.
  if (rects.empty() && !app.pinDragRects.empty()) {
    for (const auto& r : app.pinDragRects) rects.push_back(r);
  }
  if (rects.empty()) return;
  const double firstRectLeft = rects.front().first;
  const double pinnedRight = rects.back().first + rects.back().second;
  const double pxHit = std::clamp(app.pointerX, firstRectLeft, pinnedRight);

  // Find raw insertion index in the reduced array using rect midpoints.
  int rawInsert = static_cast<int>(rects.size());
  for (size_t ri = 0; ri < rects.size(); ++ri) {
    const double mid = rects[ri].first + rects[ri].second * 0.5;
    if (pxHit < mid) { rawInsert = static_cast<int>(ri); break; }
  }
  rawInsert = std::clamp(rawInsert, 0, static_cast<int>(rects.size()));

  // Hysteresis: only accept a change when the pointer has moved past the midpoint + threshold
  int insertInReduced = rawInsert;
  const int committed = app.pinDragInsertIdx;
  const int nRects = static_cast<int>(rects.size());
  if (committed >= 0 && rawInsert != committed) {
    const double refW = (committed >= 0 && committed < nRects)
        ? rects[static_cast<size_t>(committed)].second
        : rects[static_cast<size_t>(std::clamp(rawInsert, 0, nRects - 1))].second;
    const double hyst = std::clamp(refW * 0.12, 4.0, 12.0);
    bool accept = false;
    if (rawInsert > committed) {
      if (committed < nRects) {
        const double mid = rects[static_cast<size_t>(committed)].first +
                           rects[static_cast<size_t>(committed)].second * 0.5;
        if (pxHit >= mid + hyst) accept = true;
      }
    } else {
      if (committed > 0) {
        const double mid = rects[static_cast<size_t>(committed - 1)].first +
                           rects[static_cast<size_t>(committed - 1)].second * 0.5;
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
