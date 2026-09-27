#include <cairo/cairo.h>

#include "ux/settings/settings_tab_taskbar/settings_tab_taskbar.hpp"
#include "ux/settings/settings_tab_taskbar/taskbar_tab_m3.hpp"

const char* const kWidthModeLabels[] = {"Floating", "Edge-to-edge", "Fill"};

const char* const kThumbThresholdLabels[] = {"Up to 5", "Up to 8", "Up to 10", "Up to 15", "Up to 20"};
const int kThumbThresholdValues[] = {5, 8, 10, 15, 20};
const int kThumbThresholdCount = 5;

// Paint function, delegating to the M3 widgets.
void paint_taskbar_tab(App& app, cairo_t* cr, int contentX, int contentW,
                       double glassOv, double dockMatA, double paintPointerYOffset,
                       int tbWidgetCardH) {
  (void)dockMatA;
  (void)paintPointerYOffset;
  (void)tbWidgetCardH;
  auto& m3 = m3::detail::taskbarM3();
  m3.paint(app, cr, contentX, contentW, glassOv, tbWidgetCardH);
}

// M3 input dispatch.
bool taskbar_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  auto& m3 = m3::detail::taskbarM3();
  return m3.handlePointerDown(app, px, py, contentX, contentW);
}

bool taskbar_m3_handle_pointer_up(App& app, float px, float py) {
  auto& m3 = m3::detail::taskbarM3();
  return m3.handlePointerUp(app, px, py);
}

bool taskbar_m3_handle_pointer_move(App& app, float px, float py) {
  auto& m3 = m3::detail::taskbarM3();
  const bool handled = m3.handlePointerMove(px, py);
  if (handled) m3.flushSliderValues(app);
  return handled;
}

void taskbar_m3_handle_pointer_leave(App&) {
  auto& m3 = m3::detail::taskbarM3();
  m3.handlePointerLeave();
}

bool taskbar_m3_has_active_slider(const App& app) {
  auto& m3 = m3::detail::taskbarM3();
  if (m3.hasActiveSlider()) return true;
  // Appearance child tab's slider drag state might still use app.sliderDrag
  if (m3.activeChildTab_ == m3::detail::TaskbarTabM3State::kTabAppearance &&
      app.sliderDrag >= 100 && app.sliderDrag < 110) return true;
  return false;
}

bool taskbar_m3_is_appearance_child_tab() {
  return m3::detail::taskbarM3_is_appearance_child_tab();
}

bool taskbar_m3_is_widgets_child_tab() {
  return m3::detail::taskbarM3_is_widgets_child_tab();
}

void taskbar_display_dd_sync(App& app, int contentX, int contentW) {
  m3::detail::taskbar_display_dd_sync(app, contentX, contentW);
}

bool taskbar_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW) {
  return m3::detail::taskbar_display_dd_commit_pointer_up(app, px, py, contentX, contentW);
}
