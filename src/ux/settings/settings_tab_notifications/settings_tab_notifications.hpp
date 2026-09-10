#pragma once

#include <cairo/cairo.h>

struct App;

void paint_notifications_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_notifications_consume_pointer_down(App& app, int contentX, int contentW);

void notif_app_geom(int contentX, int contentW, int idx,
                    int& trX, int& trY, int& trW,
                    int& cardX, int& cardY, int& cardW);

void notifications_primary_test_button_geom(int contentX, int contentW, int& x, int& y, int& w, int& h);

bool apply_notifications_slider_x(App& app, int row, double px);
