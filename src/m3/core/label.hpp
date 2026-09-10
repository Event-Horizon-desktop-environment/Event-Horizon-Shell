#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "m3/tokens/type_scale.hpp"

namespace m3 {

// Text label using Pango layout. Supports M3 FontConfig or direct font props.
class Label {
public:
  Label() = default;

  void setText(std::string_view t) { text_ = t; }
  void setFontConfig(const FontConfig& fc) { fc_ = fc; }
  void setFontSize(float sp) { fc_.size = sp; }
  void setFontWeight(int w) { fc_.weight = w; }
  void setFontFamily(std::string_view f) { fc_.family = f; }
  void setColor(float r, float g, float b, float a = 1.0f) { cr_ = r; cg_ = g; cb_ = b; ca_ = a; }

  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] std::string_view text() const noexcept { return text_; }

  // Measure text extent without painting.
  void measureExtents(float& outW, float& outH) const {
    if (text_.empty()) { outW = 0; outH = 0; return; }
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto* cr = cairo_create(surface);
    applyLayout(cr, [&](cairo_t*, PangoLayout* layout) {
      int pw, ph;
      pango_layout_get_pixel_size(layout, &pw, &ph);
      outW = static_cast<float>(pw);
      outH = static_cast<float>(ph);
    });
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
  }

  void paint(cairo_t* cr) const {
    if (text_.empty() || w_ <= 0 || h_ <= 0) return;

    applyLayout(cr, [&](cairo_t* ctx, PangoLayout* layout) {
      cairo_set_source_rgba(ctx, cr_, cg_, cb_, ca_);
      pango_cairo_show_layout(ctx, layout);
    });
  }

  // Paint at arbitrary position (ignores x_, y_).
  void paintAt(cairo_t* cr, float px, float py) const {
    if (text_.empty()) return;
    cairo_save(cr);
    cairo_translate(cr, px, py);
    applyLayout(cr, [&](cairo_t* ctx, PangoLayout* layout) {
      cairo_set_source_rgba(ctx, cr_, cg_, cb_, ca_);
      pango_cairo_show_layout(ctx, layout);
    });
    cairo_restore(cr);
  }

private:
  template <typename Fn>
  void applyLayout(cairo_t* cr, Fn&& fn) const {
    auto* layout = pango_cairo_create_layout(cr);
    auto* desc = pango_font_description_new();
    pango_font_description_set_family(desc, fc_.family.empty() ? "Inter" : fc_.family.c_str());
    pango_font_description_set_size(desc, static_cast<int>(fc_.size * PANGO_SCALE));
    pango_font_description_set_weight(desc, static_cast<PangoWeight>(fc_.weight));
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, text_.data(), static_cast<int>(text_.size()));
    if (w_ > 0) pango_layout_set_width(layout, static_cast<int>(w_ * PANGO_SCALE));
    fn(cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
  }

  std::string text_;
  FontConfig fc_{14.0f, 400, 20.0f, 0.25f, "plain"};
  float cr_ = 0.94f, cg_ = 0.94f, cb_ = 0.96f, ca_ = 1.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace m3
