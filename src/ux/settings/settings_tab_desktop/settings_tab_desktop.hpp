#pragma once

#include <cairo/cairo.h>

struct App;

void paint_desktop_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_desktop_consume_pointer_down(App& app, int contentX, int contentW);
