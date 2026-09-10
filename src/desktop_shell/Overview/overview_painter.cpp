#include "desktop_shell/Overview/overview_painter.hpp"
#include "desktop_shell/Overview/overview_profiler.hpp"

#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"

#include "m3/core/primitives/box.hpp"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eh::shell::overview {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Pre-rendered, fitted image cache. Live window frames and workspace captures
// are downscaled into these offscreen surfaces once per source change; every
// paint pass then blits them 1:1 instead of resampling the source buffer.
struct CachedImage {
  int w = 0, h = 0;
  cairo_surface_t* surf = nullptr;
  const void* src = nullptr; // source buffer identity (live frame data())
  int srcW = 0, srcH = 0;
};

// Identity of a rendered card visual (backdrop + windows + glass).
struct CardCacheKey {
  const void* cap = nullptr; // WorkspaceCapture* or null for the procedural backdrop
  int wsId = 0;
  int w = 0, h = 0;
  std::uint64_t color = 0;
  std::uint64_t contentHash = 0; // hash of window content (live frames, layout)
  bool operator==(const CardCacheKey& o) const {
    return cap == o.cap && (cap != nullptr || wsId == o.wsId) && w == o.w && h == o.h &&
           color == o.color && contentHash == o.contentHash;
  }
};

struct CardCacheKeyHash {
  size_t operator()(const CardCacheKey& k) const {
    std::uint64_t h = 14695981039346656037ull;
    auto mix = [&](std::uint64_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(k.cap)));
    if (!k.cap) mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.wsId)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.w)) |
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.h)) << 32));
    mix(k.color);
    mix(k.contentHash);
    return static_cast<size_t>(h);
  }
};

// Cairo's C API requires a mutable pointer for cairo_image_surface_create_for_data,
// even when the surface will only be used as a read-only source.  The underlying
// data is never modified through the returned surface.
inline unsigned char* cairo_readonly_data(const uint8_t* p) {
  return const_cast<unsigned char*>(p);
}

struct StripCache {
  cairo_surface_t* surf = nullptr;
  int w = 0, h = 0;
  std::uint64_t hash = 0;
};

// All mutable paint-side caches consolidated into one struct so state is
// explicitly scoped and resettable instead of scattered across file-scope
// globals.
struct PaintCacheState {
  std::unordered_map<std::string, CachedImage> winCache;
  std::unordered_map<CardCacheKey, CachedImage, CardCacheKeyHash> cardCache;
  StripCache strip{};

  int cardHits = 0, cardMisses = 0;
  int winHits = 0, winMisses = 0;
  int stripHits = 0, stripMisses = 0;
  std::uint64_t cardCacheGen = 0;

  void reset_stats() {
    cardHits = cardMisses = 0;
    winHits = winMisses = 0;
    stripHits = stripMisses = 0;
  }
};

PaintCacheState g_paint;

constexpr int kCardDiskCacheVersion = 1;

std::filesystem::path card_disk_cache_dir() {
  auto home = std::getenv("HOME");
  if (!home || !home[0]) return {};
  return std::filesystem::path(home) / ".cache" / "event-horizon" / "overview-cards";
}

std::string card_disk_cache_filename(int wsId, int w, int h, std::uint64_t colorKey,
                                     std::uint64_t contentHash) {
  return std::to_string(wsId) + "_" + std::to_string(w) + "x" + std::to_string(h) + "_" +
         std::to_string(colorKey) + "_" + std::to_string(contentHash) + "_v" +
         std::to_string(kCardDiskCacheVersion) + ".png";
}

std::filesystem::path card_disk_cache_path(int wsId, int w, int h, std::uint64_t colorKey,
                                           std::uint64_t contentHash) {
  return card_disk_cache_dir() /
         card_disk_cache_filename(wsId, w, h, colorKey, contentHash);
}

// Try to load a previously rendered card from disk. Returns null on miss.
cairo_surface_t* card_disk_cache_load(int wsId, int w, int h, std::uint64_t colorKey,
                                      std::uint64_t contentHash) {
  auto p = card_disk_cache_path(wsId, w, h, colorKey, contentHash);
  if (p.empty() || !std::filesystem::exists(p)) return nullptr;
  cairo_surface_t* surf = cairo_image_surface_create_from_png(p.string().c_str());
  if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS ||
      cairo_image_surface_get_width(surf) != w ||
      cairo_image_surface_get_height(surf) != h) {
    if (surf) cairo_surface_destroy(surf);
    return nullptr;
  }
  return surf;
}

// Save a rendered card surface to disk (fire-and-forget).
void card_disk_cache_save(cairo_surface_t* surf, int wsId, int w, int h, std::uint64_t colorKey,
                          std::uint64_t contentHash) {
  if (!surf || w <= 0 || h <= 0) return;
  auto dir = card_disk_cache_dir();
  if (dir.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return;
  auto p = card_disk_cache_path(wsId, w, h, colorKey, contentHash);
  cairo_status_t st = cairo_surface_write_to_png(surf, p.string().c_str());
  if (st != CAIRO_STATUS_SUCCESS) {
    std::error_code rm;
    std::filesystem::remove(p, rm);
  }
}

} // anonymous namespace

std::uint64_t compute_ws_content_hash(const OverviewWorkspace& ws) {
  std::uint64_t h = 14695981039346656037ull;
  auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
  mix(static_cast<std::uint64_t>(ws.windows.size()));
  for (const auto& win : ws.windows) {
    if (win.special) continue;
    for (char c : win.addr) mix(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    for (char c : win.appId) mix(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
  }
  return h;
}

void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_path(cr);
  cairo_arc(cr, x + w - rad, y + rad, rad, -kPi * 0.5, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, kPi * 0.5);
  cairo_arc(cr, x + rad, y + h - rad, rad, kPi * 0.5, kPi);
  cairo_arc(cr, x + rad, y + rad, rad, kPi, kPi * 1.5);
  cairo_close_path(cr);
}

void hsl_to_rgb(double h, double s, double l, double& r, double& g, double& b) {
  if (s <= 0.0) {
    r = g = b = l;
    return;
  }
  const double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
  const double p = 2.0 * l - q;
  auto hue2rgb = [&](double t) -> double {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
  };
  r = hue2rgb(h + 1.0 / 3.0);
  g = hue2rgb(h);
  b = hue2rgb(h - 1.0 / 3.0);
}

// Stable per-appId tint (FNV-1a hash -> hue). Used for placeholder window tiles.
void app_tint(const std::string& appId, double& r, double& g, double& b) {
  std::uint32_t h = 2166136261u;
  for (unsigned char c : appId) {
    h ^= c;
    h *= 16777619u;
  }
  hsl_to_rgb(static_cast<double>(h % 360) / 360.0, 0.42, 0.58, r, g, b);
}

// Rect of a single window tile inside a workspace card. Maps logical window
// geometry into the card; falls back to a 2/3-column grid when geometry is
// unknown (backends without geometry data).
OverviewCardRect window_tile_rect(const OverviewLayout& layout, const OverviewCardRect& card,
                                  const OverviewWindow& win, int winIndex, int nWindowsInWs) {
  const double us = layout.uiScale;
  const double pad = 12.0 * us;
  OverviewCardRect r;
  if (win.ww > 0.0 && win.wh > 0.0 && layout.w > 0.0 && layout.h > 0.0) {
    // Normalized geometry (relative to the workspace's monitor): always maps
    // cleanly into the card regardless of which monitor the workspace is on.
    const double innerW = std::max(1.0, card.w - pad * 2.0);
    const double innerH = std::max(1.0, card.h - pad * 2.0);
    r.w = std::clamp(win.ww * innerW, 18.0 * us, innerW);
    r.h = std::clamp(win.wh * innerH, 14.0 * us, innerH);
    r.x = card.x + pad + win.wx * innerW;
    r.y = card.y + pad + win.wy * innerH;
    r.x = std::clamp(r.x, card.x + pad, card.x + card.w - pad - r.w);
    r.y = std::clamp(r.y, card.y + pad, card.y + card.h - pad - r.h);
  } else if (win.w > 0.0 && win.h > 0.0 && layout.w > 0.0 && layout.h > 0.0) {
    const double innerW = std::max(1.0, card.w - pad * 2.0);
    const double innerH = std::max(1.0, card.h - pad * 2.0);
    r.w = std::clamp((win.w / layout.w) * innerW, 18.0 * us, innerW);
    r.h = std::clamp((win.h / layout.h) * innerH, 14.0 * us, innerH);
    r.x = card.x + pad + (win.x / layout.w) * innerW;
    r.y = card.y + pad + (win.y / layout.h) * innerH;
    r.x = std::clamp(r.x, card.x + pad, card.x + card.w - pad - r.w);
    r.y = std::clamp(r.y, card.y + pad, card.y + card.h - pad - r.h);
  } else {
    const int n = std::max(1, nWindowsInWs);
    const int cols = std::min(3, std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))))));
    const int rows = (n + cols - 1) / cols;
    const double gap = 10.0 * us;
    const double innerW = std::max(1.0, card.w - pad * 2.0);
    const double innerH = std::max(1.0, card.h - pad * 2.0);
    const double cellW = (innerW - static_cast<double>(cols - 1) * gap) / cols;
    const double cellH = (innerH - static_cast<double>(rows - 1) * gap) / rows;
    const int col = winIndex % cols;
    const int row = winIndex / cols;
    r.w = std::clamp(cellW * 0.9, 40.0 * us, cellW);
    r.h = std::clamp(cellH * 0.9, 24.0 * us, cellH);
    const double cellX = card.x + pad + static_cast<double>(col) * (cellW + gap);
    const double cellY = card.y + pad + static_cast<double>(row) * (cellH + gap);
    r.x = cellX + (cellW - r.w) * 0.5;
    r.y = cellY + (cellH - r.h) * 0.5;
  }
  return r;
}

