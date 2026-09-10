#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/fs/shell_file_util.hpp"

#include "desktop_shell/common/glyph/bundled_assets.hpp"

#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>
#include <glib.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unistd.h>
#include <vector>

namespace eh::shell {

static void pango_fc_font_map_clear_cache_after_fc_change() {
   
  PangoFontMap* fm = pango_cairo_font_map_get_default();
  if (fm && PANGO_IS_FC_FONT_MAP(fm)) pango_fc_font_map_cache_clear(PANGO_FC_FONT_MAP(fm));
}

std::string material_symbols_ttf_path() {
   
  static constexpr const char kEhShareSuffix[] = "/event-horizon/assets/fonts/MF/MaterialSymbolsRounded.ttf";
  std::vector<std::string> candidates;
  candidates.reserve(24);
  if (const char* d = std::getenv("EH_ASSETS_DIR"); d && *d)
    candidates.push_back(std::string(d) + "/fonts/MF/MaterialSymbolsRounded.ttf");
  candidates.emplace_back("src/assets/fonts/MF/MaterialSymbolsRounded.ttf");
  {
    const std::string exe = exe_directory();
    candidates.push_back(exe + "/../src/assets/fonts/MF/MaterialSymbolsRounded.ttf");
  }
  if (const char* h = std::getenv("HOME"); h && *h)
    candidates.push_back(std::string(h) + "/.local/share" + kEhShareSuffix);
  {
    const char* xdg = std::getenv("XDG_DATA_DIRS");
    const std::string dirs = (xdg && *xdg) ? std::string(xdg) : std::string("/usr/local/share:/usr/share");
    std::size_t a = 0;
    while (a < dirs.size()) {
      std::size_t b = dirs.find(':', a);
      if (b == std::string::npos) b = dirs.size();
      std::string part = dirs.substr(a, b - a);
      while (!part.empty() && part.back() == '/') part.pop_back();
      if (!part.empty()) candidates.push_back(part + kEhShareSuffix);
      a = (b < dirs.size()) ? b + 1 : dirs.size();
    }
  }
  candidates.emplace_back(std::string("/usr/local/share") + kEhShareSuffix);
  candidates.emplace_back(std::string("/usr/share") + kEhShareSuffix);

  for (const auto& p : candidates) {
    if (!p.empty() && file_exists_str(p)) return p;
  }
  return {};
}

void register_material_symbols_font_once() {

  static bool did = false;
  if (did) return;
  did = true;
  // Prefer the on-disk font file when present: fontconfig mmaps it shared,
  // while the embedded-blob path copies 14.5MB into a private memfd in every
  // rendering process (~14.5MB RSS each). Memfd is only a fallback for
  // installs where no font file exists on disk.
  const std::string path = material_symbols_ttf_path();
  if (!path.empty()) {
    FcConfig* cfg = FcConfigGetCurrent();
    if (cfg && FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.c_str())) == FcTrue) {
      FcConfigBuildFonts(cfg);
      pango_fc_font_map_clear_cache_after_fc_change();
      return;
    }
  }
  if (bundled_try_register_material_symbols_fontconfig()) {
    pango_fc_font_map_clear_cache_after_fc_change();
  }
}

void ensure_material_symbols_font_registered() { register_material_symbols_font_once(); }

void draw_material_glyph(cairo_t* cr, double cx, double cy, double px, const char* ligature, double r, double g, double b,
                         double a) {
    
  if (!ligature || !ligature[0]) return;
  register_material_symbols_font_once();

  // Render the glyph at device-pixel resolution: when the current cairo CTM is
  // scaled (HiDPI surfaces, or UI-scale on widgets like the desktop weather
  // widget), a bitmap generated at the logical `px` size gets upsampled by
  // cairo and comes out blurry. Rendering at px*scale and drawing the surface
  // scaled back down keeps icons crisp.
  cairo_matrix_t ctm;
  cairo_get_matrix(cr, &ctm);
  double deviceScale = std::hypot(ctm.xx, ctm.xy);
  if (!(deviceScale > 0.0) || !std::isfinite(deviceScale)) deviceScale = 1.0;
  double scaledPx = px * deviceScale;

  const std::string key = std::string(ligature) + ':' + std::to_string(scaledPx);
  static std::unordered_map<std::string, cairo_surface_t*> s_cache;
  // Bound the cache: OSD-sized glyphs can be ~100KB each and the set of
  // (ligature, px, scale) pairs grows with every widget/font-size combination.
  constexpr std::size_t kGlyphCacheMaxEntries = 128;
  if (s_cache.size() >= kGlyphCacheMaxEntries) {
    for (auto& kv : s_cache) cairo_surface_destroy(kv.second);
    s_cache.clear();
  }

  cairo_surface_t* cached = nullptr;
  auto it = s_cache.find(key);
  if (it != s_cache.end()) {
    cached = it->second;
  } else {
    const int surfSize = static_cast<int>(std::ceil(scaledPx));
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surfSize, surfSize);
    cairo_t* tmp_cr = cairo_create(surf);
    cairo_set_operator(tmp_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(tmp_cr);
    cairo_set_operator(tmp_cr, CAIRO_OPERATOR_OVER);

    PangoLayout* layout = pango_cairo_create_layout(tmp_cr);
    PangoFontDescription* desc = pango_font_description_new();
    pango_font_description_set_family(desc, "Material Symbols Rounded");
    pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
    pango_font_description_set_absolute_size(desc, static_cast<int>(scaledPx * PANGO_SCALE));
    pango_layout_set_font_description(layout, desc);
    {
      PangoAttribute* fea = pango_attr_font_features_new("liga");
      if (fea) {
        fea->start_index = 0;
        fea->end_index = G_MAXUINT;
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, fea);
        pango_layout_set_attributes(layout, attrs);
        pango_attr_list_unref(attrs);
      }
    }
    pango_layout_set_text(layout, ligature, -1);
    int w = 0;
    int h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(tmp_cr, (surfSize - w) * 0.5, (surfSize - h) * 0.5);
    cairo_set_source_rgba(tmp_cr, 1.0, 1.0, 1.0, 1.0);
    pango_cairo_show_layout(tmp_cr, layout);
    pango_font_description_free(desc);
    g_object_unref(layout);
    cairo_destroy(tmp_cr);

    s_cache[key] = surf;
    cached = surf;
  }

  // The surface spans `surfSize` units in the scaled space below; dividing by
  // deviceScale maps it back to `px` user units (i.e. px*deviceScale device
  // pixels), matching the bitmap 1:1 so no upsampling happens.
  const int cw = cairo_image_surface_get_width(cached);
  const int ch = cairo_image_surface_get_height(cached);
  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_scale(cr, 1.0 / deviceScale, 1.0 / deviceScale);
  cairo_set_source_rgba(cr, r, g, b, a);
  cairo_mask_surface(cr, cached, -cw * 0.5, -ch * 0.5);
  cairo_restore(cr);
}

cairo_surface_t* create_material_glyph_surface(const char* ligature, int size) {
   
  if (size <= 0) return nullptr;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return nullptr;
  }
  cairo_t* cr = cairo_create(surf);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  draw_material_glyph(cr, size * 0.5, size * 0.5, static_cast<double>(size), ligature, 1.0, 1.0, 1.0, 1.0);
  cairo_destroy(cr);
  return surf;
}

}
