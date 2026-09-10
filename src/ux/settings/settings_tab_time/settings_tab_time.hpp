#pragma once
#include <cairo/cairo.h>
struct App;
void paint_time_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_time_consume_pointer_down(App& app, int contentX, int contentW);
void time_date_format_combo_geom(int contentX, int, int& bx, int& by, int& bw, int& bh);
extern const char* kDateFormatLabels[];
extern const int kDateFormatCount;
