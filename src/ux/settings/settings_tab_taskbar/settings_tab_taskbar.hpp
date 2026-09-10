#pragma once

#include <cairo/cairo.h>

struct App;

void paint_taskbar_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double dockMatA, double paintPointerYOffset, int tbWidgetCardH);

// M3 input dispatch.
bool taskbar_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
bool taskbar_m3_handle_pointer_up(App& app, float px, float py);
bool taskbar_m3_handle_pointer_move(App& app, float px, float py);
void taskbar_m3_handle_pointer_leave(App& app);
bool taskbar_m3_has_active_slider(const App& app);
bool taskbar_m3_is_appearance_child_tab();
bool taskbar_m3_is_widgets_child_tab();
