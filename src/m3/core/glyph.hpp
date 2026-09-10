#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

namespace m3 {

// Material Symbols Rounded icon glyph.
class Glyph {
public:
  Glyph() = default;

  void setGlyph(std::string_view name) { glyph_ = name; }
  void setSize(float sp) { size_ = sp; }
  void setColor(float r, float g, float b, float a = 1.0f) { cr_ = r; cg_ = g; cb_ = b; ca_ = a; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  [[nodiscard]] std::string_view glyph() const noexcept { return glyph_; }
  [[nodiscard]] float size() const noexcept { return size_; }

  // Measure icon pixel extent.
  void measureExtents(float& outW, float& outH) const {
    if (glyph_.empty()) { outW = 0; outH = 0; return; }
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
    if (glyph_.empty()) return;
    applyLayout(cr, [&](cairo_t* ctx, PangoLayout* layout) {
      cairo_set_source_rgba(ctx, cr_, cg_, cb_, ca_);
      pango_cairo_show_layout(ctx, layout);
    });
  }

  void paintAt(cairo_t* cr, float px, float py) const {
    if (glyph_.empty()) return;
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
    pango_font_description_set_family(desc, "Material Symbols Rounded");
    pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(desc, static_cast<int>(size_ * PANGO_SCALE));
    pango_layout_set_font_description(layout, desc);

    PangoAttribute* fea = pango_attr_font_features_new("liga");
    if (fea) {
      fea->start_index = 0;
      fea->end_index = G_MAXUINT;
      PangoAttrList* attrs = pango_attr_list_new();
      pango_attr_list_insert(attrs, fea);
      pango_layout_set_attributes(layout, attrs);
      pango_attr_list_unref(attrs);
    }

    pango_layout_set_text(layout, glyph_.data(), static_cast<int>(glyph_.size()));
    fn(cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
  }

  std::string glyph_;
  float size_ = 24.0f;
  float cr_ = 0.94f, cg_ = 0.94f, cb_ = 0.96f, ca_ = 1.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace m3
