#pragma once

#include "desktop_shell/widgets/start_menu/start_menu.hpp"

inline const char* dock_appdrawer_zone_cstr(AppDrawerHitZone z) {
  switch (z) {
    case AppDrawerHitZone::None: return "None";
    case AppDrawerHitZone::SearchField: return "SearchField";
    case AppDrawerHitZone::CategoryTab: return "CategoryTab";
    case AppDrawerHitZone::AppListRow: return "AppListRow";
    case AppDrawerHitZone::PinnedApp: return "PinnedApp";
    case AppDrawerHitZone::PowerButton: return "PowerButton";
    case AppDrawerHitZone::NightlightButton: return "NightlightButton";
    case AppDrawerHitZone::OrganizeButton: return "OrganizeButton";
  }
  return "?";
}
