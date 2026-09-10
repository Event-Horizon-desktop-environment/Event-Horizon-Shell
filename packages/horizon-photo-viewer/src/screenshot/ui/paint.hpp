#pragma once

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <vector>

struct ext_foreign_toplevel_handle_v1;

namespace hpv::sc {

struct AppState;
struct Layout;

void paint_frame(AppState& app);

void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r);

}
