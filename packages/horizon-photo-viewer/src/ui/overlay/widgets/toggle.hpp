#pragma once

#include <cairo.h>

namespace hpv {

class M3Toggle {
public:
    void setOn(bool on) { on_ = on; }
    bool isOn() const noexcept { return on_; }
    void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void setEnabled(bool e) { enabled_ = e; }
    void setHovered(bool h) { hovered_ = h; }

    void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
    void setOutlineColor(float r, float g, float b) { outlineR_ = r; outlineG_ = g; outlineB_ = b; }

    bool containsPoint(float px, float py) const noexcept {
        return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
    }

    void paint(cairo_t* cr) const {
        float trackH = h_;
        float trackW = w_;
        float tg_x = x_;
        float tg_y = y_;

        if (on_) {
            cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, 1.0);
            cairo_new_path(cr);
            cairo_arc(cr, tg_x + trackH * 0.5f, tg_y + trackH * 0.5f, trackH * 0.5f, M_PI_2, -M_PI_2);
            cairo_arc(cr, tg_x + trackW - trackH * 0.5f, tg_y + trackH * 0.5f, trackH * 0.5f, -M_PI_2, M_PI_2);
            cairo_close_path(cr);
            cairo_fill(cr);
        } else {
            cairo_set_source_rgba(cr, outlineR_, outlineG_, outlineB_, 1.0);
            cairo_set_line_width(cr, 2.0);
            cairo_new_path(cr);
            cairo_arc(cr, tg_x + trackH * 0.5f, tg_y + trackH * 0.5f, trackH * 0.5f - 1.0f, M_PI_2, -M_PI_2);
            cairo_arc(cr, tg_x + trackW - trackH * 0.5f, tg_y + trackH * 0.5f, trackH * 0.5f - 1.0f, -M_PI_2, M_PI_2);
            cairo_close_path(cr);
            cairo_stroke(cr);
        }

        float thumbR = trackH * 0.38f;
        float thumb_cx = on_ ? tg_x + trackW - trackH / 2 : tg_x + trackH / 2;
        float thumb_cy = tg_y + trackH / 2;
        cairo_set_source_rgba(cr, 1.0f, 1.0f, 1.0f, 1.0f);
        cairo_arc(cr, thumb_cx, thumb_cy, thumbR, 0, 2 * M_PI);
        cairo_fill(cr);
    }

private:
    bool on_ = false;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    bool enabled_ = true, hovered_ = false;
    float accentR_ = 0.565f, accentG_ = 0.792f, accentB_ = 0.976f;
    float outlineR_ = 0.557f, outlineG_ = 0.565f, outlineB_ = 0.600f;
};

} // namespace hpv
