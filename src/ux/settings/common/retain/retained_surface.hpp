#pragma once

// Shared retained-surface primitives for settings paint caching.
//
// Small glassy boxes, buttons and rows are pure functions of
// (geometry, theme colors, alpha, hover/enabled state, device scale).
// Rendering each once per distinct state into an image surface and blitting
// cuts ~1ms+ per frame with pixel-identical output: keys include every paint
// input, so a hit matches by construction.
//
// Each user TU owns its cache struct + fixed-slot/map wrappers (see
// settings_tab_monitors for the reference); this header holds only the
// stateless pieces.

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace eh::settings::retain {

struct SurfEntry {
  cairo_surface_t* surf = nullptr;
  int sw = 0, sh = 0;  // device px
  uint64_t key = 0;
};

inline uint64_t q8(float v) {
  const int q = static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  return static_cast<uint64_t>(static_cast<unsigned>(q));
}

inline uint64_t mix(uint64_t h, uint64_t v) {
  h ^= v;
  h *= 1099511628211ULL;
  return h;
}

inline double device_scale(cairo_t* cr) {
  cairo_matrix_t m;
  cairo_get_matrix(cr, &m);
  double ds = std::hypot(m.xx, m.xy);
  if (!(ds > 0.0) || !std::isfinite(ds)) ds = 1.0;
  return ds;
}

// Blit a device-resolution retained surface 1:1 onto cr at user (x, y).
inline void blit(cairo_t* cr, const SurfEntry& e, double x, double y, double ds) {
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_scale(cr, 1.0 / ds, 1.0 / ds);
  cairo_set_source_surface(cr, e.surf, 0, 0);
  cairo_rectangle(cr, 0, 0, e.sw, e.sh);
  cairo_fill(cr);
  cairo_restore(cr);
}

template <typename PaintFn>
inline void render(SurfEntry& e, int sw, int sh, double ds, uint64_t key, PaintFn&& paint) {
  if (e.surf) cairo_surface_destroy(e.surf);
  e.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
  e.sw = sw;
  e.sh = sh;
  e.key = key;
  cairo_t* tmp = cairo_create(e.surf);
  cairo_set_operator(tmp, CAIRO_OPERATOR_CLEAR);
  cairo_paint(tmp);
  cairo_set_operator(tmp, CAIRO_OPERATOR_OVER);
  cairo_scale(tmp, ds, ds);
  paint(tmp);
  cairo_destroy(tmp);
}

inline void destroy(SurfEntry& e, unsigned long long* bytes = nullptr) {
  if (e.surf) {
    if (bytes) {
      const unsigned long long n =
          static_cast<unsigned long long>(e.sw) * static_cast<unsigned long long>(e.sh) * 4ULL;
      *bytes -= std::min(*bytes, n);
    }
    cairo_surface_destroy(e.surf);
    e.surf = nullptr;
    e.sw = e.sh = 0;
    e.key = 0;
  }
}

}  // namespace eh::settings::retain