// Number of painted (non-special) windows in workspaces before `wsIndex`.
int flat_base(const std::vector<OverviewWorkspace>& workspaces, int wsIndex) {
  int f = 0;
  for (int k = 0; k < wsIndex; ++k) {
    for (const auto& w : workspaces[static_cast<size_t>(k)].windows) {
      if (!w.special) ++f;
    }
  }
  return f;
}

// Live per-window frame drawn fitted into the tile preserving the window's
// true aspect ratio (the tile rect itself is normalized to the card's aspect,
// so cover-fitting would crop 16:9 content on typical monitors). `bgra` is
// little-endian CAIRO_FORMAT_ARGB32 order, top-down. The fitted, downscaled
// frame is cached per window so the resample runs once per streamed frame
// instead of on every paint pass.
void paint_live_frame(cairo_t* cr, const OverviewCardRect& tile, const OverviewWindow& win,
                      float alpha) {
  const int w = win.liveW;
  const int h = win.liveH;
  if (w <= 0 || h <= 0 || win.liveBgra.size() < static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)
    return;

  const double scale = std::min(tile.w / static_cast<double>(w),
                                tile.h / static_cast<double>(h));
  const double dw = static_cast<double>(w) * scale;
  const double dh = static_cast<double>(h) * scale;
  const double dx = tile.x + (tile.w - dw) * 0.5;
  const double dy = tile.y + (tile.h - dh) * 0.5;
  const int rw = std::max(1, static_cast<int>(std::round(dw)));
  const int rh = std::max(1, static_cast<int>(std::round(dh)));

  if (!win.addr.empty()) {
    CachedImage& e = g_paint.winCache[win.addr];
    if (e.surf && e.src == win.liveBgra.data() && e.srcW == w && e.srcH == h) {
      ++g_paint.winHits;
      cairo_save(cr);
      cairo_translate(cr, dx, dy);
      cairo_scale(cr, static_cast<double>(rw) / w, static_cast<double>(rh) / h);
      cairo_set_source_surface(cr, e.surf, 0, 0);
      cairo_paint_with_alpha(cr, alpha);
      cairo_restore(cr);
      return;
    }

    ++g_paint.winMisses;
    cairo_surface_t* ns = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(ns) == CAIRO_STATUS_SUCCESS) {
      cairo_t* rc = cairo_create(ns);
      cairo_surface_t* img = cairo_image_surface_create_for_data(
          cairo_readonly_data(win.liveBgra.data()), CAIRO_FORMAT_ARGB32, w, h, w * 4);
      if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
        cairo_set_source_surface(rc, img, 0, 0);
        cairo_set_operator(rc, CAIRO_OPERATOR_SOURCE);
        cairo_paint(rc);
      }
      cairo_surface_destroy(img);
      cairo_destroy(rc);
    }
    if (e.surf) cairo_surface_destroy(e.surf);
    if (cairo_surface_status(ns) != CAIRO_STATUS_SUCCESS) {
      cairo_surface_destroy(ns);
      ns = nullptr;
    }
    e.surf = ns;
    e.w = w;
    e.h = h;
    e.src = win.liveBgra.data();
    e.srcW = w;
    e.srcH = h;

    if (e.surf) {
      cairo_save(cr);
      cairo_translate(cr, dx, dy);
      cairo_scale(cr, static_cast<double>(rw) / w, static_cast<double>(rh) / h);
      cairo_set_source_surface(cr, e.surf, 0, 0);
      cairo_paint_with_alpha(cr, alpha);
      cairo_restore(cr);
    }
    return;
  }

  // No stable key (backend provides none): paint directly.
  cairo_surface_t* img = cairo_image_surface_create_for_data(
      cairo_readonly_data(win.liveBgra.data()), CAIRO_FORMAT_ARGB32, w, h, w * 4);
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(img);
    return;
  }
  cairo_save(cr);
  cairo_translate(cr, dx, dy);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, img, 0, 0);
  cairo_paint_with_alpha(cr, alpha);
  cairo_restore(cr);
  cairo_surface_destroy(img);
}

void paint_window_tile(cairo_t* cr, DockApp& dock, const OverviewColors& colors,
                       const OverviewLayout& layout, const OverviewCardRect& tile,
                       const OverviewWindow& win, float alpha) {
  const double us = layout.uiScale;
  const double r = 12.0 * us;

  double tr = 0.0, tg = 0.0, tb = 0.0;
  if (!win.appId.empty())
    app_tint(win.appId, tr, tg, tb);
  else
    hsl_to_rgb(0.0, 0.0, 0.55, tr, tg, tb);

  const bool liveImg = win.liveValid && win.liveW > 0 && win.liveH > 0 &&
                       win.liveBgra.size() >=
                           static_cast<size_t>(win.liveW) * static_cast<size_t>(win.liveH) * 4u;

  if (liveImg) {
    // Real streamed frame as the tile content, fitted without cropping. No
    // tinted backing fill, glass overlay, or drop shadow: the frame floats
    // directly on the card so the desktop capture shows through around it.
    cairo_save(cr);
    rounded_rect(cr, tile.x, tile.y, tile.w, tile.h, r);
    cairo_clip(cr);
    paint_live_frame(cr, tile, win, alpha);
    cairo_restore(cr);
  } else {
    // Drop shadow
    cairo_set_source_rgba(cr, 0, 0, 0, 0.28 * alpha);
    rounded_rect(cr, tile.x + 2.0 * us, tile.y + 3.0 * us, tile.w, tile.h, r);
    cairo_fill(cr);

    // Tile background — app tint blended over the panel fill color
    {
      const double kBg = 0.45;
      cairo_set_source_rgba(cr, tr * kBg + colors.wsBgR * (1.0 - kBg),
                            tg * kBg + colors.wsBgG * (1.0 - kBg),
                            tb * kBg + colors.wsBgB * (1.0 - kBg), 0.95 * alpha);
      rounded_rect(cr, tile.x, tile.y, tile.w, tile.h, r);
      cairo_fill(cr);
    }
    eh::widgets::slot_pill_style::paint_glass_layers(cr, tile.x, tile.y, tile.w, tile.h,
                                                     alpha * 0.9, r);
  }

  // App icon top-left
  const double iconSz = std::clamp(std::min(tile.w * 0.30, tile.h * 0.34), 16.0 * us, 30.0 * us);
  const double iconX = tile.x + 6.0 * us;
  const double iconY = tile.y + 6.0 * us;
  const eh::icons::IconEntry* ic = nullptr;
  if (!win.appId.empty()) ic = dock.icons.app_icon(win.appId);
  if (ic && ic->surface) {
    cairo_save(cr);
    cairo_translate(cr, iconX, iconY);
    const double iw = static_cast<double>(ic->width);
    const double ih = static_cast<double>(ic->height);
    const double sc = iconSz / std::max(1.0, std::max(iw, ih));
    cairo_scale(cr, sc, sc);
    cairo_set_source_surface(cr, ic->surface, 0, 0);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
  }

  // Title pill along the bottom edge
  const std::string& title = win.title;
  if (!title.empty()) {
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0 * us);
    cairo_text_extents_t ex = {};
    cairo_text_extents(cr, title.c_str(), &ex);
    const double maxW = std::max(1.0, tile.w - 10.0 * us);
    const double textW = std::min(ex.x_advance, maxW);
    const double padH = 8.0 * us;
    const double padV = 3.0 * us;
    const double pillW = textW + padH * 2.0;
    const double pillH = 14.0 * us + padV * 2.0;
    const double pillX = tile.x + (tile.w - pillW) * 0.5;
    const double pillY = tile.y + tile.h - pillH - 5.0 * us;

    cairo_set_source_rgba(cr, 0, 0, 0, 0.72 * alpha);
    rounded_rect(cr, pillX, pillY, pillW, pillH, pillH * 0.5);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1, 1, 1, alpha);
    cairo_save(cr);
    cairo_rectangle(cr, pillX, pillY, pillW, pillH);
    cairo_clip(cr);
    cairo_move_to(cr, pillX + padH, pillY + pillH - padV - 2.0 * us);
    cairo_show_text(cr, title.c_str());
    cairo_restore(cr);
  }

  // Focused accent border
  if (win.focused) {
    cairo_set_line_width(cr, 2.0 * us);
    cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB, 0.85 * alpha);
    rounded_rect(cr, tile.x, tile.y, tile.w, tile.h, r);
    cairo_stroke(cr);
  }
}

void paint_close_button(cairo_t* cr, const OverviewLayout& layout, const OverviewCardRect& tile,
                        bool closeHovered, float alpha) {
  const double us = layout.uiScale;
  const double bs = layout.closeBtnSz * 0.72;
  const double bx = tile.x + tile.w - bs * 0.5;
  const double by = tile.y - bs * 0.5;
  cairo_set_source_rgba(cr, 0, 0, 0, closeHovered ? 0.55 * alpha : 0.35 * alpha);
  cairo_arc(cr, bx + bs * 0.5, by + bs * 0.5, bs * 0.5, 0, kPi * 2);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, alpha);
  cairo_set_line_width(cr, 1.8 * us);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double pad = bs * 0.30;
  cairo_move_to(cr, bx + pad, by + pad);
  cairo_line_to(cr, bx + bs - pad, by + bs - pad);
  cairo_move_to(cr, bx + bs - pad, by + pad);
  cairo_line_to(cr, bx + pad, by + bs - pad);
  cairo_stroke(cr);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
}

