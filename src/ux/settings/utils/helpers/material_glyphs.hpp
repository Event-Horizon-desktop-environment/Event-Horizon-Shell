#pragma once

#include <cairo/cairo.h>
#include <string>

void material_symbols_draw_glyph(cairo_t* cr, double cx, double cy, double px, const char* ligature, double r, double g,
                                 double b, double a);

[[nodiscard]] const char* dock_widget_material_ligature(const std::string& id);
