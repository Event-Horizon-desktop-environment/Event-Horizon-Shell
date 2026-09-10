#include "platform/common/glyph/material_glyph.hpp"
#include "platform/common/glyph/bundled_assets.hpp"

#include <fontconfig/fontconfig.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace eh::shell {

void ensure_material_symbols_font_registered() {
  static bool done = false;
  if (done) return;
  done = true;
  bundled_try_register_material_symbols_fontconfig();
}

void draw_material_glyph(cairo_t* cr, double cx, double cy, double px,
                          const char* ligature,
                          double r, double g, double b, double a) {
  ensure_material_symbols_font_registered();

  cairo_save(cr);

  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_select_font_face(cr, "Material Symbols Rounded",
                         CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, px);

  cairo_text_extents_t ext;
  cairo_text_extents(cr, ligature, &ext);

  double tx = cx - ext.width / 2.0 - ext.x_bearing;
  double ty = cy - ext.height / 2.0 - ext.y_bearing;

  cairo_move_to(cr, tx, ty);
  cairo_show_text(cr, ligature);

  cairo_restore(cr);
}

cairo_surface_t* create_material_glyph_surface(const char* ligature, int size) {
  ensure_material_symbols_font_registered();

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  cairo_t* cr = cairo_create(surf);

  cairo_set_source_rgba(cr, 1, 1, 1, 1);
  cairo_paint(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 1);
  cairo_select_font_face(cr, "Material Symbols Rounded",
                         CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size * 0.8);

  cairo_text_extents_t ext;
  cairo_text_extents(cr, ligature, &ext);
  cairo_move_to(cr,
    (size - ext.width) / 2.0 - ext.x_bearing,
    (size - ext.height) / 2.0 - ext.y_bearing);
  cairo_show_text(cr, ligature);

  cairo_destroy(cr);
  return surf;
}

}
