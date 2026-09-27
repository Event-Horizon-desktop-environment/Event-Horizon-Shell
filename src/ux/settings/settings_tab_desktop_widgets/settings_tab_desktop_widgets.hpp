#pragma once

#include <cairo/cairo.h>

struct App;

void paint_desktop_widgets_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                                double paintPointerYOffset);

bool settings_desktop_widgets_handle_remove_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_toggle_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_settings_click(App& app, int contentX, int contentW);
bool settings_desktop_widgets_handle_add_click(App& app, int contentX, int contentW);

// Display-output dropdown (desktop widgets tab).
int desktop_widgets_display_band_top();
void desktop_widgets_display_dd_sync(App& app, int contentX, int contentW);
bool desktop_widgets_display_dd_handle_pointer_down(App& app, int contentX, int contentW);
bool desktop_widgets_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW);
bool desktop_widgets_display_dd_update_hover(App& app, int contentX, int contentW);
