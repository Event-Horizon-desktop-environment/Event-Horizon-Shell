#include "desktop_shell/shared/popup/paint/paint.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "../app_drawer/trace/app_drawer_trace.hpp"

#include <cairo/cairo.h>
#include <string>

#include "configuration/shell_config.hpp"

void dock_popup_paint_app_menu(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv, bool gpu_path) {
 
if (eh_app_drawer_debug_level() >= 2) {
  eh::shell::dock::app_drawer::trace_line(2, "dock",
                             "popup_draw_surface AppMenu gpu=" + std::string(gpu_path ? "1" : "0") + " buf " +
                                 std::to_string(app.popupW) + "x" + std::to_string(app.popupH) + " hits=" +
                                 std::to_string(app.appMenuHits.size()) + " sel=" + std::to_string(app.appMenuSel) +
                                 " searchFocused=" + std::string(app.appMenuSearchFocused ? "1" : "0"));
}
const float alpha = static_cast<float>(eh::config::overlay_surface_alpha_scale(
    scPopupOv, eh::config::OverlaySurfaceAlphaKind::AppDrawer));
eh_app_drawer_paint(app, cr,  true,  true, alpha);
}
