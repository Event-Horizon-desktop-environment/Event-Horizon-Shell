#include "core/viewer/app.hpp"

#include <algorithm>
#include <cstring>

namespace hpv {

void App::draw_crop_rect(cairo_t* cr, int win_w, int win_h) {
    (void)win_w;
    (void)win_h;
    if (decoded_image_.width <= 0 || decoded_image_.height <= 0) return;

    int wx1, wy1, wx2, wy2;
    img_to_win(crop_x_, crop_y_, wx1, wy1);
    img_to_win(crop_x_ + crop_w_, crop_y_ + crop_h_, wx2, wy2);

    int rw = wx2 - wx1;
    int rh = wy2 - wy1;
    if (rw < 1 || rh < 1) return;

    // Crop rectangle outline — accent color
    cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.9);
    cairo_set_line_width(cr, 2);
    overlay_.draw_rounded_rect(cr, wx1, wy1, rw, rh, 4);
    cairo_stroke(cr);

    // Lighter fill for the crop area
    cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.08);
    overlay_.draw_rounded_rect(cr, wx1, wy1, rw, rh, 4);
    cairo_fill(cr);

    // Corner handles
    int hl = 10;
    auto draw_handle = [&](int hx, int hy) {
        cairo_rectangle(cr, hx - hl / 2, hy - hl / 2, hl, hl);
        cairo_fill(cr);
    };
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    draw_handle(wx1, wy1);
    draw_handle(wx2, wy1);
    draw_handle(wx1, wy2);
    draw_handle(wx2, wy2);

    // Center cross-hair
    cairo_set_source_rgba(cr, m3::primary_r, m3::primary_g, m3::primary_b, 0.5);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, wx1 + rw / 2, wy1);
    cairo_line_to(cr, wx1 + rw / 2, wy2);
    cairo_stroke(cr);
    cairo_move_to(cr, wx1, wy1 + rh / 2);
    cairo_line_to(cr, wx2, wy1 + rh / 2);
    cairo_stroke(cr);

    // Size label
    char buf[64];
    snprintf(buf, sizeof(buf), "%d \xc3\x97 %d", crop_w_, crop_h_);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13);
    cairo_move_to(cr, wx1 + 6, wy1 - 6);
    cairo_show_text(cr, buf);
}

void App::toggle_crop() {
    if (crop_active_) {
        cancel_crop();
        return;
    }
    if (decoded_image_.width <= 0) return;
    // Cancel markup mode if active
    if (markup_active_) cancel_markup();
    // Initialize crop rectangle to 90% of image, centered
    crop_x_ = decoded_image_.width / 20;
    crop_y_ = decoded_image_.height / 20;
    crop_w_ = decoded_image_.width * 9 / 10;
    crop_h_ = decoded_image_.height * 9 / 10;
    crop_active_ = true;
    render();
}

void App::apply_crop() {
    if (!crop_active_ || decoded_image_.width <= 0 || !svg_source_data_.empty()) return;
    if (crop_w_ <= 0 || crop_h_ <= 0) { cancel_crop(); return; }

    int x = std::max(0, std::min(crop_x_, decoded_image_.width - 1));
    int y = std::max(0, std::min(crop_y_, decoded_image_.height - 1));
    int w = std::min(crop_w_, decoded_image_.width - x);
    int h = std::min(crop_h_, decoded_image_.height - y);
    if (w <= 0 || h <= 0) { cancel_crop(); return; }

    // Extract the cropped region from the RGBA data
    std::vector<uint8_t> cropped((size_t)w * h * 4);
    int src_stride = decoded_image_.width * 4;
    for (int row = 0; row < h; row++) {
        int sy = y + row;
        memcpy(cropped.data() + (size_t)row * w * 4,
               decoded_image_.rgba.data() + (size_t)sy * src_stride + (size_t)x * 4,
               (size_t)w * 4);
    }

    decoded_image_.rgba = std::move(cropped);
    decoded_image_.width = w;
    decoded_image_.height = h;
    decoded_image_.stride = w * 4;

    crop_active_ = false;
    image_modified_ = true;
    update_title();
    render();
}

void App::cancel_crop() {
    crop_active_ = false;
    crop_dragging_ = false;
    render();
}

}
