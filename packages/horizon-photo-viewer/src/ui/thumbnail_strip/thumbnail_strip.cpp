#include "ui/thumbnail_strip/thumbnail_strip.hpp"

#include <cmath>
#include <algorithm>

namespace hpv {

void ThumbnailStrip::draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_move_to(cr, x + r, y);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
    cairo_close_path(cr);
}

void ThumbnailStrip::render(cairo_t* cr, const ThumbnailStripState& state,
                            std::vector<ThumbnailEntry>& entries) {
    int strip_y = state.win_h - kHeight;

    // Background
    cairo_set_source_rgba(cr, 0.08, 0.08, 0.10, 0.92);
    cairo_rectangle(cr, 0, strip_y, state.win_w, kHeight);
    cairo_fill(cr);

    // Top border
    cairo_set_source_rgba(cr, 0.18, 0.18, 0.20, 0.5);
    cairo_move_to(cr, 0, strip_y);
    cairo_line_to(cr, state.win_w, strip_y);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    if (state.image_count <= 0) return;

    entries.clear();

    int entry_w = kThumbW + kGap;
    int start_x = kMargin - state.scroll_offset;
    int thumb_y = strip_y + (kHeight - kThumbH) / 2;

    for (int i = 0; i < state.image_count; i++) {
        int x = start_x + i * entry_w;
        if (x + kThumbW < 0) continue;
        if (x > state.win_w) break;

        bool is_current = (i == state.image_index);

        // Look up in cache
        cairo_surface_t* surf = nullptr;
        bool cached = false;
        if (state.cache && state.cache_w && state.cache_h) {
            auto it = state.cache->find(i);
            if (it != state.cache->end()) {
                int cw = state.cache_w->at(i);
                int ch = state.cache_h->at(i);
                if (!it->second.empty() && cw > 0 && ch > 0) {
                    surf = cairo_image_surface_create_for_data(
                        const_cast<uint8_t*>(it->second.data()),
                        CAIRO_FORMAT_ARGB32, cw, ch, cw * 4);
                    cached = true;
                }
            }
        }

        if (cached && surf) {
            draw_entry(cr, x, thumb_y, kThumbW, kThumbH, is_current, surf);
            cairo_surface_destroy(surf);
        } else {
            draw_entry(cr, x, thumb_y, kThumbW, kThumbH, is_current, nullptr);
            // Index number while loading
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 10);
            std::string idx = std::to_string(i + 1);
            cairo_text_extents_t ext;
            cairo_text_extents(cr, idx.c_str(), &ext);
            cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
            cairo_move_to(cr,
                x + (kThumbW - ext.width) / 2 - ext.x_bearing,
                thumb_y + (kThumbH - ext.height) / 2 - ext.y_bearing);
            cairo_show_text(cr, idx.c_str());
        }

        entries.push_back({x, thumb_y, kThumbW, kThumbH, i, cached, is_current, {}});
    }
}

void ThumbnailStrip::draw_entry(cairo_t* cr, int x, int y, int w, int h,
                                bool current, cairo_surface_t* thumb_surf) {
    double radius = 4;

    if (thumb_surf) {
        cairo_save(cr);
        draw_rounded_rect(cr, x, y, w, h, radius);
        cairo_clip(cr);
        double sx = (double)w / cairo_image_surface_get_width(thumb_surf);
        double sy = (double)h / cairo_image_surface_get_height(thumb_surf);
        double scale = std::min(sx, sy);
        double ox = x + (w - cairo_image_surface_get_width(thumb_surf) * scale) / 2;
        double oy = y + (h - cairo_image_surface_get_height(thumb_surf) * scale) / 2;
        cairo_translate(cr, ox, oy);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, thumb_surf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_set_source_rgba(cr, 0.15, 0.15, 0.17, 0.9);
        draw_rounded_rect(cr, x, y, w, h, radius);
        cairo_fill(cr);
    }

    if (current) {
        cairo_set_source_rgba(cr, 0.4, 0.6, 1.0, 0.9);
        cairo_set_line_width(cr, 2);
    } else {
        cairo_set_source_rgba(cr, 0.25, 0.25, 0.28, 0.6);
        cairo_set_line_width(cr, 1);
    }
    draw_rounded_rect(cr, x, y, w, h, radius);
    cairo_stroke(cr);
}

}
