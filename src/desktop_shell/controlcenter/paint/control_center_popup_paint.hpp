#pragma once

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <vector>

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

void control_center_popup_paint(eh::shell::dock::control_center::ControlCenterState& state,
                                cairo_t* cr,
                                int popupW, int popupH,
                                const eh::config::ShellConfig& scPopupOv,
                                eh::mpris::DockMpris* mpris,
                                const std::string& ccWidgetId,
                                eh::icons::IconCache& icons,
                                const std::vector<std::string>& pinnedApps);