void compute_overview_layout(OverviewLayout& layout, double w, double h, int nWorkspaces,
                             double uiScale, OverviewAxis axis, double cardScale, double cardGap,
                             double closeBtnSz, double searchW) {
  layout.w = w;
  layout.h = h;
  layout.uiScale = uiScale;
  layout.axis = axis;
  layout.scale = std::clamp(cardScale, 0.2, 0.8);
  layout.cardGap = std::max(8.0, cardGap * uiScale);
  (void)nWorkspaces;

  layout.searchY = 20.0 * uiScale;
  layout.searchW = std::clamp(searchW, 100.0, 1200.0) * uiScale;
  layout.searchH = 40.0 * uiScale;
  layout.closeBtnSz = std::clamp(closeBtnSz, 12.0, 90.0) * uiScale;

  if (axis == OverviewAxis::Vertical) {
    layout.cardH = h * layout.scale;
    layout.cardW = std::min(w * 0.92, layout.cardH * 1.6);
    layout.pitch = layout.cardH + layout.cardGap;
  } else {
    layout.cardW = w * layout.scale;
    layout.cardH = std::min(h * 0.92, layout.cardW * 0.625);
    layout.pitch = layout.cardW + layout.cardGap;
  }

  // Quick select strip: ~25% card size, positioned at bottom (horizontal) or
  // left (vertical). The main strip center is shifted to make room.
  compute_quick_select_layout(layout.qs, axis, w, h, layout.cardW, layout.cardH, uiScale,
                              nWorkspaces);

  if (axis == OverviewAxis::Vertical) {
    // Strip on the left: shift main center rightward
    layout.stripCenterX = (w + layout.qs.stripW) * 0.5;
    layout.stripCenterY = h * 0.5;
  } else {
    // Strip at the bottom: shift main center upward
    layout.stripCenterX = w * 0.5;
    layout.stripCenterY = (h - layout.qs.stripH) * 0.5;
  }
}

OverviewCardRect overview_card_rect(const OverviewLayout& layout, int index, double scrollPos) {
  OverviewCardRect r;
  const double pitch = layout.axis == OverviewAxis::Vertical
                           ? layout.cardH + layout.cardGap
                           : layout.cardW + layout.cardGap;
  if (layout.axis == OverviewAxis::Vertical) {
    r.w = layout.cardW;
    r.h = layout.cardH;
    r.x = layout.stripCenterX - r.w * 0.5;
    r.y = layout.stripCenterY - r.h * 0.5 + static_cast<double>(index) * pitch - scrollPos;
  } else {
    r.w = layout.cardW;
    r.h = layout.cardH;
    r.x = layout.stripCenterX - r.w * 0.5 + static_cast<double>(index) * pitch - scrollPos;
    r.y = layout.stripCenterY - r.h * 0.5;
  }
  return r;
}

double overview_selected_index(const OverviewLayout& layout, double scrollPos, int nWorkspaces) {
  if (layout.pitch <= 0.0) return 0.0;
  double sel = std::round(scrollPos / layout.pitch);
  if (nWorkspaces > 0) sel = std::clamp(sel, 0.0, static_cast<double>(nWorkspaces - 1));
  return sel;
}

void paint_search_bar(cairo_t* cr, const OverviewColors& colors, const OverviewLayout& layout,
                      float hoverLift, float progress, const std::string& query) {
  const float alpha = std::clamp(progress, 0.0f, 1.0f);
  if (alpha <= 0.0f) return;

  const double liftPx = static_cast<double>(hoverLift) * 5.0 * layout.uiScale;
  const double cx = layout.w * 0.5;
  const double x = cx - layout.searchW * 0.5;
  const double y = layout.searchY - liftPx;

  if (hoverLift > 0.01f) {
    const double shAlpha = static_cast<double>(hoverLift) * 0.2;
    cairo_set_source_rgba(cr, 0, 0, 0, shAlpha * alpha);
    rounded_rect(cr, x + 3 * layout.uiScale, y + 4 * layout.uiScale,
                 layout.searchW, layout.searchH, layout.searchH * 0.5);
    cairo_fill(cr);
  }

  rounded_rect(cr, x, y, layout.searchW, layout.searchH, layout.searchH * 0.5);
  cairo_set_source_rgba(cr, colors.searchBgR, colors.searchBgG, colors.searchBgB, alpha);
  cairo_fill(cr);
  eh::widgets::slot_pill_style::paint_glass_layers(cr, x, y, layout.searchW, layout.searchH, alpha,
                                                   layout.searchH * 0.5);

  if (hoverLift > 0.01f) {
    cairo_set_source_rgba(cr, 1, 1, 1, static_cast<double>(hoverLift) * 0.08 * alpha);
    rounded_rect(cr, x, y, layout.searchW, layout.searchH, layout.searchH * 0.5);
    cairo_fill(cr);
  }

  draw_material_glyph(cr, x + 20 * layout.uiScale, y + layout.searchH * 0.5,
                      18.0 * layout.uiScale, "search",
                      colors.dimFgR, colors.dimFgG, colors.dimFgB, alpha);

  if (query.empty()) {
    cairo_set_font_size(cr, 13.0 * layout.uiScale);
    cairo_set_source_rgba(cr, colors.dimFgR, colors.dimFgG, colors.dimFgB, alpha);
    cairo_move_to(cr, x + 42 * layout.uiScale, y + layout.searchH * 0.66);
    cairo_show_text(cr, "Type to search...");
  } else {
    cairo_set_font_size(cr, 13.0 * layout.uiScale);
    cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, alpha);
    cairo_move_to(cr, x + 42 * layout.uiScale, y + layout.searchH * 0.66);
    cairo_show_text(cr, query.c_str());
  }
}

// Real workspace capture drawn fitted into the card without cropping.
// `bgra` is stored in little-endian CAIRO_FORMAT_ARGB32 order. With `blur` the
// image is painted into a small buffer and upscaled with bilinear filtering
// (a cheap multi-pixel-average Gaussian), used for the desktop backdrop behind
// the per-window tiles of a scrolling workspace so the apps pop off the image.
void paint_capture_background(cairo_t* cr, const OverviewCardRect& card,
                              const WorkspaceCapture& cap, float alpha, bool blur = false) {
  // Prefer the pre-scaled thumbnail; fall back to the full-res buffer when the
  // capture was built without one.
  const bool useThumb = (cap.thumbW > 0 && cap.thumbH > 0 &&
                         cap.thumbBgra.size() >= static_cast<size_t>(cap.thumbW) *
                                                     static_cast<size_t>(cap.thumbH) * 4u);
  const int w = useThumb ? cap.thumbW : cap.width;
  const int h = useThumb ? cap.thumbH : cap.height;
  const uint8_t* data = useThumb ? cap.thumbBgra.data() : cap.bgra.data();
  if (w <= 0 || h <= 0 || !data ||
      (useThumb ? cap.thumbBgra : cap.bgra).size() <
          static_cast<size_t>(w) * static_cast<size_t>(h) * 4u)
    return;

  const double scale = std::min(card.w / static_cast<double>(w),
                                card.h / static_cast<double>(h));
  const double dw = static_cast<double>(w) * scale;
  const double dh = static_cast<double>(h) * scale;
  const double dx = card.x + (card.w - dw) * 0.5;
  const double dy = card.y + (card.h - dh) * 0.5;

  cairo_surface_t* img = cairo_image_surface_create_for_data(
      cairo_readonly_data(data), CAIRO_FORMAT_ARGB32, w, h,
      w * 4);
  if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(img);
    return;
  }

  if (!blur) {
    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, img, 0, 0);
    if (alpha < 1.0f) cairo_paint_with_alpha(cr, alpha);
    else cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(img);
    return;
  }

  // Downscale into a ~1/8 buffer, then upscale to the card rect with bilinear
  // filtering: each output pixel averages a large source neighborhood, giving
  // the frosted backdrop without an explicit convolution.
  const int sw = std::max(1, static_cast<int>(std::round(dw)) / 8);
  const int sh = std::max(1, static_cast<int>(std::round(dh)) / 8);
  cairo_surface_t* small = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
  if (cairo_surface_status(small) == CAIRO_STATUS_SUCCESS) {
    cairo_t* sc = cairo_create(small);
    cairo_set_operator(sc, CAIRO_OPERATOR_SOURCE);
    cairo_scale(sc, static_cast<double>(sw) / w, static_cast<double>(sh) / h);
    cairo_set_source_surface(sc, img, 0, 0);
    cairo_paint(sc);
    cairo_destroy(sc);

    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, dw / static_cast<double>(sw), dh / static_cast<double>(sh));
    cairo_pattern_t* pat = cairo_pattern_create_for_surface(small);
    cairo_pattern_set_filter(pat, CAIRO_FILTER_BILINEAR);
    cairo_set_source(cr, pat);
    if (alpha < 1.0f) cairo_paint_with_alpha(cr, alpha);
    else cairo_paint(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
  }
  cairo_surface_destroy(small);
  cairo_surface_destroy(img);
}

// Paint the full card visual into `cr`: a frosted glass backdrop (the Settings
// glass-card recipe) plus the fitted desktop capture (capture mode). The result
// is cached per card size in paint_workspace_cards so the glass fill and
// screenshot resample happen once instead of on every paint pass. The glass
// rim/highlight bands are stroked live in paint_workspace_cards so they stay
// consistent with the per-frame opacity.
void paint_card_content(cairo_t* cr, const OverviewColors& colors,
                        const OverviewCardRect& card, const WorkspaceCapture* cap, double us,
                        float alpha, bool blurBackdrop = false) {
  if (cap) {
    // Frosted glass card base — the same recipe as the Settings glass cards
    // (panelFill * 0.35 at 0.78 alpha).
    {
      m3::Box box;
      box.setColor(static_cast<float>(colors.glassBgR), static_cast<float>(colors.glassBgG),
                   static_cast<float>(colors.glassBgB), static_cast<float>(0.78 * alpha));
      box.setRadius(static_cast<float>(24.0 * us));
      box.setGeometry(static_cast<float>(card.x), static_cast<float>(card.y),
                      static_cast<float>(card.w), static_cast<float>(card.h));
      box.paint(cr);
    }
    paint_capture_background(cr, card, *cap, alpha, blurBackdrop);
    return;
  }

  // No capture (Snapshot mode, or a workspace whose capture was dropped as
  // stale). Leave the card transparent so the dimmed desktop backdrop shows
  // through behind the window tiles; the glassy rim/highlight bands are
  // stroked live over the whole card in paint_workspace_cards.
}

