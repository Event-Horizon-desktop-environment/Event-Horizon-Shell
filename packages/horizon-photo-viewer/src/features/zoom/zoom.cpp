#include "core/viewer/app.hpp"

#include <algorithm>

namespace hpv {

void App::img_to_win(int img_x, int img_y, int& win_x, int& win_y) const {
    if (decoded_image_.width <= 0 || decoded_image_.height <= 0) {
        win_x = img_x; win_y = img_y;
        return;
    }
    float img_w = (float)decoded_image_.width;
    float img_h = (float)decoded_image_.height;
    bool hide_strip = fullscreen_ || slideshow_;
    bool strip_visible = show_thumbnails_ && (!hide_strip || show_thumbnails_hover_);
    int strip_h = (strip_visible && decoded_image_.width > 0 && dir_images_.size() > 1)
                      ? ThumbnailStrip::kHeight : 0;
    int avail_h = window_height_ - (show_toolbar_ ? Overlay::kToolbarHeight : 0) - strip_h;
    int content_w = window_width_ - (show_sidebar_ ? 320 : 0);
    float fit_scale = std::min((float)content_w / img_w, (float)avail_h / img_h) * zoom_;
    int draw_w = std::max(1, (int)(img_w * fit_scale));
    int draw_h = std::max(1, (int)(img_h * fit_scale));
    int offset_x = (content_w - draw_w) / 2 + (int)pan_x_;
    int offset_y = (show_toolbar_ ? Overlay::kToolbarHeight : 0) + (avail_h - draw_h) / 2 + (int)pan_y_;
    float sx = (float)draw_w / img_w;
    float sy = (float)draw_h / img_h;
    win_x = (int)(img_x * sx) + offset_x;
    win_y = (int)(img_y * sy) + offset_y;
}

void App::win_to_img(int win_x, int win_y, int& img_x, int& img_y) const {
    if (decoded_image_.width <= 0 || decoded_image_.height <= 0) {
        img_x = win_x; img_y = win_y;
        return;
    }
    float img_w = (float)decoded_image_.width;
    float img_h = (float)decoded_image_.height;
    bool hide_strip = fullscreen_ || slideshow_;
    bool strip_visible = show_thumbnails_ && (!hide_strip || show_thumbnails_hover_);
    int strip_h = (strip_visible && decoded_image_.width > 0 && dir_images_.size() > 1)
                      ? ThumbnailStrip::kHeight : 0;
    int avail_h = window_height_ - (show_toolbar_ ? Overlay::kToolbarHeight : 0) - strip_h;
    int content_w = window_width_ - (show_sidebar_ ? 320 : 0);
    float fit_scale = std::min((float)content_w / img_w, (float)avail_h / img_h) * zoom_;
    int draw_w = std::max(1, (int)(img_w * fit_scale));
    int draw_h = std::max(1, (int)(img_h * fit_scale));
    int offset_x = (content_w - draw_w) / 2 + (int)pan_x_;
    int offset_y = (show_toolbar_ ? Overlay::kToolbarHeight : 0) + (avail_h - draw_h) / 2 + (int)pan_y_;
    float sx = (float)draw_w / img_w;
    float sy = (float)draw_h / img_h;
    img_x = (sx > 0) ? (int)((win_x - offset_x) / sx) : 0;
    img_y = (sy > 0) ? (int)((win_y - offset_y) / sy) : 0;
}

void App::zoom_in() {
    zoom_ *= 1.25f;
    render();
}

void App::zoom_out() {
    zoom_ /= 1.25f;
    if (zoom_ < 0.1f) zoom_ = 0.1f;
    render();
}

void App::zoom_fit() {
    zoom_ = 1.0f;
    pan_x_ = 0.0f;
    pan_y_ = 0.0f;
    render();
}

void App::zoom_1to1() {
    if (decoded_image_.width == 0) return;
    float fit_scale = std::min(
        (float)window_width_ / (float)decoded_image_.width,
        (float)window_height_ / (float)decoded_image_.height
    );
    if (fit_scale > 0.0f) zoom_ = 1.0f / fit_scale;
    render();
}

}
