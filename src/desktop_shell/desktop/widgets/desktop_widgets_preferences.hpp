#pragma once

#include <string>
#include <vector>

struct DesktopWidgetConfig;

namespace eh::shell::desktop {

struct DesktopApp;

void desktop_widgets_prefs_save(const std::vector<DesktopWidgetConfig>& configs);
std::vector<DesktopWidgetConfig> desktop_widgets_prefs_load();

}

