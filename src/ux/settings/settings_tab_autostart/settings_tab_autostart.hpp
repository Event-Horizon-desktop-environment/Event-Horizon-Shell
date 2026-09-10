#pragma once

#include <cairo/cairo.h>

struct App;

enum class AutostartField : int {
  None = 0,
  Name,
  Exec,
  Delay,
};

void paint_autostart_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv);
bool settings_autostart_consume_pointer_down(App& app, int contentX, int contentW);
void settings_autostart_consume_pointer_up(App& app);
bool settings_autostart_consume_pointer_move(App& app, int contentX, int contentW);
bool settings_autostart_consume_key(App& app, unsigned sym, unsigned state, const char* utf8, int utf8Len);
void settings_autostart_refresh_entries(App& app);
