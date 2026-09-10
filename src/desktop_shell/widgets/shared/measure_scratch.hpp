#pragma once
#include <cairo/cairo.h>

namespace eh::widgets {

inline cairo_t* get_measure_cr() {
  static cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  static cairo_t* cr = cairo_create(surf);
  return cr;
}

}
