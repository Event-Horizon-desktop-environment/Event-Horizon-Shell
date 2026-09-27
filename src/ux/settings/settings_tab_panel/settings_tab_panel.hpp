#pragma once

#include <cairo/cairo.h>

struct App;

void paint_panel_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double dockMatA,
                     double paintPointerYOffset, int widgetCardH);

bool panel_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
bool panel_m3_handle_pointer_up(App& app, float px, float py);
bool panel_m3_handle_pointer_move(App& app, float px, float py);
void panel_m3_handle_pointer_leave(App& app);
bool panel_m3_has_active_slider(const App& app);
bool panel_m3_is_appearance_child_tab();
bool panel_m3_is_widgets_child_tab();

// Display-output dropdown (panel tab, Settings child).
void panel_display_dd_sync(App& app, int contentX, int contentW);
bool panel_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW);
