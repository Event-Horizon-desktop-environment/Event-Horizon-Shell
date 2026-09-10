#include "ui/overlay.hpp"

#include <cstdio>

namespace hpv {

void Overlay::render_crop_overlay(cairo_t* cr, int win_w, int win_h,
                                  const OverlayState& state,
                                  std::vector<OverlayButton>& buttons) {
    (void)state;
    // Semi-transparent dim overlay over the image area
    int top = Overlay::kToolbarHeight;
    cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
    cairo_rectangle(cr, 0, top, win_w, win_h - top);
    cairo_fill(cr);

    // Draw crop rectangle (coordinates are in image space, so we need to
    // map them through zoom/pan). Since we don't have zoom/pan here,
    // we use the passed state fields.
    int img_w = state.image_width;
    int img_h = state.image_height;
    if (img_w <= 0 || img_h <= 0) return;

    // We don't have direct access to crop rect coordinates from OverlayState,
    // so we draw a simple initial rectangle centered on the image.
    // The actual interactive rectangle is passed via buttons or drawn by App.
    // For now just draw a helpful hint:
    (void)buttons;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_move_to(cr, win_w / 2 - 80, win_h / 2);
    cairo_show_text(cr, "Crop Mode — drag to area");
}

} // namespace hpv
