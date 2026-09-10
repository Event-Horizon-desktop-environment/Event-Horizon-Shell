#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <cairo.h>

namespace hpv {

class M3Slider {
public:
    static constexpr float kThumbSize = 24.0f;

    void setRange(float lo, float hi) {
        if (hi < lo) std::swap(lo, hi);
        lo_ = lo; hi_ = hi;
    }

    void setStep(float step) { step_ = std::max(step, 0.0f); }
    void setValue(float val) { value_ = snap(val); }
    void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void setEnabled(bool e) { enabled_ = e; if (!e) { hovered_ = false; dragging_ = false; } }
    void setHovered(bool h) { hovered_ = h; }
    void setShowValueLabel(bool s) { showValueLabel_ = s; }
    void setValueLabel(const char* text) { valueLabel_ = text ? text : ""; }

    void setAccentColor(float r, float g, float b) { accentR_ = r; accentG_ = g; accentB_ = b; }
    void setSurfaceColor(float r, float g, float b) { surfaceR_ = r; surfaceG_ = g; surfaceB_ = b; }
    void setTextColor(float r, float g, float b) { textR_ = r; textG_ = g; textB_ = b; }

    float value() const noexcept { return value_; }
    bool enabled() const noexcept { return enabled_; }
    bool dragging() const noexcept { return dragging_; }
    bool containsPoint(float px, float py) const noexcept {
        return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
    }

    bool handlePointerDown(float px, float py) {
        if (!enabled_ || !containsPoint(px, py)) return false;
        dragging_ = true;
        float tc = thumbCenter();
        float dist = px - tc;
        if (std::abs(dist) <= kThumbSize * 0.5f) {
            grabOffsetX_ = dist;
        } else {
            grabOffsetX_ = 0;
        }
        applyValue(mapXToValue(px - grabOffsetX_));
        return true;
    }

    bool handlePointerMove(float px, float) {
        if (!enabled_ || !dragging_) return false;
        applyValue(mapXToValue(px - grabOffsetX_));
        return true;
    }

    bool handlePointerUp(float, float) {
        if (!enabled_ || !dragging_) return false;
        dragging_ = false;
        return true;
    }

    void paint(cairo_t* cr) const {
        if (w_ <= 0 || h_ <= 0) return;

        const float trackH = 6.0f;
        const float tl = x_ + kThumbSize * 0.5f;
        const float tw = std::max(0.0f, w_ - kThumbSize);
        const float ty = y_ + h_ * 0.5f;
        const float alpha = enabled_ ? 1.0f : 0.38f;
        const float tc = tl + normalized() * tw;

        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

        cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, alpha * 0.35f);
        cairo_set_line_width(cr, trackH);
        cairo_move_to(cr, tl, ty);
        cairo_line_to(cr, tl + tw, ty);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
        cairo_set_line_width(cr, trackH);
        cairo_move_to(cr, tl, ty);
        cairo_line_to(cr, tc, ty);
        cairo_stroke(cr);

        if (enabled_) {
            cairo_set_source_rgba(cr, surfaceR_, surfaceG_, surfaceB_, 0.6f);
            cairo_arc(cr, tl + tw, ty, 2.0f, 0, 2.0 * M_PI);
            cairo_fill(cr);
        }

        const float ts = dragging_ ? kThumbSize * 1.1f : kThumbSize;
        const float thumbR = ts * 0.5f;

        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha);
        cairo_arc(cr, tc, ty, thumbR, 0, 2.0 * M_PI);
        cairo_fill(cr);

        if (showValueLabel_ && dragging_ && !valueLabel_.empty()) {
            drawValuePopup(cr, tc, ty, tw, alpha);
        }
    }

private:
    float snap(float val) const noexcept {
        float clamped = std::clamp(val, lo_, hi_);
        if (step_ <= 0.0f || hi_ <= lo_) return clamped;
        float steps = std::round((clamped - lo_) / step_);
        return std::clamp(lo_ + steps * step_, lo_, hi_);
    }

    float normalized() const noexcept {
        if (hi_ <= lo_) return 0.0f;
        return std::clamp((value_ - lo_) / (hi_ - lo_), 0.0f, 1.0f);
    }

    float thumbCenter() const noexcept {
        float tl = x_ + kThumbSize * 0.5f;
        float tw = std::max(0.0f, w_ - kThumbSize);
        return tl + normalized() * tw;
    }

    float mapXToValue(float px) const noexcept {
        float tl = x_ + kThumbSize * 0.5f;
        float tw = std::max(0.0f, w_ - kThumbSize);
        if (tw <= 0) return lo_;
        float t = (px - tl) / tw;
        t = std::clamp(t, 0.0f, 1.0f);
        return lo_ + t * (hi_ - lo_);
    }

    void applyValue(float raw) {
        float s = snap(raw);
        if (std::abs(s - value_) < 0.0001f) return;
        value_ = s;
    }

    void drawValuePopup(cairo_t* cr, float tc, float ty, float tw, float alpha) const {
        (void)tw;
        float popupW = 48.0f;
        float popupH = 24.0f;
        float popupR = 4.0f;
        float popupX = tc - popupW * 0.5f;
        float popupY = ty - popupH - 8.0f - kThumbSize * 0.5f;

        cairo_save(cr);

        cairo_new_path(cr);
        cairo_arc(cr, popupX + popupR, popupY + popupR, popupR, M_PI, 1.5 * M_PI);
        cairo_arc(cr, popupX + popupW - popupR, popupY + popupR, popupR, 1.5 * M_PI, 2.0 * M_PI);
        cairo_arc(cr, popupX + popupW - popupR, popupY + popupH - popupR, popupR, 0.0, 0.5 * M_PI);
        cairo_arc(cr, popupX + popupR, popupY + popupH - popupR, popupR, 0.5 * M_PI, M_PI);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.9f);
        cairo_fill(cr);

        cairo_move_to(cr, tc - 4, popupY + popupH);
        cairo_line_to(cr, tc, popupY + popupH + 6);
        cairo_line_to(cr, tc + 4, popupY + popupH);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, accentR_, accentG_, accentB_, alpha * 0.9f);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0f, 1.0f, 1.0f, alpha);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 10);
        cairo_text_extents_t te;
        cairo_text_extents(cr, valueLabel_.c_str(), &te);
        cairo_move_to(cr, popupX + (popupW - te.x_advance) * 0.5f, popupY + popupH * 0.5f + 4);
        cairo_show_text(cr, valueLabel_.c_str());

        cairo_restore(cr);
    }

    float lo_ = 0.0f, hi_ = 100.0f, step_ = 1.0f, value_ = 50.0f;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    bool enabled_ = true, hovered_ = false, dragging_ = false;
    bool showValueLabel_ = false;
    std::string valueLabel_;
    float grabOffsetX_ = 0;

    float accentR_ = 0.565f, accentG_ = 0.792f, accentB_ = 0.976f;
    float surfaceR_ = 0.102f, surfaceG_ = 0.110f, surfaceB_ = 0.118f;
    float textR_ = 1.0f, textG_ = 1.0f, textB_ = 1.0f;
};

} // namespace hpv
