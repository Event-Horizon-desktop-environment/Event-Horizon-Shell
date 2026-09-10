#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include <cairo.h>

#include <cstddef>
#include <string>
#include <vector>

struct DockWidgetHit {
  std::string widgetId;
  double x = 0, y = 0, w = 0, h = 0;
  std::uint64_t chosenSerial = 0;
  bool isPinned = false;
  int slotKind = 0;
};

void dock_paint_widget_bar(DockApp& app, cairo_t* cr, double x, double y, double boxW, double boxH,
                            const std::vector<std::string>& leftW, const std::vector<std::string>& centerW,
                            const std::vector<std::string>& rightW, bool isPanel, int panelLayoutMode,
                            bool* out_media_marquee_wants_frame, size_t paint_layer_idx,
                            std::vector<DockWidgetHit>* out_hits = nullptr,
                            int taskbarHoverSlot = -1, int taskbarPressedSlot = -1,
                            double taskbarHoverLiftPx = 0.0,
                            bool* out_media_progress_tick = nullptr);