void paint_workspace_cards(cairo_t* cr, DockApp& dock, const OverviewColors& colors,
                           const OverviewLayout& layout,
                           const std::vector<OverviewWorkspace>& workspaces,
                           double scrollPos, int selectedIndex,
                           int hoveredWs, int hoveredWinFlat, bool closeHovered,
                           const std::vector<float>& wsHoverLifts,
                           const std::vector<float>& winHoverLifts,
                           int draggedWs, int draggedWinFlat,
                           double ghostX, double ghostY,
                            int dropTargetWs,
                            float progress,
                            bool invalidateCards) {

  g_paint.cardHits = 0; g_paint.cardMisses = 0;
  g_paint.winHits = 0; g_paint.winMisses = 0;
  g_paint.stripHits = 0; g_paint.stripMisses = 0;

  if (invalidateCards) {
    for (auto& [k, v] : g_paint.cardCache) {
      if (v.surf) cairo_surface_destroy(v.surf);
    }
    g_paint.cardCache.clear();
  }
  const float alpha = std::clamp(progress, 0.0f, 1.0f);
  if (alpha <= 0.0f || workspaces.empty()) return;
  const double us = layout.uiScale;

  // Zoom-out open effect: cards start at full-monitor size and shrink to the
  // final `scale` as progress goes 0 -> 1.
  const double zoom = layout.scale > 0.0
                          ? (1.0 / layout.scale + (1.0 - 1.0 / layout.scale) * progress)
                          : 1.0;

  const double margin = 40.0 * us;
  const bool vertical = (layout.axis == OverviewAxis::Vertical);

  // Prune the render caches to the windows/captures still present so closed or
  // moved items free their cached surfaces immediately.
  {
    std::unordered_set<std::string> addrs;
    for (const auto& ws : workspaces)
      for (const auto& w : ws.windows)
        if (!w.addr.empty()) addrs.insert(w.addr);
    for (auto it = g_paint.winCache.begin(); it != g_paint.winCache.end();) {
      if (addrs.count(it->first)) {
        ++it;
        continue;
      }
      cairo_surface_destroy(it->second.surf);
      it = g_paint.winCache.erase(it);
    }

    // Build O(1) lookup sets for card cache pruning instead of nested loops.
    std::unordered_set<const void*> validCaps;
    std::unordered_set<int> validWsIds;
    for (const auto& ws : workspaces) {
      if (ws.capture) validCaps.insert(ws.capture.get());
      validWsIds.insert(ws.id);
    }
    for (auto it = g_paint.cardCache.begin(); it != g_paint.cardCache.end();) {
      bool keep = false;
      if (it->first.cap != nullptr) {
        keep = validCaps.count(it->first.cap) > 0;
      } else {
        keep = validWsIds.count(it->first.wsId) > 0;
      }
      if (!keep) {
        cairo_surface_destroy(it->second.surf);
        it = g_paint.cardCache.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Every workspace card draws its per-window tiles whenever it has windows.
  // A workspace capture is only ever the backdrop: it stays behind the tiles
  // (the "desktop behind the apps" look) and fills the card alone only for
  // empty workspaces. A capture must never hide the windows it represents —
  // showing the frozen screenshot alone is what made the apps look like they
  // had slipped behind the desktop image.

  for (int wsIdx = 0; wsIdx < static_cast<int>(workspaces.size()); ++wsIdx) {
    const auto& ws = workspaces[static_cast<size_t>(wsIdx)];

    const OverviewCardRect baseCard = overview_card_rect(layout, wsIdx, scrollPos);
    OverviewCardRect card = baseCard;
    if (zoom != 1.0) {
      const double cx = card.x + card.w * 0.5;
      const double cy = card.y + card.h * 0.5;
      card.w *= zoom;
      card.h *= zoom;
      card.x = cx - card.w * 0.5;
      card.y = cy - card.h * 0.5;
    }

    const double lift = (wsIdx < static_cast<int>(wsHoverLifts.size()))
                            ? static_cast<double>(wsHoverLifts[static_cast<size_t>(wsIdx)])
                            : 0.0;
    if (vertical)
      card.y -= lift * 6.0 * us;
    else
      card.x -= lift * 6.0 * us;

    if (card.x + card.w < -margin || card.x > layout.w + margin ||
        card.y + card.h < -margin || card.y > layout.h + margin) {
      continue;
    }

    const bool wsEmpty = ws.windows.empty();
    const bool hasCap = ws.capture && ws.capture->width > 0 && ws.capture->height > 0;
    const bool useCapture = hasCap;
    const bool drawTiles = !wsEmpty;

    const int cw = std::max(1, static_cast<int>(std::round(baseCard.w)));
    const int ch = std::max(1, static_cast<int>(std::round(baseCard.h)));
    const std::uint64_t colorKey =
        (static_cast<std::uint64_t>(std::lround(colors.glassBgR * 255.0)) << 16) |
        (static_cast<std::uint64_t>(std::lround(colors.glassBgG * 255.0)) << 8) |
        static_cast<std::uint64_t>(std::lround(colors.glassBgB * 255.0));
    const std::uint64_t contentHash = compute_ws_content_hash(ws);
    const CardCacheKey key{useCapture ? ws.capture.get() : nullptr, ws.id, cw, ch, colorKey,
                           contentHash};
    auto it = g_paint.cardCache.find(key);
    if (it == g_paint.cardCache.end()) {
      ++g_paint.cardMisses;
      CachedImage img;
      img.w = cw;
      img.h = ch;

      // Try disk cache first (skip for live-capture cards — content changes every frame).
      img.surf = nullptr;
      if (!useCapture) {
        img.surf = card_disk_cache_load(ws.id, cw, ch, colorKey, contentHash);
        debug_log("overview", "disk_cache: load wsId=%d %dx%d hit=%d", ws.id, cw, ch, img.surf != nullptr);
      } else {
        debug_log("overview", "disk_cache: skip wsId=%d (capture-backed)", ws.id);
      }

      // Render from scratch on disk miss.
      if (!img.surf) {
        img.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
        if (cairo_surface_status(img.surf) == CAIRO_STATUS_SUCCESS) {
          cairo_t* rc = cairo_create(img.surf);
          cairo_translate(rc, -baseCard.x, -baseCard.y);

          paint_card_content(rc, colors, baseCard, useCapture ? ws.capture.get() : nullptr, us, 1.0f,
                             ws.overflow);

          if (drawTiles) {
            int paintedInWs = 0;
            for (const auto& w : ws.windows)
              if (!w.special) ++paintedInWs;
            int tileOrd = 0;
            for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
              const auto& win = ws.windows[static_cast<size_t>(j)];
              if (win.special) continue;
              OverviewCardRect tile = window_tile_rect(layout, baseCard, win, tileOrd, paintedInWs);
              paint_window_tile(rc, dock, colors, layout, tile, win, 1.0f);
              ++tileOrd;
            }
          }

          eh::widgets::slot_pill_style::paint_glass_layers(rc, baseCard.x, baseCard.y, baseCard.w, baseCard.h, 1.0, 24.0 * us);

          const std::string& label = ws.label;
          if (!label.empty()) {
            cairo_select_font_face(rc, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_NORMAL);
            cairo_set_font_size(rc, 12.0 * us);
            cairo_text_extents_t ex = {};
            cairo_text_extents(rc, label.c_str(), &ex);
            const double padH = 12.0 * us;
            const double pillW = std::max(40.0 * us, ex.x_advance + padH * 2.0);
            const double pillH = 20.0 * us;
            const double pillX = baseCard.x + (baseCard.w - pillW) * 0.5;
            const double pillY = baseCard.y + baseCard.h - pillH - 10.0 * us;
            {
              m3::Box box;
              box.setColor(1.0f, 1.0f, 1.0f, 0.10f);
              box.setRadius(static_cast<float>(pillH * 0.5));
              box.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                              static_cast<float>(pillW), static_cast<float>(pillH));
              box.setGlassy(true);
              box.paint(rc);
            }
            cairo_set_source_rgba(rc, colors.fgR, colors.fgG, colors.fgB, 1.0);
            cairo_move_to(rc, pillX + (pillW - ex.x_advance) * 0.5, pillY + pillH - 5.0 * us);
            cairo_show_text(rc, label.c_str());
          }

          cairo_destroy(rc);
        } else {
          cairo_surface_destroy(img.surf);
          img.surf = nullptr;
        }

        // Save non-capture cards to disk for next session.
        if (img.surf && !useCapture) {
          card_disk_cache_save(img.surf, ws.id, cw, ch, colorKey, contentHash);
          debug_log("overview", "disk_cache: saved wsId=%d %dx%d", ws.id, cw, ch);
        }
      }
      it = g_paint.cardCache.emplace(key, std::move(img)).first;
      ++g_paint.cardCacheGen;
    } else {
      ++g_paint.cardHits;
    }

    cairo_save(cr);
    rounded_rect(cr, card.x, card.y, card.w, card.h, 24.0 * us);
    cairo_clip(cr);

    if (it->second.surf) {
      if (zoom != 1.0) {
        cairo_save(cr);
        cairo_translate(cr, card.x, card.y);
        cairo_scale(cr, card.w / baseCard.w, card.h / baseCard.h);
        cairo_set_source_surface(cr, it->second.surf, 0, 0);
        if (alpha < 1.0f) cairo_paint_with_alpha(cr, alpha);
        else cairo_paint(cr);
        cairo_restore(cr);
      } else {
        cairo_set_source_surface(cr, it->second.surf, card.x, card.y);
        if (alpha < 1.0f) cairo_paint_with_alpha(cr, alpha);
        else cairo_paint(cr);
      }
    }

    if (wsIdx == hoveredWs) {
      cairo_set_source_rgba(cr, 1, 1, 1, 0.06 * alpha);
      cairo_paint(cr);
    }

    if (drawTiles) {
      int paintedInWs = 0;
      for (const auto& w : ws.windows)
        if (!w.special) ++paintedInWs;
      int flat = flat_base(workspaces, wsIdx);
      int tileOrd = 0;
      for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
        const auto& win = ws.windows[static_cast<size_t>(j)];
        if (win.special) continue;

        const bool isDrag = (wsIdx == draggedWs && flat == draggedWinFlat);
        const double winLift = (flat < static_cast<int>(winHoverLifts.size()))
                                   ? static_cast<double>(winHoverLifts[static_cast<size_t>(flat)])
                                   : 0.0;
        const bool hovered = (wsIdx == hoveredWs && flat == hoveredWinFlat && !isDrag);
        const bool cHov = hovered && closeHovered;

        OverviewCardRect tile = window_tile_rect(layout, card, win, tileOrd, paintedInWs);
        if (vertical)
          tile.y -= winLift * 4.0 * us;
        else
          tile.x -= winLift * 4.0 * us;

        if (hovered) {
          cairo_set_source_rgba(cr, 1, 1, 1, 0.10 * alpha);
          rounded_rect(cr, tile.x, tile.y, tile.w, tile.h, 12.0 * us);
          cairo_fill(cr);
        }
        if (hovered) paint_close_button(cr, layout, tile, cHov, alpha);
        ++flat;
        ++tileOrd;
      }
    }

    if (wsIdx == selectedIndex) {
      cairo_set_line_width(cr, 3.0 * us);
      cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB, 0.9 * alpha);
      rounded_rect(cr, card.x, card.y, card.w, card.h, 24.0 * us);
      cairo_stroke(cr);
    }

    if (wsIdx == dropTargetWs) {
      cairo_set_line_width(cr, 4.0 * us);
      cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB, 0.95 * alpha);
      rounded_rect(cr, card.x, card.y, card.w, card.h, 24.0 * us);
      cairo_stroke(cr);
    }

    cairo_restore(cr);
  }

  // Dragged ghost — painted topmost at reduced opacity
  if (draggedWs >= 0 && draggedWs < static_cast<int>(workspaces.size())) {
    int gflat = 0;
    for (int k = 0; k < static_cast<int>(workspaces.size()); ++k) {
      const auto& ws = workspaces[static_cast<size_t>(k)];
      int tileOrd = 0;
      for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
        const auto& win = ws.windows[static_cast<size_t>(j)];
        if (win.special) continue;
        if (k == draggedWs && gflat == draggedWinFlat) {
          const OverviewCardRect card = overview_card_rect(layout, k, scrollPos);
          OverviewCardRect tile =
              window_tile_rect(layout, card, win, tileOrd, static_cast<int>(ws.windows.size()));
          // ghostX/ghostY already preserve the grab offset (cursor + drag
          // delta), so the ghost stays where it was picked up and follows the
          // cursor even while the strip auto-scrolls beneath it.
          tile.x = ghostX;
          tile.y = ghostY;
          cairo_save(cr);
          // Scale 5% larger about the tile's own center. paint_window_tile()
          // draws using tile's *absolute* x/y, so the matching un-translate
          // must subtract the full center point (tile.x + w/2, tile.y + h/2),
          // not just half the tile's width/height — otherwise the tile's
          // absolute position gets scaled too, shoving the ghost far to the
          // right/down of the cursor instead of just growing it in place.
          cairo_translate(cr, tile.x + tile.w * 0.5, tile.y + tile.h * 0.5);
          cairo_scale(cr, 1.05, 1.05);
          cairo_translate(cr, -(tile.x + tile.w * 0.5), -(tile.y + tile.h * 0.5));
          paint_window_tile(cr, dock, colors, layout, tile, win, alpha * 0.9f);
          cairo_restore(cr);
          break;
        }
        ++gflat;
        ++tileOrd;
      }
    }
  }
}

