#include "desktop_shell/shared/widgets/workspace_strip.hpp"

#include "desktop_shell/shared/widgets/workspace_strip.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "desktop_shell/dock/core/dock_app.h"

namespace eh::shell::dock {

void dock_ensure_workspace_strip(DockApp& app) {
   
  if (!eh::shell::shared::widgets::has_workspaces_widget(app.settings)) return;
  eh::widgets::workspace_strip_poll(app.workspaceStrip, app.workspaceStripLastPoll, app.compositorKind);
}

}
