#include "ui/overlay.hpp"

#include <cstdio>

namespace hpv {

void Overlay::render_markup_overlay(cairo_t* cr, int win_w, int win_h,
                                    const OverlayState& state) {
    (void)state;
    int top = Overlay::kToolbarHeight;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
    cairo_rectangle(cr, 0, top, win_w, win_h - top);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, win_w / 2 - 80, win_h / 2);
    cairo_show_text(cr, "Markup Mode — draw on the image");
}

} // namespace hpv
