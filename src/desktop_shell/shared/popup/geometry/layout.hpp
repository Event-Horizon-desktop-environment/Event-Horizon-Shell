#pragma once

#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"

namespace eh::shell::dock {

inline int kSpotlightPopupW() { return static_cast<int>(656.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock)); }
inline int kSpotlightSearchOuterH() { return static_cast<int>(88.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock)); }
inline int kSpotlightRowPx() { return static_cast<int>(34.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock)); }
inline int kSpotlightMaxRows() { return 8; }
inline int kControlCenterPopupW() { return static_cast<int>(620.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock)); }
inline int kControlCenterPopupH() { return static_cast<int>(640.0 * dock_ui_scale(eh::config::shell_config_snapshot().dock)); }

[[nodiscard]] inline int spotlight_popup_total_height() {
  return kSpotlightSearchOuterH() + kSpotlightMaxRows() * kSpotlightRowPx();
}

}
