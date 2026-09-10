#pragma once

#include <cairo/cairo.h>

struct App;

void paint_dock_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double dockMatA, double paintPointerYOffset, int chDock, int autoBarHeightPx);

// M3 input dispatch helpers, called from settings_event_handlers.cpp.
bool dock_m3_handle_pointer_down(App& app, float px, float py, int contentX, int contentW);
bool dock_m3_handle_pointer_up(App& app, float px, float py);
bool dock_m3_handle_pointer_move(App& app, float px, float py);
void dock_m3_handle_pointer_leave(App& app);
bool dock_m3_has_active_slider(const App& app);
bool dock_m3_is_appearance_child_tab();
bool dock_m3_is_widgets_child_tab();

// Renderer dropdown (dock tab, Settings child): shared sync + up-commit hook
// used by popup painting and the centralized event handlers.
void dock_renderer_dd_sync(App& app, int contentX, int contentW);
bool dock_renderer_dd_commit_pointer_up(App& app, float px, float py);
