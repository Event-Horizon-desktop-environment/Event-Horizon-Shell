#include "../../dock/core/dock_app.h"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/fs/file_util.hpp"

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

bool dock_settings_equal(const DockSettings& a, const DockSettings& b) {
   
  return a.dockShowDock == b.dockShowDock && a.dockAutoHide == b.dockAutoHide && a.dockRadius == b.dockRadius &&
         a.dockIconSize == b.dockIconSize && a.dockIconSpacing == b.dockIconSpacing &&
          a.dockScale == b.dockScale && a.shellUiScale == b.shellUiScale && a.dockBottomGap == b.dockBottomGap &&
          a.dockExclusiveZoneGap == b.dockExclusiveZoneGap && a.iconTheme == b.iconTheme &&
         a.dockBorderEnabled == b.dockBorderEnabled && a.dockBorderSize == b.dockBorderSize &&
         a.dockBorderHue == b.dockBorderHue &&
         a.dockOpacity == b.dockOpacity && a.outputName == b.outputName &&
         a.dockWidgetsEnabled == b.dockWidgetsEnabled && a.dockGroupApps == b.dockGroupApps &&
         a.dockTooltipsEnabled == b.dockTooltipsEnabled &&
          a.dockPinnedAppsTrayPill == b.dockPinnedAppsTrayPill &&
          a.dockRunningAppsTrayPill == b.dockRunningAppsTrayPill &&
          a.dockBarFollowsIcons == b.dockBarFollowsIcons && a.dockManualBarHeightPx == b.dockManualBarHeightPx &&
          a.slotPillOpacity == b.slotPillOpacity && a.dockBorderOpacity == b.dockBorderOpacity &&
          a.dockLiquidGlass == b.dockLiquidGlass && a.dockColoredGlass == b.dockColoredGlass &&
           a.pinnedApps == b.pinnedApps && a.drawerPinnedApps == b.drawerPinnedApps &&
          a.startMenuPinnedApps == b.startMenuPinnedApps &&
           a.leftWidgets == b.leftWidgets && a.centerWidgets == b.centerWidgets && a.rightWidgets == b.rightWidgets;
}

bool dock_save_dock_settings(const DockSettings& s) {
   
  eh::config::ShellConfig sc = eh::config::shell_config_snapshot();
  sc.dock = s;
  (void)eh::config::write_state_settings_toml(sc);
  eh::config::shell_config_apply_from_memory(std::move(sc));
  return true;
}
