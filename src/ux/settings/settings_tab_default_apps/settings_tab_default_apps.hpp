#pragma once

#include <cairo/cairo.h>
#include <string>

struct App;

namespace eh::settings::default_apps {
struct DefaultAppsLayout;
}

void paint_default_apps_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_default_apps_consume_pointer_down(App& app, int contentX, int contentW);

void settings_paint_default_app_picker_popup(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
void settings_fill_default_app_picker(App& app);
void default_app_picker_teardown_layer(App& app);
bool default_app_picker_popup_geom(const App& app, int contentX, int contentW, int* out_lx, int* out_ly,
                                   int* out_lw, int* out_view_h, int* out_row_h, int* out_n_rows,
                                   int* out_max_scroll);
int default_apps_hit_pill_row(const App& app, double px, double py,
                              const eh::settings::default_apps::DefaultAppsLayout& daLay);
const std::string* default_apps_saved_field(const App& app, int r);
