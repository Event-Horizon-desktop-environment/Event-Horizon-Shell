#pragma once
#include <cairo/cairo.h>
struct App;

void paint_themes_tab(App& app, cairo_t* cr, int contentX, int contentW,
                      double cardX, double cardW, double glassOv,
                      double dockMatA, double paintPointerYOffset);
bool settings_themes_consume_pointer_down(App& app, int contentX, int contentW);
int themes_tab_content_bottom_px(const App& app);
int settings_themes_scroll_max_px(const App& app);
void settings_clamp_themes_scroll_px(App& app);
void themes_backup_reset();
const std::string& get_pending_qt_color_scheme();
void set_pending_qt_color_scheme(const std::string& scheme);
