#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace hpv::sc {

struct ChromePaintColors {
  double dockFillR = 0.1, dockFillG = 0.1, dockFillB = 0.12;
  double panelFillR = 0.1, panelFillG = 0.1, panelFillB = 0.12;
  double accentR = 0.39, accentG = 0.40, accentB = 0.96;
  double textR = 0.9, textG = 0.9, textB = 0.9;
};

struct ShellAppearance {
  bool matugenThemingEnabled = false;
  bool matugenPaletteOk = false;
  std::string matugenScheme;
  std::string matugenMode;
  std::string matugenCustomColor;
  std::string wallpaperImage;
  double opacityLevel = 1.0;
  double opacityStep = 0.1;
  int launchpadColumns = 7;
  int launchpadRowCount = 2;
  std::string font;
  bool launchpadShowLabels = true;
  std::string gtkTheme;
  std::string iconTheme;
  std::string cursorTheme;
  int cursorSize = 24;
  std::string qtStyle;
  std::string qtColorScheme;
  std::string plasmaTheme;
  bool taskbarShowLabel = true;
  bool taskbarShowApplicationIcon = true;
  bool taskbarGrouping = true;
  bool taskbarShowDesktopButton = true;
  bool panelOpaque = false;
  bool panelThin = false;
};

struct ShellConfig {
  ShellAppearance appearance;
  std::string wallpaperImage;
};

inline ChromePaintColors derived_chrome_colors(const ShellAppearance& ap)
{
  ChromePaintColors c;
  (void)ap;
  return c;
}

inline ShellConfig shell_config_snapshot_skip_matugen()
{
  ShellConfig sc;
  sc.appearance = ShellAppearance{};
  return sc;
}

}
