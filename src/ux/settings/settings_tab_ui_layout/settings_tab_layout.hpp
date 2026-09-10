#pragma once

#include <cairo/cairo.h>

struct App;

void paint_layout_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_layout_consume_pointer_down(App& app, int contentX, int contentW);
