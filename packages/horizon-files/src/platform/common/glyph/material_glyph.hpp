#pragma once

#include <cairo/cairo.h>

namespace eh::shell {

void ensure_material_symbols_font_registered();

void draw_material_glyph(cairo_t* cr, double cx, double cy, double px, const char* ligature, double r, double g, double b,
                         double a);

// Create an ARGB32 cairo surface containing a material glyph rendered at the given size.
// The caller owns the returned surface and must destroy it with cairo_surface_destroy().
cairo_surface_t* create_material_glyph_surface(const char* ligature, int size);

}
