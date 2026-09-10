#pragma once

#include <string>
#include <vector>

namespace eh::shell::taskbar {

struct TaskbarSettings {
  bool enabled = false;
  int widthMode = 0;  // 0 = floating, 1 = edge, 2 = fill
  int height = 48;
  int radius = 12;
  int opacity = 100;
  int iconSize = 32;
  int iconSpacing = 8;
  int floatingAmount = 8;
  double scale = 1.0;
  std::string iconTheme = "";
  std::string outputName{};
  std::vector<std::string> leftWidgets{};
  std::vector<std::string> centerWidgets{};
  std::vector<std::string> rightWidgets{};
  std::vector<std::string> pinnedApps{};
  bool groupApps = true;
  int slotPillOpacity = 100;
  bool positionTop = false;
  bool autoHide = false;
  bool tooltipsEnabled = true;
  bool pinnedAppsTrayPill = false;
  bool widgetsEnabled = true;
  bool border = false;
  int borderSize = 1;
};

}
