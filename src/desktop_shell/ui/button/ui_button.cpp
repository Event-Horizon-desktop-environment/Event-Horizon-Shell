#include "desktop_shell/ui/button/ui_button.hpp"

#include <algorithm>
#include <cmath>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

namespace eh::ui {

Button::Button() = default;

void Button::setLabel(std::string_view text) {
   
  label_ = text;
}

void Button::setGlyph(std::string_view name) {
   
  glyph_ = name;
}

void Button::setStyle(ButtonStyle style) {
   
  style_ = style;
}

void Button::setEnabled(bool enabled) {
   
  enabled_ = enabled;
  if (!enabled_) {
    hovered_ = false;
    pressed_ = false;
  }
}

void Button::setMinSize(float w, float h) {
   
  minW_ = w;
  minH_ = h;
}

void Button::setFontSize(float size) {
   
  fontSize_ = size;
}

void Button::setAccentColor(float r, float g, float b) {
   
  accentR_ = r; accentG_ = g; accentB_ = b;
}

void Button::setSurfaceColor(float r, float g, float b) {
   
  surfaceR_ = r; surfaceG_ = g; surfaceB_ = b;
}

void Button::setTextColor(float r, float g, float b) {
   
  textR_ = r; textG_ = g; textB_ = b;
}

void Button::setOutlineColor(float r, float g, float b) {
   
  outlineR_ = r; outlineG_ = g; outlineB_ = b;
}

void Button::setHovered(bool h) { hovered_ = h; }

void Button::setPressed(bool p) { pressed_ = p; }

void Button::setOnClick(std::function<void()> cb) {
   
  onClick_ = std::move(cb);
}

void Button::setOnPress(std::function<void()> cb) {
   
  onPress_ = std::move(cb);
}

void Button::setOnRelease(std::function<void()> cb) {
   
  onRelease_ = std::move(cb);
}

void Button::setGeometry(float x, float y, float w, float h) {
   
  x_ = x;
  y_ = y;
  w_ = std::max(w, minW_);
  h_ = std::max(h, minH_);
}

bool Button::containsPoint(float px, float py) const noexcept {
  return px >= x_ && px < x_ + w_ && py >= y_ && py < y_ + h_;
}

bool Button::handlePointerEnter(float /*px*/, float /*py*/) {
   
  if (!enabled_) return false;
  hovered_ = true;
  return true;
}

bool Button::handlePointerLeave() {
   
  hovered_ = false;
  pressed_ = false;
  return true;
}

bool Button::handlePointerDown(float px, float py) {
   
  if (!enabled_ || !containsPoint(px, py)) return false;
  pressed_ = true;
  if (onPress_) onPress_();
  return true;
}

bool Button::handlePointerUp(float px, float py) {
   
  if (!enabled_) return false;
  const bool wasPressed = pressed_;
  pressed_ = false;
  if (wasPressed && containsPoint(px, py)) {
    if (onClick_) onClick_();
  }
  if (onRelease_) onRelease_();
  return wasPressed;
}

void Button::resolveColors(
    float& bg_r, float& bg_g, float& bg_b, float& bg_a,
    float& fg_r, float& fg_g, float& fg_b, float& fg_a,
    float& border_r, float& border_g, float& border_b, float& border_a,
    float& border_w) const {
   
  auto set = [](float& r, float& g, float& b, float& a, float sr, float sg, float sb, float sa) {
    r = sr; g = sg; b = sb; a = sa;
  };

  auto darken = [](float v, float t) { return v * (1.0f - t); };
  auto blend = [](float a, float b, float t) { return a + (b - a) * t; };

  border_w = 0.0f;

  if (!enabled_) {
    set(bg_r, bg_g, bg_b, bg_a, surfaceR_, surfaceG_, surfaceB_, 0.38f);
    set(fg_r, fg_g, fg_b, fg_a, textR_, textG_, textB_, 0.38f);
    set(border_r, border_g, border_b, border_a, textR_, textG_, textB_, 0.12f);
    return;
  }

  if (pressed_) {
    switch (style_) {
      case ButtonStyle::Filled:
        set(bg_r, bg_g, bg_b, bg_a, darken(accentR_, 0.25f), darken(accentG_, 0.25f), darken(accentB_, 0.25f), 1.0f);
        set(fg_r, fg_g, fg_b, fg_a, 1.0f, 1.0f, 1.0f, 1.0f);
        break;
      case ButtonStyle::Tonal: {
        const float cr = blend(accentR_, surfaceR_, 0.55f);
        const float cg = blend(accentG_, surfaceG_, 0.55f);
        const float cb = blend(accentB_, surfaceB_, 0.55f);
        set(bg_r, bg_g, bg_b, bg_a, darken(cr, 0.20f), darken(cg, 0.20f), darken(cb, 0.20f), 1.0f);
        set(fg_r, fg_g, fg_b, fg_a, surfaceR_, surfaceG_, surfaceB_, 1.0f);
        break;
      }
      case ButtonStyle::Outlined:
        set(bg_r, bg_g, bg_b, bg_a, accentR_, accentG_, accentB_, 0.12f);
        set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
        set(border_r, border_g, border_b, border_a, accentR_, accentG_, accentB_, 1.0f);
        border_w = 1.0f;
        break;
      case ButtonStyle::Ghost:
        set(bg_r, bg_g, bg_b, bg_a, accentR_, accentG_, accentB_, 0.12f);
        set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
        break;
    }
    return;
  }

  if (hovered_) {
    switch (style_) {
      case ButtonStyle::Filled:
        set(bg_r, bg_g, bg_b, bg_a, darken(accentR_, 0.10f), darken(accentG_, 0.10f), darken(accentB_, 0.10f), 1.0f);
        set(fg_r, fg_g, fg_b, fg_a, 1.0f, 1.0f, 1.0f, 1.0f);
        break;
      case ButtonStyle::Tonal: {
        const float cr = blend(accentR_, surfaceR_, 0.55f);
        const float cg = blend(accentG_, surfaceG_, 0.55f);
        const float cb = blend(accentB_, surfaceB_, 0.55f);
        set(bg_r, bg_g, bg_b, bg_a, darken(cr, 0.05f), darken(cg, 0.05f), darken(cb, 0.05f), 1.0f);
        set(fg_r, fg_g, fg_b, fg_a, surfaceR_, surfaceG_, surfaceB_, 1.0f);
        break;
      }
      case ButtonStyle::Outlined:
        set(bg_r, bg_g, bg_b, bg_a, accentR_, accentG_, accentB_, 0.08f);
        set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
        set(border_r, border_g, border_b, border_a, accentR_, accentG_, accentB_, 1.0f);
        border_w = 1.0f;
        break;
      case ButtonStyle::Ghost:
        set(bg_r, bg_g, bg_b, bg_a, accentR_, accentG_, accentB_, 0.08f);
        set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
        break;
    }
    return;
  }

  switch (style_) {
    case ButtonStyle::Filled:
      set(bg_r, bg_g, bg_b, bg_a, accentR_, accentG_, accentB_, 1.0f);
      set(fg_r, fg_g, fg_b, fg_a, 1.0f, 1.0f, 1.0f, 1.0f);
      break;
    case ButtonStyle::Tonal: {
      const float cr = blend(accentR_, surfaceR_, 0.55f);
      const float cg = blend(accentG_, surfaceG_, 0.55f);
      const float cb = blend(accentB_, surfaceB_, 0.55f);
      set(bg_r, bg_g, bg_b, bg_a, cr, cg, cb, 1.0f);
      set(fg_r, fg_g, fg_b, fg_a, surfaceR_, surfaceG_, surfaceB_, 1.0f);
      break;
    }
    case ButtonStyle::Outlined:
      set(bg_r, bg_g, bg_b, bg_a, 0.0f, 0.0f, 0.0f, 0.0f);
      set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
      set(border_r, border_g, border_b, border_a, outlineR_, outlineG_, outlineB_, 1.0f);
      border_w = 1.0f;
      break;
    case ButtonStyle::Ghost:
      set(bg_r, bg_g, bg_b, bg_a, 0.0f, 0.0f, 0.0f, 0.0f);
      set(fg_r, fg_g, fg_b, fg_a, accentR_, accentG_, accentB_, 1.0f);
      break;
  }
}

void Button::paint(cairo_t* cr) const {
   
  if (w_ <= 0.0f || h_ <= 0.0f) return;

  float bg_r, bg_g, bg_b, bg_a;
  float fg_r, fg_g, fg_b, fg_a;
  float border_r, border_g, border_b, border_a, border_w;

  resolveColors(bg_r, bg_g, bg_b, bg_a,
                fg_r, fg_g, fg_b, fg_a,
                border_r, border_g, border_b, border_a,
                border_w);

  const float radius = std::min(w_, h_) * 0.25f;

  cairo_save(cr);

  cairo_translate(cr, x_, y_);

  if (bg_a > 0.0f) {
    cairo_new_path(cr);
    cairo_arc(cr, radius, radius, radius, M_PI, 1.5 * M_PI);
    cairo_arc(cr, w_ - radius, radius, radius, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, w_ - radius, h_ - radius, radius, 0.0, 0.5 * M_PI);
    cairo_arc(cr, radius, h_ - radius, radius, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, bg_a);
    cairo_fill(cr);
  }

  if (border_w > 0.0f) {
    cairo_new_path(cr);
    cairo_arc(cr, radius, radius, radius - border_w * 0.5f, M_PI, 1.5 * M_PI);
    cairo_arc(cr, w_ - radius, radius, radius - border_w * 0.5f, 1.5 * M_PI, 2.0 * M_PI);
    cairo_arc(cr, w_ - radius, h_ - radius, radius - border_w * 0.5f, 0.0, 0.5 * M_PI);
    cairo_arc(cr, radius, h_ - radius, radius - border_w * 0.5f, 0.5 * M_PI, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, border_r, border_g, border_b, border_a);
    cairo_set_line_width(cr, border_w);
    cairo_stroke(cr);
  }

  if (!glyph_.empty()) {
    auto* gl = pango_cairo_create_layout(cr);
    auto* gd = pango_font_description_new();
    pango_font_description_set_family(gd, "Material Symbols Rounded");
    pango_font_description_set_weight(gd, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(gd, static_cast<int>(fontSize_ * PANGO_SCALE));
    pango_layout_set_font_description(gl, gd);
    {
      PangoAttribute* fea = pango_attr_font_features_new("liga");
      if (fea) {
        fea->start_index = 0;
        fea->end_index = G_MAXUINT;
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, fea);
        pango_layout_set_attributes(gl, attrs);
        pango_attr_list_unref(attrs);
      }
    }
    pango_layout_set_text(gl, glyph_.data(), static_cast<int>(glyph_.size()));

    int gw = 0, gh = 0;
    pango_layout_get_pixel_size(gl, &gw, &gh);

    if (!label_.empty()) {
      auto* ll = pango_cairo_create_layout(cr);
      auto* ld = pango_font_description_from_string("Inter");
      pango_font_description_set_size(ld, fontSize_ * PANGO_SCALE);
      pango_font_description_set_weight(ld, PANGO_WEIGHT_MEDIUM);
      pango_layout_set_font_description(ll, ld);
      pango_layout_set_text(ll, label_.data(), static_cast<int>(label_.size()));

      int lw = 0, lh = 0;
      pango_layout_get_pixel_size(ll, &lw, &lh);

      const float gap = 6.0f;
      const float totalW = static_cast<float>(gw) + gap + static_cast<float>(lw);
      const float baseX = std::round((w_ - totalW) * 0.5f);
      const float gly = std::round((h_ - static_cast<float>(gh)) * 0.5f);
      const float lby = std::round((h_ - static_cast<float>(lh)) * 0.5f);

      cairo_move_to(cr, baseX, gly);
      cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
      pango_cairo_show_layout(cr, gl);

      cairo_move_to(cr, baseX + static_cast<float>(gw) + gap, lby);
      cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
      pango_cairo_show_layout(cr, ll);

      pango_font_description_free(ld);
      g_object_unref(ll);
    } else {
      const float gx = std::round((w_ - static_cast<float>(gw)) * 0.5f);
      const float gy = std::round((h_ - static_cast<float>(gh)) * 0.5f);
      cairo_move_to(cr, gx, gy);
      cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
      pango_cairo_show_layout(cr, gl);
    }

    pango_font_description_free(gd);
    g_object_unref(gl);
  } else if (!label_.empty()) {
    auto* layout = pango_cairo_create_layout(cr);
    auto* desc = pango_font_description_from_string("Inter");
    pango_font_description_set_size(desc, fontSize_ * PANGO_SCALE);
    pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, label_.data(), static_cast<int>(label_.size()));

    PangoAlignment align = PANGO_ALIGN_CENTER;
    pango_layout_set_alignment(layout, align);

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);

    const float tx = std::round((w_ - static_cast<float>(text_w)) * 0.5f);
    const float ty = std::round((h_ - static_cast<float>(text_h)) * 0.5f);

    cairo_move_to(cr, tx, ty);
    cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(desc);
    g_object_unref(layout);
  }

  cairo_restore(cr);
}

}
