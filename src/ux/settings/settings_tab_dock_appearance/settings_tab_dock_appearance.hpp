#pragma once

#include <cairo/cairo.h>

struct App;

void paint_dock_appearance_tab(App& app, cairo_t* cr, int contentX, int contentW,
                                double glassOv, double paintPointerYOffset);

int dock_appearance_tab_scroll_max_px();

bool dock_appearance_consume_pointer_down(App& app, int contentX, int contentW);
bool dock_appearance_consume_pointer_move(App& app, int contentX, int contentW);
