#pragma once

#include <cairo/cairo.h>

struct App;

void paint_mango_tab(App& app, cairo_t* cr, int contentX, int contentW,
                     double cardX, double cardW, double glassOv,
                     double dockMatA, double paintPointerYOffset, int activeSubTab);

bool settings_mango_consume_pointer_down(App& app, int contentX, int contentW, int activeSubTab);

void settings_clamp_mango_scroll_px(App& app);

// Called from event handler during slider drag + on release
bool mango_slider_apply(App& app, int sliderIdx, double px);
void mango_commit_cfg(App& app);