int pick_workspace_at(const OverviewLayout& layout, double mx, double my,
                      const std::vector<OverviewWorkspace>& workspaces, double scrollPos) {
  for (int i = static_cast<int>(workspaces.size()) - 1; i >= 0; --i) {
    const OverviewCardRect r = overview_card_rect(layout, i, scrollPos);
    if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) return i;
  }
  return -1;
}

int pick_window_at(const OverviewLayout& layout, const std::vector<OverviewWorkspace>& workspaces,
                   double mx, double my, int wsIndex, double scrollPos, int* outFlat) {
  if (outFlat) *outFlat = -1;
  if (wsIndex < 0 || wsIndex >= static_cast<int>(workspaces.size())) return -1;
  const auto& ws = workspaces[static_cast<size_t>(wsIndex)];
  const OverviewCardRect card = overview_card_rect(layout, wsIndex, scrollPos);
  int flat = flat_base(workspaces, wsIndex);
  int tileOrd = 0;
  int hit = -1;
  for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
    const auto& win = ws.windows[static_cast<size_t>(j)];
    if (win.special) continue;
    const OverviewCardRect t = window_tile_rect(layout, card, win, tileOrd,
                                                static_cast<int>(ws.windows.size()));
    if (mx >= t.x && mx <= t.x + t.w && my >= t.y && my <= t.y + t.h) hit = flat;
    ++flat;
    ++tileOrd;
  }
  if (hit >= 0) {
    if (outFlat) *outFlat = hit;
    return wsIndex;
  }
  return -1;
}

OverviewCardRect overview_tile_rect_for_flat(const OverviewLayout& layout,
                                             const std::vector<OverviewWorkspace>& workspaces,
                                             int flatIdx, double scrollPos, bool* outOk) {
  if (outOk) *outOk = false;
  int flat = 0;
  for (int ws = 0; ws < static_cast<int>(workspaces.size()); ++ws) {
    const auto& w = workspaces[static_cast<size_t>(ws)];
    const OverviewCardRect card = overview_card_rect(layout, ws, scrollPos);
    int tileOrd = 0;
    for (int j = 0; j < static_cast<int>(w.windows.size()); ++j) {
      const auto& win = w.windows[static_cast<size_t>(j)];
      if (win.special) continue;
      if (flat == flatIdx) {
        if (outOk) *outOk = true;
        return window_tile_rect(layout, card, win, tileOrd, static_cast<int>(w.windows.size()));
      }
      ++flat;
      ++tileOrd;
    }
  }
  return {};
}

bool pick_close_at(const OverviewLayout& layout, const std::vector<OverviewWorkspace>& workspaces,
                   double mx, double my, int wsIndex, double scrollPos, int* outFlat) {
  if (outFlat) *outFlat = -1;
  if (wsIndex < 0 || wsIndex >= static_cast<int>(workspaces.size())) return false;
  const auto& ws = workspaces[static_cast<size_t>(wsIndex)];
  const OverviewCardRect card = overview_card_rect(layout, wsIndex, scrollPos);
  int flat = flat_base(workspaces, wsIndex);
  int tileOrd = 0;
  bool hit = false;
  for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
    const auto& win = ws.windows[static_cast<size_t>(j)];
    if (win.special) continue;
    const OverviewCardRect t = window_tile_rect(layout, card, win, tileOrd,
                                                static_cast<int>(ws.windows.size()));
    const double bs = layout.closeBtnSz * 0.72;
    const double bx = t.x + t.w - bs * 0.5;
    const double by = t.y - bs * 0.5;
    if (mx >= bx && mx <= bx + bs && my >= by && my <= by + bs) {
      hit = true;
      if (outFlat) *outFlat = flat;
    }
    ++flat;
    ++tileOrd;
  }
  return hit;
}

