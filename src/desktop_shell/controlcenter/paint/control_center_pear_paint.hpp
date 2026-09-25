#pragma once

// PearCenter compact layout paint for the quick-settings popup.
// Draws Section A (SectionButtons LongButtons + DND + quick toggles),
// Section B (Volume / Brightness / Media rows), and the SectionNetworks
// overlay. Geometry comes from PearLayout; backends are the existing EH ones.

#include <cairo/cairo.h>

#include <string>

namespace eh::icons {
class IconCache;
}
namespace eh::mpris {
class DockMpris;
}
namespace eh::config {
struct ShellConfig;
}
namespace eh::shell::dock::control_center {
struct ControlCenterState;
}

void pear_center_paint(eh::shell::dock::control_center::ControlCenterState& state,
                              cairo_t* cr, int popupW,
                              const eh::config::ShellConfig& scPopupOv,
                              eh::mpris::DockMpris* mpris,
                              const std::string& ccWidgetId);

namespace eh::shell::dock::control_center {
// Optimistic Night Light paint state (supervisor owns gamma; the dock child
// has no gamma client). Initialised from config, flipped on click alongside
// the `nightlight.toggle` bus publish; resyncs on the next config snapshot.
bool pear_nightlight_active(const eh::config::ShellConfig& sc);
void pear_nightlight_set_active(bool active);
} // namespace eh::shell::dock::control_center
