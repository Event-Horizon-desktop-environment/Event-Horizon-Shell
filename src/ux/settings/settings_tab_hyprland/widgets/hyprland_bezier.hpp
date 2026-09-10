#pragma once

#include <cairo/cairo.h>

struct App;

void paint_hyprland_bezier_editor(App& app, cairo_t* cr, int contentX, int contentW);
bool hyprland_bezier_pointer_down(App& app, int contentX, int contentW);
void hyprland_bezier_motion(App& app, int contentX, int contentW, bool& needDdDraw);
