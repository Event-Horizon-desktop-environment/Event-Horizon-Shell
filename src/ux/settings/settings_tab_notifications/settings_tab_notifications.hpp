#pragma once

#include <cairo/cairo.h>

struct App;

void paint_notifications_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_notifications_consume_pointer_down(App& app, int contentX, int contentW);

// Display-output dropdown (notifications tab, Settings child).
int notif_display_band_top();
void notif_display_dd_sync(App& app, int contentX, int contentW);
bool notif_display_dd_commit_pointer_up(App& app, float px, float py, int contentX, int contentW);

void notif_app_geom(int contentX, int contentW, int idx,
                    int& trX, int& trY, int& trW,
                    int& cardX, int& cardY, int& cardW);

void notifications_primary_test_button_geom(int contentX, int contentW, int& x, int& y, int& w, int& h);

bool apply_notifications_slider_x(App& app, int row, double px);