namespace {

// Trim `text` with an ellipsis so it fits within maxW at the current font.
std::string ellipsize_label(cairo_t* cr, const std::string& text, double maxW) {
  cairo_text_extents_t ex;
  cairo_text_extents(cr, text.c_str(), &ex);
  if (ex.x_advance <= maxW) return text;
  static const std::string kDots = "…";
  size_t len = text.size();
  while (len > 0) {
    // Skip continuation bytes so multi-byte UTF-8 chars stay intact.
    --len;
    while (len > 0 && (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80) --len;
    const std::string cand = text.substr(0, len) + kDots;
    cairo_text_extents(cr, cand.c_str(), &ex);
    if (ex.x_advance <= maxW) return cand;
  }
  return kDots;
}

} // namespace

void compute_app_grid_layout(AppGridLayout& grid, double w, double h, double searchY,
                             double uiScale, int nApps) {
  const double topY = searchY + 50 * uiScale;
  const double bottomY = h - 60 * uiScale;
  const double availH = bottomY - topY;
  grid.scrollMax = 0.0;
  if (availH <= 0) return;

  grid.iconSize = 56 * uiScale;
  grid.fontSize = 12 * uiScale;
  grid.cellW = grid.iconSize + 20 * uiScale;
  grid.cellH = grid.iconSize + grid.fontSize + 24 * uiScale;
  grid.cols = std::max(1, static_cast<int>((w - 40 * uiScale) / grid.cellW));
  grid.rows = std::max(1, static_cast<int>(availH / grid.cellH));
  if (grid.cols < 4) grid.cols = 4;

  // Keep the viewport fully filled and scroll when more rows are needed, so
  // the card geometry stays stable no matter how many apps match.
  const int neededRows = (std::max(nApps, 1) + grid.cols - 1) / grid.cols;
  if (neededRows > grid.rows)
    grid.scrollMax = static_cast<double>(neededRows - grid.rows) * grid.cellH;

  const double totalW = static_cast<double>(grid.cols) * grid.cellW;
  const double totalH = static_cast<double>(grid.rows) * grid.cellH;
  grid.startX = (w - totalW) * 0.5;
  grid.startY = topY + (availH - totalH) * 0.5;
}

void paint_app_grid(cairo_t* cr, DockApp& dock, const OverviewColors& colors,
                    const OverviewLayout& layout,
                    const std::vector<SpotlightHit>& apps,
                    int hoveredIdx, float hoverLift, float progress,
                    double scrollPos) {
  const float alpha = std::clamp(progress, 0.0f, 1.0f);
  if (alpha <= 0.0f) return;

  const auto& grid = layout.appGrid;
  if (grid.cols <= 0 || grid.rows <= 0 || grid.cellW <= 0.0 || grid.cellH <= 0.0) return;

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  // Empty state: keep the card up and say why it's empty.
  if (apps.empty()) {
    m3::Box box;
    box.setColor(static_cast<float>(colors.glassBgR), static_cast<float>(colors.glassBgG),
                 static_cast<float>(colors.glassBgB), static_cast<float>(0.78 * alpha));
    box.setRadius(static_cast<float>(24.0 * layout.uiScale));
    box.setGeometry(static_cast<float>(layout.w * 0.5 - 220.0 * layout.uiScale),
                    static_cast<float>(grid.startY),
                    static_cast<float>(440.0 * layout.uiScale),
                    static_cast<float>(160.0 * layout.uiScale));
    box.setGlassy(true);
    box.paint(cr);
    cairo_set_source_rgba(cr, colors.dimFgR, colors.dimFgG, colors.dimFgB, 0.9 * alpha);
    cairo_set_font_size(cr, 15.0 * layout.uiScale);
    cairo_text_extents_t ex{};
    const std::string msg = "No apps found";
    cairo_text_extents(cr, msg.c_str(), &ex);
    cairo_move_to(cr, layout.w * 0.5 - ex.x_advance * 0.5,
                  grid.startY + 80.0 * layout.uiScale + ex.height * 0.5);
    cairo_show_text(cr, msg.c_str());
    return;
  }

  const double us = layout.uiScale;
  const double cardX = grid.startX - 16.0 * us;
  const double cardY = grid.startY - 16.0 * us;
  const double cardW = static_cast<double>(grid.cols) * grid.cellW + 32.0 * us;
  const double cardH = static_cast<double>(grid.rows) * grid.cellH + 32.0 * us;

  // Glassy settings-style card behind the app grid
  {
    m3::Box box;
    box.setColor(static_cast<float>(colors.glassBgR), static_cast<float>(colors.glassBgG),
                 static_cast<float>(colors.glassBgB), static_cast<float>(0.78 * alpha));
    box.setRadius(static_cast<float>(24.0 * us));
    box.setGeometry(static_cast<float>(cardX), static_cast<float>(cardY),
                    static_cast<float>(cardW), static_cast<float>(cardH));
    box.setGlassy(true);
    box.paint(cr);
  }

  // Clip to the card so scrolled rows never bleed outside it.
  cairo_save(cr);
  rounded_rect(cr, cardX, cardY, cardW, cardH, 24.0 * us);
  cairo_clip(cr);

  const int nApps = static_cast<int>(apps.size());
  const double viewH = static_cast<double>(grid.rows) * grid.cellH;
  const double scroll = std::clamp(scrollPos, 0.0, std::max(0.0, grid.scrollMax));
  const int totalRows = (nApps + grid.cols - 1) / grid.cols;
  const int firstRow = std::max(0, static_cast<int>(scroll / grid.cellH));
  const int lastRow = std::min(totalRows,
      static_cast<int>((scroll + viewH) / grid.cellH) + 1);

  for (int i = firstRow * grid.cols; i < nApps; ++i) {
    const int row = i / grid.cols;
    if (row > lastRow) break;
    const int col = i % grid.cols;
    const double cellX = grid.startX + col * grid.cellW;
    const double cellY = grid.startY + row * grid.cellH - scroll;
    const bool hovered = (i == hoveredIdx);
    const double lift = hovered ? static_cast<double>(hoverLift) : 0.0;
    const double liftPx = lift * 6.0 * us;

    // Rounded pill highlight behind the whole cell (icon + label).
    if (lift > 0.01f) {
      cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB,
                            0.16 * alpha * lift);
      rounded_rect(cr, cellX + 6.0 * us, cellY + 4.0 * us - liftPx,
                   grid.cellW - 12.0 * us, grid.cellH - 8.0 * us, 14.0 * us);
      cairo_fill(cr);
      cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB,
                            0.35 * alpha * lift);
      cairo_set_line_width(cr, 1.2 * us);
      rounded_rect(cr, cellX + 6.0 * us, cellY + 4.0 * us - liftPx,
                   grid.cellW - 12.0 * us, grid.cellH - 8.0 * us, 14.0 * us);
      cairo_stroke(cr);
    }

    const double cx = cellX + grid.cellW * 0.5;
    const double iconX = cx - grid.iconSize * 0.5;
    const double iconY = cellY + 10.0 * us - liftPx;
    const std::string& iconKey = apps[static_cast<size_t>(i)].iconKey;
    const std::string& name = apps[static_cast<size_t>(i)].name;

    // iconKey here is a themed icon name (desktop Icon= key), not an appId:
    // tray_icon() is the correct resolver and avoids the appId desktop-file
    // scan that app_icon() would trigger on every miss. The size-aware
    // overload rasterizes on the background thread at the display bucket;
    // while a raster is pending it returns nullptr and the placeholder
    // squircle below covers the cell until the surface lands. Request
    // physical pixels (logical size × UI scale) so HiDPI never upscales a
    // small bucket — that was the source of blurry grid icons.
    const int wantPx = std::max(16, static_cast<int>(std::ceil(grid.iconSize * us)));
    const eh::icons::IconEntry* ic = dock.icons.tray_icon(iconKey, wantPx);
    if (ic && ic->surface) {
      cairo_save(cr);
      cairo_translate(cr, iconX, iconY);
      const double iw = static_cast<double>(ic->width);
      const double ih = static_cast<double>(ic->height);
      const double sc = grid.iconSize / std::max(1.0, std::max(iw, ih));
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, ic->surface, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
      cairo_paint_with_alpha(cr, alpha);
      cairo_restore(cr);
    } else {
      // Fallback: accent-tinted squircle with the app's initial.
      cairo_set_source_rgba(cr,
                            colors.thumbActiveBorderR * 0.35 + colors.wsBgR * 0.65,
                            colors.thumbActiveBorderG * 0.35 + colors.wsBgG * 0.65,
                            colors.thumbActiveBorderB * 0.35 + colors.wsBgB * 0.65,
                            alpha);
      rounded_rect(cr, iconX + 3.0 * us, iconY + 3.0 * us,
                   grid.iconSize - 6.0 * us, grid.iconSize - 6.0 * us,
                   (grid.iconSize - 6.0 * us) * 0.28);
      cairo_fill(cr);
      if (!name.empty()) {
        cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, alpha);
        cairo_set_font_size(cr, grid.fontSize * 1.8);
        cairo_text_extents_t fex;
        const std::string ch = name.substr(0, 1);
        cairo_text_extents(cr, ch.c_str(), &fex);
        cairo_move_to(cr, cx - fex.x_advance * 0.5 - fex.x_bearing,
                      iconY + grid.iconSize * 0.5 + fex.height * 0.5);
        cairo_show_text(cr, ch.c_str());
      }
    }

    if (!name.empty()) {
      cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, alpha);
      cairo_set_font_size(cr, grid.fontSize);
      const std::string label =
          ellipsize_label(cr, name, grid.cellW - 10.0 * us);
      cairo_text_extents_t ex;
      cairo_text_extents(cr, label.c_str(), &ex);
      const double labelY =
          cellY + 10.0 * us + grid.iconSize + grid.fontSize + 4.0 * us - liftPx;
      cairo_move_to(cr, cx - ex.x_advance * 0.5, labelY);
      cairo_show_text(cr, label.c_str());
    }
  }

  cairo_restore(cr);

  // Scrollbar along the card's right edge when content overflows.
  if (grid.scrollMax > 0.0) {
    const double trackH = cardH - 20.0 * us;
    const double thumbH = std::max(24.0 * us,
        trackH * viewH / (viewH + grid.scrollMax));
    const double maxTravel = std::max(0.0, trackH - thumbH);
    const double frac = scroll / grid.scrollMax;
    const double thumbY = cardY + 10.0 * us + frac * maxTravel;
    const double sbX = cardX + cardW - 8.0 * us;
    cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, 0.10 * alpha);
    rounded_rect(cr, sbX, cardY + 10.0 * us, 4.0 * us, trackH, 2.0 * us);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, 0.35 * alpha);
    rounded_rect(cr, sbX, thumbY, 4.0 * us, thumbH, 2.0 * us);
    cairo_fill(cr);
  }
}

int pick_app_at(const AppGridLayout& grid, double mx, double my, int nApps,
                double scrollPos) {
  if (grid.cols <= 0 || grid.rows <= 0 || nApps <= 0 || grid.cellW <= 0.0 || grid.cellH <= 0.0)
    return -1;
  const double scroll = std::clamp(scrollPos, 0.0, std::max(0.0, grid.scrollMax));
  const int col = static_cast<int>((mx - grid.startX) / grid.cellW);
  const int row = static_cast<int>((my - grid.startY + scroll) / grid.cellH);
  if (col < 0 || col >= grid.cols || row < 0 || row >= grid.rows) return -1;
  const int idx = row * grid.cols + col;
  if (idx >= nApps) return -1;

  const double cellX = grid.startX + col * grid.cellW;
  const double cellY = grid.startY + row * grid.cellH - scroll;
  if (mx < cellX || mx > cellX + grid.cellW || my < cellY || my > cellY + grid.cellH)
    return -1;
  return idx;
}

