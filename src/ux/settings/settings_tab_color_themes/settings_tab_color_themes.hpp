#pragma once
#include <cairo/cairo.h>
struct App;
void paint_color_themes_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_color_themes_consume_pointer_down(App& app, int contentX, int contentW);
bool settings_color_themes_consume_pointer_move(App& app, int contentX, int contentW);
void settings_clamp_color_themes_scroll(App& app, int contentW);
