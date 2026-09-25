#pragma once
#include <cairo/cairo.h>
struct App;
void paint_time_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_time_consume_pointer_down(App& app, int contentX, int contentW);
void time_date_format_combo_geom(int contentX, int, int& bx, int& by, int& bw, int& bh);
void time_custom_format_field_geom(int contentX, int contentW, int& bx, int& by, int& bw, int& bh);
int time_content_bottom(const App& app);
bool time_zone_picker_visible(const App& app);
bool time_zone_picker_consume_scroll(App& app, double delta_px);
void paint_time_zone_picker(App& app, cairo_t* cr);
bool time_zone_picker_consume_pointer_down(App& app);
bool time_zone_picker_consume_pointer_move(App& app);
bool settings_time_consume_key(App& app, unsigned sym, unsigned state, const char* utf8, int utf8Len);
extern const char* kDateFormatLabels[];
extern const int kDateFormatCount;