void compute_quick_select_layout(QuickSelectLayout& qs, OverviewAxis axis, double w, double h,
                                 double cardW, double cardH, double uiScale, int nWorkspaces) {
  qs.horizontal = (axis == OverviewAxis::Horizontal);
  qs.padX = 16.0 * uiScale;
  qs.padY = 12.0 * uiScale;
  qs.thumbGap = 8.0 * uiScale;
  qs.thumbRadius = 8.0 * uiScale;

  // Thumbnail is 25% of card dimensions
  qs.thumbW = std::max(40.0, cardW * 0.25);
  qs.thumbH = std::max(30.0, cardH * 0.25);

  if (qs.horizontal) {
    // Strip at bottom: thumbnails side by side
    const double stripContentH = qs.thumbH + qs.padY * 2.0;
    qs.stripX = 0;
    qs.stripY = h - stripContentH;
    qs.stripW = w;
    qs.stripH = stripContentH;
  } else {
    // Strip on left: thumbnails stacked vertically
    const double stripContentW = qs.thumbW + qs.padX * 2.0;
    qs.stripX = 0;
    qs.stripY = 0;
    qs.stripW = stripContentW;
    qs.stripH = h;
  }

  // The add-workspace slot sits right after the last thumbnail.
  const int n = std::max(1, nWorkspaces);
  if (qs.horizontal) {
    const double totalW = static_cast<double>(n) * qs.thumbW +
                          static_cast<double>(std::max(0, n - 1)) * qs.thumbGap;
    qs.addRect.w = qs.thumbW;
    qs.addRect.h = qs.thumbH;
    qs.addRect.x = qs.stripX + (qs.stripW - totalW) * 0.5 +
                   static_cast<double>(n) * (qs.thumbW + qs.thumbGap);
    qs.addRect.y = qs.stripY + qs.padY;
  } else {
    const double totalH = static_cast<double>(n) * qs.thumbH +
                          static_cast<double>(std::max(0, n - 1)) * qs.thumbGap;
    qs.addRect.w = qs.thumbW;
    qs.addRect.h = qs.thumbH;
    qs.addRect.x = qs.stripX + qs.padX;
    qs.addRect.y = qs.stripY + (qs.stripH - totalH) * 0.5 +
                   static_cast<double>(n) * (qs.thumbH + qs.thumbGap);
  }
}

namespace {
void paint_strip_contents(cairo_t* cr, const OverviewColors& colors,
                          const QuickSelectLayout& qs, const OverviewLayout& layout,
                          const std::vector<OverviewWorkspace>& workspaces, float alpha) {
  const double us = layout.uiScale;
  const int nWs = static_cast<int>(workspaces.size());

  {
    m3::Box box;
    box.setColor(static_cast<float>(colors.glassBgR), static_cast<float>(colors.glassBgG),
                 static_cast<float>(colors.glassBgB), static_cast<float>(0.55 * alpha));
    box.setRadius(static_cast<float>(qs.thumbRadius * 2.0));
    box.setGeometry(static_cast<float>(qs.stripX + 4.0 * us),
                    static_cast<float>(qs.stripY + 4.0 * us),
                    static_cast<float>(qs.stripW - 8.0 * us),
                    static_cast<float>(qs.stripH - 8.0 * us));
    box.setGlassy(true);
    box.paint(cr);
  }

  for (int i = 0; i < nWs; ++i) {
    const auto& ws = workspaces[static_cast<size_t>(i)];

    double tx = 0, ty = 0;
    if (qs.horizontal) {
      const double totalW = static_cast<double>(nWs) * qs.thumbW +
                            static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
      tx = qs.stripX + (qs.stripW - totalW) * 0.5 +
           static_cast<double>(i) * (qs.thumbW + qs.thumbGap);
      ty = qs.stripY + qs.padY;
    } else {
      const double totalH = static_cast<double>(nWs) * qs.thumbH +
                            static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
      tx = qs.stripX + qs.padX;
      ty = qs.stripY + (qs.stripH - totalH) * 0.5 +
           static_cast<double>(i) * (qs.thumbH + qs.thumbGap);
    }

    cairo_save(cr);
    rounded_rect(cr, tx, ty, qs.thumbW, qs.thumbH, qs.thumbRadius);
    cairo_clip(cr);

    // Try to reuse the main card cache surface for this workspace.
    cairo_surface_t* cardSurf = nullptr;
    double cardW = 0, cardH = 0;
    const bool wsHasCap = ws.capture && ws.capture->width > 0 && ws.capture->height > 0;
    for (const auto& [k, v] : g_paint.cardCache) {
      if (!v.surf) continue;
      if (wsHasCap) {
        if (k.cap == ws.capture.get()) {
          cardSurf = v.surf;
          cardW = static_cast<double>(v.w);
          cardH = static_cast<double>(v.h);
          break;
        }
      } else {
        if (k.cap == nullptr && k.wsId == ws.id) {
          cardSurf = v.surf;
          cardW = static_cast<double>(v.w);
          cardH = static_cast<double>(v.h);
          break;
        }
      }
    }

    if (cardSurf && cardW > 0 && cardH > 0) {
      cairo_set_source_rgba(cr, colors.wsBgR, colors.wsBgG, colors.wsBgB, 0.9 * alpha);
      cairo_rectangle(cr, tx, ty, qs.thumbW, qs.thumbH);
      cairo_fill(cr);
      cairo_translate(cr, tx, ty);
      cairo_scale(cr, qs.thumbW / cardW, qs.thumbH / cardH);
      cairo_set_source_surface(cr, cardSurf, 0, 0);
      cairo_paint_with_alpha(cr, alpha);
    } else {
      const bool hasCap = ws.capture && ws.capture->width > 0 && ws.capture->height > 0;
      if (hasCap) {
        paint_capture_background(cr, {tx, ty, qs.thumbW, qs.thumbH}, *ws.capture, alpha * 0.9f);
      } else {
        cairo_set_source_rgba(cr, colors.wsBgR, colors.wsBgG, colors.wsBgB, 0.9 * alpha);
        cairo_rectangle(cr, tx, ty, qs.thumbW, qs.thumbH);
        cairo_fill(cr);
      }

      if (!ws.windows.empty()) {
        int paintedInWs = 0;
        for (const auto& w : ws.windows)
          if (!w.special) ++paintedInWs;
        if (paintedInWs > 0) {
          const double pad = 3.0 * us;
          const double innerW = qs.thumbW - pad * 2.0;
          const double innerH = qs.thumbH - pad * 2.0;
          const int nWin = paintedInWs;
          const int cols = std::min(3, std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nWin))))));
          const int rows = (nWin + cols - 1) / cols;
          const double cellW = (innerW - static_cast<double>(cols - 1) * 2.0 * us) / cols;
          const double cellH = (innerH - static_cast<double>(rows - 1) * 2.0 * us) / rows;
          int tileOrd = 0;
          for (int j = 0; j < static_cast<int>(ws.windows.size()); ++j) {
            const auto& win = ws.windows[static_cast<size_t>(j)];
            if (win.special) continue;
            const int col = tileOrd % cols;
            const int row = tileOrd / cols;
            const double cellX = tx + pad + static_cast<double>(col) * (cellW + 2.0 * us);
            const double cellY = ty + pad + static_cast<double>(row) * (cellH + 2.0 * us);
            const OverviewCardRect tile{cellX, cellY, cellW, cellH};
            const bool liveImg = win.liveValid && win.liveW > 0 && win.liveH > 0 &&
                                 win.liveBgra.size() >= static_cast<size_t>(win.liveW) *
                                                             static_cast<size_t>(win.liveH) * 4u;
            if (liveImg) {
              paint_live_frame(cr, tile, win, alpha);
            } else if (!hasCap) {
              double wr = 0.0, wg = 0.0, wb = 0.0;
              if (!win.appId.empty())
                app_tint(win.appId, wr, wg, wb);
              else
                hsl_to_rgb(0.0, 0.0, 0.55, wr, wg, wb);
              cairo_set_source_rgba(cr, wr * 0.6 + colors.wsBgR * 0.4,
                                    wg * 0.6 + colors.wsBgG * 0.4,
                                    wb * 0.6 + colors.wsBgB * 0.4, 0.85 * alpha);
              rounded_rect(cr, cellX, cellY, cellW, cellH, 3.0 * us);
              cairo_fill(cr);
            }
            ++tileOrd;
          }
        }
      }
    }

    cairo_restore(cr);

    if (qs.thumbH > 40.0 * us) {
      eh::widgets::slot_pill_style::paint_glass_layers(cr, tx, ty, qs.thumbW, qs.thumbH,
                                                       alpha * 0.7f, qs.thumbRadius);
    }

    const std::string& label = ws.label;
    if (!label.empty()) {
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 9.0 * us);
      cairo_text_extents_t ex = {};
      cairo_text_extents(cr, label.c_str(), &ex);
      const double labelW = std::min(ex.x_advance, qs.thumbW - 4.0 * us);
      double labelX = 0, labelY = 0;
      if (qs.horizontal) {
        labelX = tx + (qs.thumbW - labelW) * 0.5;
        labelY = ty + qs.thumbH + 12.0 * us;
      } else {
        labelX = tx + qs.thumbW + 6.0 * us;
        labelY = ty + qs.thumbH * 0.5 + 3.0 * us;
      }
      cairo_set_source_rgba(cr, colors.fgR, colors.fgG, colors.fgB, alpha * 0.8);
      cairo_save(cr);
      if (qs.horizontal) {
        cairo_rectangle(cr, tx, ty + qs.thumbH, qs.thumbW, 16.0 * us);
      } else {
        cairo_rectangle(cr, tx + qs.thumbW, ty, 60.0 * us, qs.thumbH);
      }
      cairo_clip(cr);
      cairo_move_to(cr, labelX, labelY);
      cairo_show_text(cr, label.c_str());
      cairo_restore(cr);
    }
  }

  // Add-workspace slot.
  {
    const double ax = qs.addRect.x, ay = qs.addRect.y;
    const double aw = qs.addRect.w, ah = qs.addRect.h;

    cairo_set_source_rgba(cr, colors.thumbBgR, colors.thumbBgG, colors.thumbBgB, 0.55 * alpha);
    rounded_rect(cr, ax, ay, aw, ah, qs.thumbRadius);
    cairo_fill(cr);

    // Dashed outline marks it as a drop target rather than a live workspace.
    const double dashLen = std::max(2.0, 3.0 * us);
    const double dashes[] = {dashLen, dashLen};
    cairo_set_dash(cr, dashes, 2, 0.0);
    cairo_set_line_width(cr, 1.4 * us);
    cairo_set_source_rgba(cr, colors.dimFgR, colors.dimFgG, colors.dimFgB, 0.55 * alpha);
    rounded_rect(cr, ax + 0.7 * us, ay + 0.7 * us, aw - 1.4 * us, ah - 1.4 * us, qs.thumbRadius);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0.0);

    const double glyphPx = std::min(aw, ah) * 0.42;
    eh::shell::draw_material_glyph(cr, ax + aw * 0.5, ay + ah * 0.5, glyphPx,
                                   "add", colors.dimFgR, colors.dimFgG, colors.dimFgB,
                                   0.8 * alpha);
  }
}
} // namespace

