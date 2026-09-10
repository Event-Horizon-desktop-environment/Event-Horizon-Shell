#pragma once

#include <cairo/cairo.h>

struct App;

void workspace_app_geom(int contentX, int contentW, int idx,
                        int& trX, int& trY, int& trW,
                        int& cardX, int& cardY, int& cardW);
void paint_workspaces_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                           double paintPointerYOffset, double dockMatA);
