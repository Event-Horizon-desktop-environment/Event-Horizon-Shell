#pragma once

// Panel paint interface — brand-new three-zone strip.
// The panel hosts the existing widget slot paints (clock, weather, media,
// workspaces, battery, bluetooth, world-clock, control-center, tray, app
// slots) inside its own layout/measure/paint pass. Nothing here is copied
// from the dock or taskbar painters: geometry, caching and fallback rules
// are written for a thin indicator bar.

#include "desktop_shell/shared/core/running_snapshot.hpp"
#include "services/mpris/mpris_player.hpp"
#include "desktop_shell/widgets/shared/shared_slot_paint.hpp"
#include "desktop_shell/panel/layout/panel_types.hpp"

#include <cairo.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct PanelApp;

namespace eh::shell::panel {

struct PanelPaintSlot {
  using Kind = eh::widgets::shared_slot_paint::SlotKind;
  Kind kind = Kind::Clock;
  std::string key;
  std::string iconId;
  std::uint64_t chosenSerial = 0;
  bool anyActivated = false;
};

struct PanelSectionWidths {
  double left = 0.0;
  double center = 0.0;
  double right = 0.0;
  double total = 0.0;
};

void panel_paint_widget_bar(PanelApp& app, cairo_t* cr, double x, double y, double boxW, double boxH,
                            const std::vector<std::string>& leftW, const std::vector<std::string>& centerW,
                            const std::vector<std::string>& rightW, std::vector<PanelWidgetHit>* out_hits,
                            int hoverSlot, int pressedSlot, bool usePanel,
                            const eh::shell::shared::RunningSnapshot& runningSnap,
                            const eh::mpris::PlayerSnapshot& mprisSnap);

PanelSectionWidths panel_measure_sections(PanelApp& app, const std::vector<std::string>& leftW,
                                          const std::vector<std::string>& centerW,
                                          const std::vector<std::string>& rightW,
                                          const eh::shell::shared::RunningSnapshot& runningSnap,
                                          const eh::mpris::PlayerSnapshot& mprisSnap);

}  // namespace eh::shell::panel
