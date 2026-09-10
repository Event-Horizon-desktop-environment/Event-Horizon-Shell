#pragma once

#include <cairo/cairo.h>

struct App;

// WiFi tab (tab id 17)
void paint_wifi_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv, double paintPointerYOffset);
bool settings_wifi_consume_pointer_down(App& app, int contentX, int contentW);
bool settings_wifi_consume_pointer_up(App& app, int contentX, int contentW);

// Wired tab (tab id 18)
void paint_wired_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_wired_consume_pointer_down(App& app, int contentX, int contentW);
bool settings_wired_consume_pointer_up(App& app, int contentX, int contentW);
void settings_wired_handle_pointer_leave(App& app);

// VPN tab (tab id 28)
void paint_vpn_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_vpn_consume_pointer_down(App& app, int contentX, int contentW);
