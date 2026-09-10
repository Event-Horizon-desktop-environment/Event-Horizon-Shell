#include "desktop_shell/taskbar/Features/taskbar_pinned.h"

#include "desktop_shell/taskbar/core/taskbar.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "desktop_shell/taskbar/layout/taskbar_types.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace eh::shell::taskbar {

const std::vector<std::string>& taskbar_pinned_apps_source_for_layout(const TaskbarApp& app) {
  if (app.pinDragging && !app.pinDragPinsSnapshot.empty()) {
    if (!app.pinDragPaintOrder.empty()) return app.pinDragPaintOrder;
    return app.pinDragPinsSnapshot;
  }
  return app.settings.pinnedApps;
}

void taskbar_pin_drag_rebuild_paint_order(TaskbarApp& app) {
  debug_log("taskbar", "pin_drag_rebuild: dragging=%d snapSize=%zu insertIdx=%d key='%s'",
            (int)app.pinDragging, app.pinDragPinsSnapshot.size(), app.pinDragInsertIdx, app.pinDragKey.c_str());

  if (!app.pinDragging || app.pinDragPinsSnapshot.empty()) {
    app.pinDragPaintOrder.clear();
    debug_log("taskbar", "pin_drag_rebuild: early exit (not dragging or empty snapshot)");
    return;
  }
  if (app.pinDragInsertIdx < 0) {
    app.pinDragPaintOrder = app.pinDragPinsSnapshot;
    debug_log("taskbar", "pin_drag_rebuild: no insert idx, returning snapshot (size=%zu)", app.pinDragPaintOrder.size());
    return;
  }
  if (app.pinDragKey.empty()) {
    app.pinDragPaintOrder.clear();
    debug_log("taskbar", "pin_drag_rebuild: early exit (empty key)");
    return;
  }

  std::vector<std::string> pins = app.pinDragPinsSnapshot;

  int curIdx = -1;
  for (int i = 0; i < static_cast<int>(pins.size()); ++i) {
    if (eh::shell::paths::normalize_desktop_app_id(pins[static_cast<size_t>(i)]) == app.pinDragKey) {
      curIdx = i;
      break;
    }
  }
  if (curIdx < 0 || static_cast<int>(pins.size()) <= 1) {
    app.pinDragPaintOrder = std::move(pins);
    return;
  }

  std::vector<int> rawIndices;
  rawIndices.reserve(pins.size());
  for (int i = 0; i < static_cast<int>(pins.size()); i++) {
    const std::string k = eh::shell::paths::normalize_desktop_app_id(pins[static_cast<size_t>(i)]);
    if (k == "unknown" || k == eh::shell::kSettingsAppId) continue;
    rawIndices.push_back(i);
  }

  std::vector<std::string> pinStrings;
  pinStrings.reserve(rawIndices.size());
  for (int ri : rawIndices) pinStrings.push_back(pins[static_cast<size_t>(ri)]);

  int strCurIdx = -1;
  for (int i = 0; i < static_cast<int>(pinStrings.size()); ++i) {
    if (eh::shell::paths::normalize_desktop_app_id(pinStrings[static_cast<size_t>(i)]) == app.pinDragKey) {
      strCurIdx = i;
      break;
    }
  }
  if (strCurIdx < 0 || static_cast<int>(pinStrings.size()) <= 1) {
    app.pinDragPaintOrder = std::move(pins);
    return;
  }

  const int reducedCount = static_cast<int>(pinStrings.size()) - 1;
  const int insertAt = std::clamp(app.pinDragInsertIdx, 0, reducedCount);

  const std::string moved = pinStrings[static_cast<size_t>(strCurIdx)];
  pinStrings.erase(pinStrings.begin() + strCurIdx);
  pinStrings.insert(pinStrings.begin() + insertAt, moved);

  for (size_t i = 0; i < pinStrings.size(); ++i)
    pins[static_cast<size_t>(rawIndices[i])] = std::move(pinStrings[i]);
  app.pinDragPaintOrder = std::move(pins);
  debug_log("taskbar", "pin_drag_rebuild: done orderSize=%zu insertAt=%d curIdx=%d",
            app.pinDragPaintOrder.size(), insertAt, curIdx);
}

}
