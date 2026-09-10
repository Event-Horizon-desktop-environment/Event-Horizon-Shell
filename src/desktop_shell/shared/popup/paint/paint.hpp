#pragma once

#include "desktop_shell/controlcenter/paint/control_center_popup_paint.hpp"

#include <cairo/cairo.h>

struct DockApp;

namespace eh::config {
struct ShellConfig;
}

void dock_popup_paint_spotlight(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv);
void dock_popup_paint_app_menu(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv, bool gpu_path);
void dock_popup_paint_context_menu(DockApp& app, cairo_t* cr, const eh::config::ShellConfig& scPopupOv);
