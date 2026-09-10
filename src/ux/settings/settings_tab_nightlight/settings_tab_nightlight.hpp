#pragma once
#include <cairo/cairo.h>
struct App;
void paint_nightlight_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool nightlight_consume_pointer_down(App& app, int contentX, int contentW);
bool nightlight_consume_pointer_move(App& app, int contentX, int contentW);
