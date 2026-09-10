#include <cairo/cairo.h>

#include "ux/settings/settings_tab_dock/settings_tab_dock.hpp"
#include "ux/settings/settings_tab_dock/dock_tab_m3.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"

// Paint function — delegates to the M3 widgets.
void paint_dock_tab(App& app, cairo_t* cr, int contentX, int contentW,
                    double glassOv, double dockMatA, double paintPointerYOffset,
                    int chDock, int autoBarHeightPx) {
  (void)dockMatA;
  (void)paintPointerYOffset;
  auto& m3 = m3::detail::dockM3();
  m3.paint(app, cr, contentX, contentW, glassOv, chDock, autoBarHeightPx);
}

// M3 input dispatch, called from settings_event_handlers.cpp.
bool dock_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  auto& m3 = m3::detail::dockM3();
  return m3.handlePointerDown(app, px, py, contentX, contentW);
}

bool dock_m3_handle_pointer_up(App& app, float px, float py) {
  auto& m3 = m3::detail::dockM3();
  return m3.handlePointerUp(app, px, py);
}

bool dock_m3_handle_pointer_move(App& app, float px, float py) {
  auto& m3 = m3::detail::dockM3();
  const bool handled = m3.handlePointerMove(px, py);
  if (handled) m3.flushSliderValues(app);
  return handled;
}

void dock_m3_handle_pointer_leave(App&) {
  auto& m3 = m3::detail::dockM3();
  m3.handlePointerLeave();
}

bool dock_m3_has_active_slider(const App& app) {
  auto& m3 = m3::detail::dockM3();
  if (m3.activeSlider_ >= 0) return true;
  // Appearance child tab uses app.sliderDrag
  if (m3.activeChildTab_ == m3::detail::DockTabM3State::kTabAppearance &&
      app.sliderDrag >= 200 && app.sliderDrag < 212) return true;
  return false;
}

bool dock_m3_is_appearance_child_tab() {
  return m3::detail::dockM3_is_appearance_child_tab();
}

bool dock_m3_is_widgets_child_tab() {
  return m3::detail::dockM3_is_widgets_child_tab();
}

void dock_renderer_dd_sync(App& app, int contentX, int contentW) {
  m3::detail::renderer_dd_sync(app, contentX, contentW);
}

bool dock_renderer_dd_commit_pointer_up(App& app, float px, float py) {
  auto& m3 = m3::detail::dockM3();
  if (!app.rendererDd.open()) return false;
  if (m3.activeChildTab_ != m3::detail::DockTabM3State::kTabSettings) {
    app.rendererDd.close();
    return false;
  }
  const int scr = settings_scroll_px_int(app);
  const int rr = app.rendererDd.hit_row(static_cast<int>(px), static_cast<int>(py), scr,
                                        app.width, app.height);
  if (rr >= 0 && rr < m3::detail::kRendererCount) {
    app.settings.renderer = m3::detail::kRendererValues[rr];
  }
  app.rendererDd.close();
  draw(app);
  return true;
}
