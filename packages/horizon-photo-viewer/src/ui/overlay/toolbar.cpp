#include "ui/overlay.hpp"

namespace hpv {

void Overlay::render_toolbar(cairo_t* cr, int win_w, int win_h,
                             std::vector<OverlayButton>& buttons,
                             int hovered_idx, int pressed_idx, float bg_alpha) {
    (void)win_h;
    (void)bg_alpha;
    int h = kToolbarHeight;

    // Toolbar background — surface (fully opaque)
    cairo_set_source_rgba(cr, m3::surface_r, m3::surface_g, m3::surface_b, 0.92);
    cairo_rectangle(cr, 0, 0, win_w, h);
    cairo_fill(cr);

    // Bottom border — outline-variant
    cairo_set_source_rgba(cr, m3::outline_variant_r, m3::outline_variant_g,
                          m3::outline_variant_b, 0.6);
    cairo_move_to(cr, 0, h);
    cairo_line_to(cr, win_w, h);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    buttons.clear();

    // M3 Icon Button — 48dp touch target, 24dp icon.
    const int btn_size = 48;
    const int btn_y = (h - btn_size) / 2;
    const int icon_pad = 12;             // (48 – 24) / 2
    const int group_gap = 12;
    const int item_gap  = 4;

    auto draw_button = [&](int cx, int cy, int csize, int icon_idx, int btn_idx) {
        if (icon_idx >= (int)icon_surfaces_.size() || !icon_surfaces_[icon_idx])
            return;

        float cr2 = csize * 0.22f; // corner radius ≈ 10 px at 48dp

        // Pressed state layer (on_surface at 12 %)
        if (btn_idx == pressed_idx) {
            cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                  m3::on_surface_variant_b, 0.12);
            draw_rounded_rect(cr, cx + 1, cy + 1, csize - 2, csize - 2, cr2);
            cairo_fill(cr);
        }
        // Hover state layer (on_surface at 8 %, only when not pressed)
        if (btn_idx == hovered_idx && btn_idx != pressed_idx) {
            cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                                  m3::on_surface_variant_b, 0.08);
            draw_rounded_rect(cr, cx + 1, cy + 1, csize - 2, csize - 2, cr2);
            cairo_fill(cr);
        }
        // Icon (Material Symbols, 24x24 effective)
        cairo_surface_t* icon = icon_surfaces_[icon_idx];
        int iw = cairo_image_surface_get_width(icon);
        int ih = cairo_image_surface_get_height(icon);
        double iscale = std::min((double)(csize - icon_pad * 2) / iw,
                                 (double)(csize - icon_pad * 2) / ih);
        if (iscale <= 0) return;
        cairo_save(cr);
        cairo_translate(cr,
            cx + (csize - (int)(iw * iscale)) / 2,
            cy + (csize - (int)(ih * iscale)) / 2);
        cairo_scale(cr, iscale, iscale);
        cairo_set_source_rgba(cr, m3::on_surface_variant_r, m3::on_surface_variant_g,
                              m3::on_surface_variant_b, 1.0);
        cairo_mask_surface(cr, icon, 0, 0);
        cairo_restore(cr);
    };

    int x = 4;  // leading edge
    int btn_idx = 0;

    for (int ti = 0; ti < kNumToolbarItems; ti++) {
        int8_t idx = kToolbarLayout[ti];
        if (idx < 0) {
            x += group_gap;
            continue;
        }
        draw_button(x, btn_y, btn_size, idx, btn_idx);
        buttons.push_back({x, btn_y, btn_size, btn_size, kIconLabels[idx], {}, {}});
        x += btn_size + item_gap;
        btn_idx++;
    }

    // Right-justified: Copy, Panel, Menu, Settings, Info (right to left)
    int rj_x = win_w - 4;
    rj_x -= btn_size; // Settings is rightmost
    draw_button(rj_x, btn_y, btn_size, kSettingsIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kSettingsIconIdx], {}, {}});
    btn_idx++;
    rj_x -= btn_size + 4;
    draw_button(rj_x, btn_y, btn_size, kMetadataIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kMetadataIconIdx], {}, {}});
    btn_idx++;
    rj_x -= btn_size + 4;
    draw_button(rj_x, btn_y, btn_size, kMenuIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kMenuIconIdx], {}, {}});
    btn_idx++;
    rj_x -= btn_size + 4;
    draw_button(rj_x, btn_y, btn_size, kPanelIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kPanelIconIdx], {}, {}});
    btn_idx++;
    rj_x -= btn_size + 4;
    draw_button(rj_x, btn_y, btn_size, kUploadIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kUploadIconIdx], {}, {}});
    btn_idx++;
    rj_x -= btn_size + 4;
    draw_button(rj_x, btn_y, btn_size, kCopyIconIdx, btn_idx);
    buttons.push_back({rj_x, btn_y, btn_size, btn_size, kIconLabels[kCopyIconIdx], {}, {}});
}

} // namespace hpv
