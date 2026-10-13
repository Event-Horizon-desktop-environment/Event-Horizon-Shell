#pragma once

#include "desktop_shell/widgets/layout/layout_paint.hpp"

#include <cstdint>

typedef struct _cairo cairo_t;
struct DockApp;
namespace eh::config {
struct ShellConfig;
}

namespace eh::shell::dock::popup::layout {

constexpr int kLayoutPopupW = 320;

// Shared popup geometry. Single column of tall rows for a handful of layouts
// (Hyprland: 4), two-column grid of compact cells for many (MangoWM: 14).
// Paint (.tpp) and click (.cpp) both use these, so the two can never disagree.
inline int layout_popup_entry_count() { return eh::widgets::layout_count(); }
inline const char* layout_popup_entry_id(int idx) { return eh::widgets::layout_id_at(idx); }
inline int layout_popup_columns() { return layout_popup_entry_count() > 6 ? 2 : 1; }
inline int layout_popup_rows() {
  const int n = layout_popup_entry_count();
  const int c = layout_popup_columns();
  return (n + c - 1) / c;
}
inline double layout_popup_cell_h() { return layout_popup_columns() == 1 ? 56.0 : 46.0; }

constexpr double kLayoutPopupPad = 16.0;
constexpr double kLayoutPopupHeaderH = 52.0;
constexpr double kLayoutPopupRowGap = 8.0;

inline void layout_popup_size(double* W, double* H) {
  const int cols = layout_popup_columns();
  const int rows = layout_popup_rows();
  const double ch = layout_popup_cell_h();
  *W = static_cast<double>(kLayoutPopupW);
  *H = kLayoutPopupPad + kLayoutPopupHeaderH + 8.0 + rows * ch + (rows - 1) * kLayoutPopupRowGap +
       kLayoutPopupPad;
  (void)cols;
}

inline void layout_popup_close_rect(double* x, double* y) {
  *x = static_cast<double>(kLayoutPopupW) - kLayoutPopupPad - 24.0;
  *y = kLayoutPopupPad;
}

// Cell rect for entry idx (row-major). Returns false when idx is out of range.
inline bool layout_popup_cell_rect(int idx, double* x, double* y, double* w, double* h) {
  const int n = layout_popup_entry_count();
  if (idx < 0 || idx >= n) return false;
  const int cols = layout_popup_columns();
  const double ch = layout_popup_cell_h();
  const double top = kLayoutPopupPad + kLayoutPopupHeaderH + 8.0;
  if (cols == 1) {
    *x = kLayoutPopupPad;
    *w = static_cast<double>(kLayoutPopupW) - kLayoutPopupPad * 2;
  } else {
    const double cw = (static_cast<double>(kLayoutPopupW) - kLayoutPopupPad * 2 - kLayoutPopupRowGap) * 0.5;
    const int col = idx % cols;
    *x = kLayoutPopupPad + col * (cw + kLayoutPopupRowGap);
    *w = cw;
  }
  const int row = idx / cols;
  *y = top + row * (ch + kLayoutPopupRowGap);
  *h = ch;
  return true;
}

int layout_popup_height();

template <typename A>
void dock_layout_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_layout_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);

enum class LayoutPopupClick { None, Close, Redraw };

[[nodiscard]] LayoutPopupClick layout_popup_click_action(double x, double y);

}  // namespace eh::shell::dock::popup::layout

namespace eh::widgets::popup::layout {
using eh::shell::dock::popup::layout::kLayoutPopupW;
template <typename A>
inline void layout_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::layout::dock_layout_popup_paint(app, cr, sc);
}
inline void layout_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::layout::dock_layout_popup_handle_click(app, x, y, serial);
}
template <typename A>
inline void layout_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::layout::dock_layout_popup_handle_click(app, x, y, serial);
}
}  // namespace eh::widgets::popup::layout

#include "desktop_shell/widgets/popup/layout/layout_popup.tpp"
