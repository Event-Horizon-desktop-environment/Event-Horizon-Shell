#pragma once

#include "decode/common/svg_doc.hpp"

#include <cairo.h>

struct NSVGimage;

namespace hpv {

// Render a pre-parsed NanoSVG image directly to Cairo as vector paths.
// The SVG is drawn at its native viewbox coordinates, maintaining aspect ratio
// to fill (0,0,target_w,target_h). Caller should set up any additional transforms.
void render_svg_cairo(cairo_t* cr, const NSVGimage* img,
                       float target_w, float target_h);

}
