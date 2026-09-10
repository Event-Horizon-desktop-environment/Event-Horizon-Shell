#pragma once

#include <cairo/cairo.h>

#include <cstdint>

namespace hpv::sc {

struct AppState;

void draw_vista_button(cairo_t* cr, const AppState& app,
                       double x, double y, double w, double h,
                       const char* label, const char* sublabel,
                       bool active, bool hovered, bool pressed);

}
