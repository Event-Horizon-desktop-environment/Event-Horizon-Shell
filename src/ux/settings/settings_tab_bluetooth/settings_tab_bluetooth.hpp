#pragma once

#include <cairo/cairo.h>

struct App;

void paint_bluetooth_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_bluetooth_consume_pointer_down(App& app, int contentX, int contentW);
void settings_bluetooth_consume_pointer_move(App& app, int contentX, int contentW);
// Page-coordinate bottom of the bluetooth tab content (for scroll clamping).
int bluetooth_tab_content_bottom();
