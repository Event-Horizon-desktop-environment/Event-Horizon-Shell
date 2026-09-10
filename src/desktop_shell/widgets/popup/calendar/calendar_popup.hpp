#pragma once

#include <cstdint>
#include <ctime>

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo cairo_t;

struct DockApp;
namespace eh::config { struct ShellConfig; }

namespace eh::shell::dock::popup::calendar {

constexpr int kCalendarPopupW = 320;
constexpr int kCalendarPopupH = 370;

template<typename A>
void dock_calendar_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_calendar_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);

}

namespace eh::widgets::popup::calendar {
using eh::shell::dock::popup::calendar::kCalendarPopupW;
using eh::shell::dock::popup::calendar::kCalendarPopupH;
template<typename A>
inline void calendar_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::calendar::dock_calendar_popup_paint(app, cr, sc);
}
inline void calendar_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::calendar::dock_calendar_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void calendar_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::calendar::dock_calendar_popup_handle_click(app, x, y, serial);
}
}

#include "desktop_shell/widgets/popup/calendar/calendar_popup.tpp"
