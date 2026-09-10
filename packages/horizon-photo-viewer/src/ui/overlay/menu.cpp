#include "ui/overlay.hpp"

namespace hpv {

void Overlay::render_menu_popup(cairo_t* cr, int win_w, int win_h,
                                const OverlayState& state,
                                std::vector<OverlayButton>& buttons) {
    (void)win_h;
    (void)state;
    int pw = 200;
    int item_h = 44;
    const char* items[] = { "Save", "Save As", "Save As Copy", "Upload\u2026" };
    int num_items = sizeof(items) / sizeof(items[0]);
    int ph = 8 + num_items * item_h;
    int px = win_w - pw - 8;
    int py = Overlay::kToolbarHeight + 4;

    // Surface container background
    cairo_set_source_rgba(cr, m3::surface_container_high_r, m3::surface_container_high_g,
                          m3::surface_container_high_b, 0.95);
    draw_rounded_rect(cr, px, py, pw, ph, 10);
    cairo_fill(cr);

    // Outline
    cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                          m3::outline_variant_b, 0.6);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, px, py, pw, ph, 10);
    cairo_stroke(cr);

    // Menu items
    for (int i = 0; i < num_items; i++) {
        int iy = py + 4 + i * item_h;
        // Hover highlight
        // (hover state passed through the button vector is checked at App level)
        cairo_set_source_rgba(cr, m3::on_surface_r, m3::on_surface_g,
                              m3::on_surface_b, 0.87);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);
        cairo_move_to(cr, px + 16, iy + 28);
        cairo_show_text(cr, items[i]);

        buttons.push_back({
            px + 8, iy, pw - 16, item_h, std::string(items[i]), {}, {}
        });
    }
}

} // namespace hpv
