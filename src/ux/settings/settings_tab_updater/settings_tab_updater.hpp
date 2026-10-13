#pragma once

#include <cairo/cairo.h>

struct App;

void paint_updater_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_updater_consume_pointer_down(App& app, int contentX, int contentW);

// Bottom extent of the updater tab content (for scroll clamp / tests).
int updater_content_bottom(int contentX, int contentW);
