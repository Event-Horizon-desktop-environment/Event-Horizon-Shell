#pragma once

#include <cairo/cairo.h>

struct App;

void paint_taskflip_tab(App& app, cairo_t* cr, int contentX, int contentW);
bool taskflip_tab_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
void taskflip_tab_sync(App& app, int contentX, int contentW);
