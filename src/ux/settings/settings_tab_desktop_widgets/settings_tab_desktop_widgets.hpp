#pragma once

#include <cairo/cairo.h>

struct App;

void paint_desktop_widgets_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                                double paintPointerYOffset);

bool settings_desktop_widgets_handle_remove_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_toggle_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_settings_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_add_click(App& app, int contentX, int contentW);