void paint_quick_select_strip(cairo_t* cr, DockApp& dock, const OverviewColors& colors,
                               const QuickSelectLayout& qs, const OverviewLayout& layout,
                               const std::vector<OverviewWorkspace>& workspaces,
                               int selectedIndex, int hoveredWs, int hoveredQs,
                               float progress) {
  (void)dock;
  (void)hoveredWs;
  const float alpha = std::clamp(progress, 0.0f, 1.0f);
  if (alpha <= 0.0f || workspaces.empty()) return;
  const int nWs = static_cast<int>(workspaces.size());

  const int sw = std::max(1, static_cast<int>(std::round(qs.stripW)));
  const int sh = std::max(1, static_cast<int>(std::round(qs.stripH)));

  std::uint64_t shash = 14695981039346656037ull;
  auto mix = [&](std::uint64_t v) { shash ^= v; shash *= 1099511628211ull; };
  mix(static_cast<std::uint64_t>(nWs));
  for (const auto& ws : workspaces) {
    mix(compute_ws_content_hash(ws));
  }
  mix(g_paint.cardCacheGen);

  if (g_paint.strip.surf && g_paint.strip.w == sw && g_paint.strip.h == sh &&
      g_paint.strip.hash == shash) {
    ++g_paint.stripHits;
    cairo_set_source_surface(cr, g_paint.strip.surf, qs.stripX, qs.stripY);
    cairo_paint_with_alpha(cr, alpha);
  } else {
    ++g_paint.stripMisses;
    if (!g_paint.strip.surf || g_paint.strip.w != sw || g_paint.strip.h != sh) {
      if (g_paint.strip.surf) cairo_surface_destroy(g_paint.strip.surf);
      g_paint.strip.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
      g_paint.strip.w = sw;
      g_paint.strip.h = sh;
    }
    if (cairo_surface_status(g_paint.strip.surf) == CAIRO_STATUS_SUCCESS) {
      cairo_t* rc = cairo_create(g_paint.strip.surf);
      cairo_save(rc);
      cairo_set_operator(rc, CAIRO_OPERATOR_CLEAR);
      cairo_paint(rc);
      cairo_set_operator(rc, CAIRO_OPERATOR_OVER);
      cairo_translate(rc, -qs.stripX, -qs.stripY);
      paint_strip_contents(rc, colors, qs, layout, workspaces, 1.0f);
      cairo_restore(rc);
      cairo_destroy(rc);
      g_paint.strip.hash = shash;
    } else {
      g_paint.strip.hash = 0;
    }
    cairo_set_source_surface(cr, g_paint.strip.surf, qs.stripX, qs.stripY);
    cairo_paint_with_alpha(cr, alpha);
  }

  {
    const double us = layout.uiScale;
    for (int i = 0; i < nWs; ++i) {
      const auto& ws = workspaces[static_cast<size_t>(i)];
      const bool isHovered = (i == hoveredQs);
      const bool isSelected = (i == selectedIndex);
      const bool isActive = ws.active;
      if (!isHovered && !isSelected && !isActive) continue;

      double tx = 0, ty = 0;
      if (qs.horizontal) {
        const double totalW = static_cast<double>(nWs) * qs.thumbW +
                              static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
        tx = qs.stripX + (qs.stripW - totalW) * 0.5 +
             static_cast<double>(i) * (qs.thumbW + qs.thumbGap);
        ty = qs.stripY + qs.padY;
      } else {
        const double totalH = static_cast<double>(nWs) * qs.thumbH +
                              static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
        tx = qs.stripX + qs.padX;
        ty = qs.stripY + (qs.stripH - totalH) * 0.5 +
             static_cast<double>(i) * (qs.thumbH + qs.thumbGap);
      }

      if (isHovered) {
        const double liftPx = 3.0 * us;
        if (qs.horizontal) ty -= liftPx; else tx -= liftPx;
        cairo_set_source_rgba(cr, 1, 1, 1, 0.12 * alpha);
        rounded_rect(cr, tx, ty, qs.thumbW, qs.thumbH, qs.thumbRadius);
        cairo_fill(cr);
      }

      if (isSelected || isActive) {
        cairo_set_line_width(cr, 2.0 * us);
        cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB,
                              (isSelected ? 0.9 : 0.55) * alpha);
        rounded_rect(cr, tx, ty, qs.thumbW, qs.thumbH, qs.thumbRadius);
        cairo_stroke(cr);
      }
    }

    // Add-workspace slot highlight (hover or window dragged over it).
    if (hoveredQs == kQuickSelectAddIdx) {
      double ax = qs.addRect.x, ay = qs.addRect.y;
      const double liftPx = 3.0 * us;
      if (qs.horizontal) ay -= liftPx; else ax -= liftPx;

      cairo_set_source_rgba(cr, 1, 1, 1, 0.12 * alpha);
      rounded_rect(cr, ax, ay, qs.addRect.w, qs.addRect.h, qs.thumbRadius);
      cairo_fill(cr);

      cairo_set_line_width(cr, 2.0 * us);
      cairo_set_source_rgba(cr, colors.accentR, colors.accentG, colors.accentB, 0.95 * alpha);
      rounded_rect(cr, ax, ay, qs.addRect.w, qs.addRect.h, qs.thumbRadius);
      cairo_stroke(cr);

      const double glyphPx = std::min(qs.addRect.w, qs.addRect.h) * 0.42;
      eh::shell::draw_material_glyph(cr, ax + qs.addRect.w * 0.5, ay + qs.addRect.h * 0.5,
                                     glyphPx, "add", colors.accentR, colors.accentG,
                                     colors.accentB, 0.95 * alpha);
    }
  }
}

int pick_quick_select_at(const QuickSelectLayout& qs, const OverviewLayout& layout,
                         const std::vector<OverviewWorkspace>& workspaces,
                         double mx, double my) {
  (void)layout;
  const int nWs = static_cast<int>(workspaces.size());
  if (nWs <= 0) return -1;

  // Quick bounds check
  if (mx < qs.stripX || mx > qs.stripX + qs.stripW ||
      my < qs.stripY || my > qs.stripY + qs.stripH)
    return -1;

  for (int i = 0; i < nWs; ++i) {
    double tx = 0, ty = 0;
    if (qs.horizontal) {
      const double totalW = static_cast<double>(nWs) * qs.thumbW +
                            static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
      tx = qs.stripX + (qs.stripW - totalW) * 0.5 +
           static_cast<double>(i) * (qs.thumbW + qs.thumbGap);
      ty = qs.stripY + qs.padY;
    } else {
      const double totalH = static_cast<double>(nWs) * qs.thumbH +
                            static_cast<double>(std::max(0, nWs - 1)) * qs.thumbGap;
      tx = qs.stripX + qs.padX;
      ty = qs.stripY + (qs.stripH - totalH) * 0.5 +
           static_cast<double>(i) * (qs.thumbH + qs.thumbGap);
    }
    if (mx >= tx && mx <= tx + qs.thumbW && my >= ty && my <= ty + qs.thumbH)
      return i;
  }
  return -1;
}

bool pick_quick_select_add_at(const QuickSelectLayout& qs, double mx, double my) {
  return mx >= qs.addRect.x && mx <= qs.addRect.x + qs.addRect.w &&
         my >= qs.addRect.y && my <= qs.addRect.y + qs.addRect.h;
}

void get_cache_stats(int& cardHits, int& cardMisses, int& winHits, int& winMisses,
                     int& stripHits, int& stripMisses) {
  cardHits = g_paint.cardHits;
  cardMisses = g_paint.cardMisses;
  winHits = g_paint.winHits;
  winMisses = g_paint.winMisses;
  stripHits = g_paint.stripHits;
  stripMisses = g_paint.stripMisses;
}

void prune_stale_card_disk_cache(const std::vector<OverviewWorkspace>& workspaces,
                                 const OverviewColors& /*colors*/) {
  auto dir = card_disk_cache_dir();
  if (dir.empty() || !std::filesystem::exists(dir)) return;

  // Build a set of (wsId, contentHash) pairs that are still valid.
  std::set<std::pair<int, std::uint64_t>> valid;
  for (const auto& ws : workspaces) {
    valid.insert({ws.id, compute_ws_content_hash(ws)});
  }

  const std::string vtag = "_v" + std::to_string(kCardDiskCacheVersion) + ".png";
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(dir, ec);
       it != std::filesystem::directory_iterator(); ++it) {
    if (!it->is_regular_file()) continue;
    auto name = it->path().filename().string();
    // Parse: {wsId}_{w}x{h}_{colorKey}_{contentHash}_v{ver}.png
    // Find first underscore to get wsId.
    auto u1 = name.find('_');
    if (u1 == std::string::npos) { std::filesystem::remove(it->path(), ec); continue; }
    int fileWsId = 0;
    try { fileWsId = std::stoi(name.substr(0, u1)); } catch (...) {
      std::filesystem::remove(it->path(), ec); continue;
    }
    // Find last underscore-v tag to get contentHash.
    auto vpos = name.find("_v");
    if (vpos == std::string::npos) { std::filesystem::remove(it->path(), ec); continue; }
    // contentHash is the field just before _vN.png — find the underscore before vpos.
    auto hashEnd = name.rfind('_', vpos - 1);
    if (hashEnd == std::string::npos) { std::filesystem::remove(it->path(), ec); continue; }
    auto hashStart = name.rfind('_', hashEnd - 1);
    if (hashStart == std::string::npos) { std::filesystem::remove(it->path(), ec); continue; }
    std::uint64_t fileHash = 0;
    try { fileHash = std::stoull(name.substr(hashStart + 1, hashEnd - hashStart - 1)); } catch (...) {
      std::filesystem::remove(it->path(), ec); continue;
    }
    if (valid.count({fileWsId, fileHash}) == 0) {
      std::filesystem::remove(it->path(), ec);
    }
  }
}

} // namespace eh::shell::overview
