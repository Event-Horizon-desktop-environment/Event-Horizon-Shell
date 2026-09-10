#pragma once

#include <cairo/cairo.h>

struct App;

void paint_hyprland_anim_popup(App& app, cairo_t* cr, int contentX, int contentW);
bool hyprland_anim_popup_pointer_down(App& app, int contentX, int contentW);
void hyprland_anim_popup_motion(App& app, int contentX, int contentW, bool& needDdDraw);
