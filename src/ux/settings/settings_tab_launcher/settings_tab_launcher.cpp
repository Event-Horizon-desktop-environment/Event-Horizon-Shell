#include <cairo/cairo.h>
#include <algorithm>
#include <cstdio>
#include <string>

#include "m3/core/primitives/box.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/settings_tab_launcher/launcher_tab_m3.hpp"
#include "ux/settings/settings_tab_launcher/settings_tab_launcher.hpp"
#include "ux/settings/utils/events/settings_event_handlers.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

extern void draw(App& app);

// 2-column card grid geometry
void launcher_app_geom(int contentX, int contentW, int idx,
                       int& trX, int& trY, int& trW,
                       int& cardX, int& cardY, int& cardW) {
  constexpr int kTopMargin = 72;
  constexpr int kCardH = 92;
  constexpr int kGap = 12;
  constexpr int kPad = 20;
  constexpr int kSliderY = 50;

  const int col = idx % 2;
  const int row = idx / 2;
  const int colW = (contentW * 68) / 100;
  const int colX = contentX + (contentW - colW) / 2;
  cardW = (colW - kGap) / 2;
  if (cardW < 160) cardW = 160;
  cardX = colX + col * (cardW + kGap);
  cardY = kContentTop + kTopMargin + row * (kCardH + kGap);
  trX = cardX + kPad;
  trW = cardW - kPad - kPad;
  if (trW < 40) trW = 40;
  trY = cardY + kSliderY;
}

// View mode combo positioned inside card at index 8




// M3 wrappers.
static constexpr int kLauncherContentTop = kContentTop + kDockChildTabH + 12;

void paint_launcher_tab_m3(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  float a_r = 0.769f, a_g = 0.659f, a_b = 0.941f;
  float t_r = 1.0f, t_g = 1.0f, t_b = 1.0f;
  float s_r = 0.102f, s_g = 0.075f, s_b = 0.188f;
  float o_r = 0.478f, o_g = 0.416f, o_b = 0.588f;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  (void)o_r; (void)o_g; (void)o_b;

  m3::detail::launcher_paint_child_tab_bar(cr, contentX, contentW, t_r, t_g, t_b, a_r, a_g, a_b,
                                           s_r, s_g, s_b, glassOv, app.launcherChildTab);
  m3::detail::launcherM3().paint(app, cr, contentX, contentW, glassOv, kLauncherContentTop);
}

bool launcher_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW) {
  const int tabHit = m3::detail::launcher_hit_child_tab(px, py, contentX, contentW);
  if (tabHit >= 0 && tabHit != app.launcherChildTab) {
    settings_close_mode_dropdowns(app);
    app.launcherChildTab = tabHit;
    app.settingsLauncherScrollPx = 0;
    draw(app);
    return true;
  }
  return m3::detail::launcherM3().handlePointerDown(app, px, py);
}

bool launcher_m3_handle_pointer_up(App& app, float px, float py) {
  return m3::detail::launcherM3().handlePointerUp(app, px, py);
}

bool launcher_m3_handle_pointer_move(App& app, float px, float py) {
  const bool handled = m3::detail::launcherM3().handlePointerMove(px, py);
  if (handled) m3::detail::launcherM3().flushSliderValues(app);
  return handled;
}

void launcher_m3_handle_pointer_leave() {
  m3::detail::launcherM3().handlePointerLeave();
}

bool launcher_m3_has_active_slider(const App& app) {
  (void)app;
  return m3::detail::launcherM3().activeSlider_ >= 0;
}

void settings_clamp_launcher_scroll_px(App& app) {
  const int mx = std::max(0, app.launcherContentBottom - kLauncherContentTop -
                             std::max(120, app.height - kLauncherContentTop - kSpacingL));
  app.settingsLauncherScrollPx = std::clamp(app.settingsLauncherScrollPx, 0, mx);
}
