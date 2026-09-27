// Panel input — brand-new hit testing and dispatch for the thin strip.
// Hit rects follow the painter's three-zone layout exactly (same padding and
// gap rules); a click first dismisses any open popup (toggle-close), then
// activates the slot underneath: workspaces switch, media toggles, clock
// opens calendar, control-center opens its popup, tray opens its menu stub.

#include "desktop_shell/panel/core/panel.hpp"
#include "desktop_shell/panel/paint/panel_paint.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"

namespace eh::shell::panel {

namespace {

using Kind = PanelPaintSlot::Kind;

struct HitSlot {
  PanelPaintSlot slot;
  double w = 0;
};

}  // namespace

}  // namespace eh::shell::panel
