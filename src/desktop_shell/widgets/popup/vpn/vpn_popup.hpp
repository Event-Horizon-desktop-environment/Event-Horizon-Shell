#pragma once

#include <cstdint>

typedef struct _cairo cairo_t;
struct DockApp;
namespace eh::config { struct ShellConfig; }

namespace eh::shell::dock::popup::vpn {

constexpr int kVpnPopupW = 320;

int vpn_popup_height(int vpnCount);

template<typename A>
void dock_vpn_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc);

void dock_vpn_popup_handle_click(DockApp& app, double x, double y, uint32_t serial);
}

namespace eh::widgets::popup::vpn {
using eh::shell::dock::popup::vpn::kVpnPopupW;
template<typename A>
inline void vpn_popup_paint(const A& app, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::shell::dock::popup::vpn::dock_vpn_popup_paint(app, cr, sc);
}
inline void vpn_popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::vpn::dock_vpn_popup_handle_click(app, x, y, serial);
}
template<typename A>
inline void vpn_popup_handle_click(A& app, double x, double y, uint32_t serial) {
  eh::shell::dock::popup::vpn::dock_vpn_popup_handle_click(app, x, y, serial);
}
}

#include "desktop_shell/widgets/popup/vpn/vpn_popup.tpp"
