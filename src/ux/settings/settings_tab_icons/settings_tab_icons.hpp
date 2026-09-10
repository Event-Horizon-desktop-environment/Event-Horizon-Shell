#pragma once

#include <cairo/cairo.h>

struct App;

void paint_icons_tab(App& app, cairo_t* cr, int contentX, int contentW,
                     double cardX, double cardW, double glassOv,
                     double dockMatA, double paintPointerYOffset);

bool settings_icons_consume_pointer_down(App& app, int contentX, int contentW);

int icons_tab_content_bottom_px(const App& app);
int settings_icons_scroll_max_px(const App& app);
void settings_clamp_icons_scroll_px(App& app);
void clear_icons_preview_cache();
void icons_backup_reset();
