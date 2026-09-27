#include <cairo/cairo.h>

#include "ux/settings/settings_tab_panel/settings_tab_panel.hpp"
#include "ux/settings/settings_tab_panel/panel_tab_m3.hpp"

const char* const kPanelWidthModeLabels[] = {"Floating", "Edge-to-edge", "Fill"};

void paint_panel_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double dockMatA,
                     double paintPointerYOffset, int widgetCardH) {
  (void)dockMatA;
  (void)paintPointerYOffset;
  (void)widgetCardH;
  auto& m3 = m3::detail::panelM3();
  m3.paint(app, cr, contentX, contentW, glassOv, 0);
}

bool panel_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  return m3::detail::panelM3().handlePointerDown(app, px, py, contentX, contentW);
}

bool panel_m3_handle_pointer_up(App& app, float px, float py) {
  return m3::detail::panelM3().handlePointerUp(app, px, py);
}

bool panel_m3_handle_pointer_move(App& app, float px, float py) {
  const bool handled = m3::detail::panelM3().handlePointerMove(px, py);
  if (handled) m3::detail::panelM3().flushSliderValues(app);
  return handled;
}

void panel_m3_handle_pointer_leave(App&) { m3::detail::panelM3().handlePointerLeave(); }

void panel_display_dd_sync(App& app, int contentX, int contentW) {
  m3::detail::panel_display_dd_sync(app, contentX, contentW);
}

bool panel_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW) {
  return m3::detail::panel_display_dd_commit_pointer_up(app, px, py, contentX, contentW);
}

bool panel_m3_has_active_slider(const App&) { return m3::detail::panelM3().hasActiveSlider(); }

bool panel_m3_is_appearance_child_tab() { return m3::detail::panelM3_is_appearance_child_tab(); }

bool panel_m3_is_widgets_child_tab() { return m3::detail::panelM3_is_widgets_child_tab(); }
