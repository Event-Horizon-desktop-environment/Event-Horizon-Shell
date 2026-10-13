#include "desktop_shell/widgets/popup/layout/layout_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"
#include "desktop_shell/widgets/layout/layout_paint.hpp"

namespace eh::shell::dock::popup::layout {

LayoutPopupClick layout_popup_click_action(double x, double y) {
  double W = 0.0, H = 0.0;
  layout_popup_size(&W, &H);
  if (x < 0 || x >= W || y < 0 || y >= H) return LayoutPopupClick::Close;

  double cbX = 0.0, cbY = 0.0;
  layout_popup_close_rect(&cbX, &cbY);
  if (x >= cbX && x < cbX + 24.0 && y >= cbY && y < cbY + 24.0) return LayoutPopupClick::Close;

  const int n = layout_popup_entry_count();
  for (int i = 0; i < n; ++i) {
    double rx = 0.0, ry = 0.0, rw = 0.0, rh = 0.0;
    if (!layout_popup_cell_rect(i, &rx, &ry, &rw, &rh)) continue;
    if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
      const char* id = layout_popup_entry_id(i);
      if (eh::widgets::layout_current() != id) eh::widgets::layout_apply(id);
      return LayoutPopupClick::Redraw;
    }
  }
  return LayoutPopupClick::None;
}

void dock_layout_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
  switch (layout_popup_click_action(x, y)) {
    case LayoutPopupClick::Close: popup_close(app); break;
    case LayoutPopupClick::Redraw: popup_draw_surface(app); break;
    case LayoutPopupClick::None: break;
  }
}

}  // namespace eh::shell::dock::popup::layout
