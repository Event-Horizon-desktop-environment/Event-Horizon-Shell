#pragma once

#include <cairo/cairo.h>

struct App;

void paint_bing_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);

void handle_bing_click(App& app, int contentX, int contentW);
