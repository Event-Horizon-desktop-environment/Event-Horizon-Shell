#pragma once
#include <cairo/cairo.h>
struct App;
void paint_power_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_power_consume_pointer_down(App& app, int contentX, int contentW);
void power_btn_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh);
void lid_close_combo_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh);
void power_gov_dd_sync(App& app, int contentX, int contentW);
void power_epp_dd_sync(App& app, int contentX, int contentW);
void power_tuned_dd_sync(App& app, int contentX, int contentW);
bool settings_power_consume_performance_dropdown_pointer_up(App& app, int contentX, int contentW);
