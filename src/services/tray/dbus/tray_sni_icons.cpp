#include "services/tray/dbus/tray_sni_icons.hpp"

#include <cstdint>
#include <cstring>

namespace eh::shell::dock::tray::sni {

using Pix = sdbus::Struct<int32_t, int32_t, std::vector<uint8_t>>;

static constexpr int kMaxTrayPixmapSidePx = 128;
static constexpr std::int64_t kMaxTrayPixmapPixels = 128LL * 128LL;

static void destroy_pixmap(cairo_surface_t*& pixSurface, int& pixW, int& pixH, std::vector<uint8_t>& pixData,
                           std::vector<uint8_t>& pixDataCairo) {
   
  if (pixSurface) {
    cairo_surface_destroy(pixSurface);
    pixSurface = nullptr;
  }
  pixW = 0;
  pixH = 0;
  pixData.clear();
  pixDataCairo.clear();
}

static void get_text_prop(sdbus::IProxy& proxy, const char* prop, std::string& out) {
   
  try {
    out = static_cast<std::string>(proxy.getProperty(prop).onInterface("org.kde.StatusNotifierItem"));
  } catch (const sdbus::Error&) {
  }
}

static bool try_nonempty_string_prop(sdbus::IProxy& proxy, const char* prop, std::string& out) {
   
  try {
    out = static_cast<std::string>(proxy.getProperty(prop).onInterface("org.kde.StatusNotifierItem"));
    return !out.empty();
  } catch (const sdbus::Error&) {
    return false;
  }
}

static cairo_surface_t* build_surface_from_rgba_pixels(int w, int h, const std::vector<uint8_t>& argbPremulHostOrder) {
   
  const size_t expected = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  if (w <= 0 || h <= 0 || argbPremulHostOrder.size() < expected) return nullptr;

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return nullptr;
  }
  unsigned char* dst = cairo_image_surface_get_data(surf);
  const int stride = cairo_image_surface_get_stride(surf);
  if (!dst || stride <= 0) {
    cairo_surface_destroy(surf);
    return nullptr;
  }
  const int rowBytes = w * 4;
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<size_t>(y) * static_cast<size_t>(stride),
                argbPremulHostOrder.data() + static_cast<size_t>(y) * static_cast<size_t>(rowBytes),
                static_cast<size_t>(rowBytes));
  }
  cairo_surface_mark_dirty(surf);
  return surf;
}

static bool try_pixmap_prop(sdbus::IProxy& proxy, const char* prop, cairo_surface_t*& pixSurface, int& pixW, int& pixH,
                            std::vector<uint8_t>& pixData, std::vector<uint8_t>& pixDataCairo) {
   
  std::vector<Pix> pixmaps;
  try {
    pixmaps = proxy.getProperty(prop).onInterface("org.kde.StatusNotifierItem").get<std::vector<Pix>>();
  } catch (const sdbus::Error&) {
    return false;
  }
  int best = -1;
  int bestArea = -1;
  for (size_t i = 0; i < pixmaps.size(); i++) {
    const int w = std::get<0>(pixmaps[i]);
    const int h = std::get<1>(pixmaps[i]);
    if (w <= 0 || h <= 0) continue;
    if (w > kMaxTrayPixmapSidePx || h > kMaxTrayPixmapSidePx) continue;
    const std::int64_t px = static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h);
    if (px > kMaxTrayPixmapPixels) continue;
    const int area = w * h;
    if (area > bestArea) {
      bestArea = area;
      best = static_cast<int>(i);
    }
  }
  if (best < 0) return false;

  pixW = std::get<0>(pixmaps[static_cast<size_t>(best)]);
  pixH = std::get<1>(pixmaps[static_cast<size_t>(best)]);
  pixData = std::get<2>(pixmaps[static_cast<size_t>(best)]);

  const size_t expected = static_cast<size_t>(pixW) * static_cast<size_t>(pixH) * 4u;
  if (pixW <= 0 || pixH <= 0 || pixData.size() < expected) {
    pixData.clear();
    pixW = 0;
    pixH = 0;
    return false;
  }

  pixDataCairo.resize(expected);
  for (size_t p = 0; p < expected; p += 4) {
    const uint8_t a = pixData[p + 0];
    const uint8_t r = pixData[p + 1];
    const uint8_t g = pixData[p + 2];
    const uint8_t b = pixData[p + 3];

    const uint8_t pr = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
    const uint8_t pg = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
    const uint8_t pb = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    pixDataCairo[p + 0] = pb;
    pixDataCairo[p + 1] = pg;
    pixDataCairo[p + 2] = pr;
    pixDataCairo[p + 3] = a;
#else
    pixDataCairo[p + 0] = a;
    pixDataCairo[p + 1] = pr;
    pixDataCairo[p + 2] = pg;
    pixDataCairo[p + 3] = pb;
#endif
  }

  pixSurface = build_surface_from_rgba_pixels(pixW, pixH, pixDataCairo);
  if (!pixSurface) {
    pixData.clear();
    pixDataCairo.clear();
    pixW = 0;
    pixH = 0;
    return false;
  }
  pixData.clear();
  pixData.shrink_to_fit();
  pixDataCairo.clear();
  pixDataCairo.shrink_to_fit();
  return true;
}

void dock_update_item_from_proxy(sdbus::IProxy& proxy, std::string& id, std::string& title, std::string& iconName,
                                 int& pixW, int& pixH, std::vector<uint8_t>& pixData,
                                 std::vector<uint8_t>& pixDataCairo, cairo_surface_t*& pixSurface) {
   
  destroy_pixmap(pixSurface, pixW, pixH, pixData, pixDataCairo);
  iconName.clear();

  get_text_prop(proxy, "Id", id);
  get_text_prop(proxy, "Title", title);

  std::string tmp;
  if (!try_nonempty_string_prop(proxy, "IconName", iconName)) {
    if (try_nonempty_string_prop(proxy, "AttentionIconName", tmp))
      iconName = std::move(tmp);
    else if (try_nonempty_string_prop(proxy, "OverlayIconName", tmp))
      iconName = std::move(tmp);
  }

  if (!try_pixmap_prop(proxy, "IconPixmap", pixSurface, pixW, pixH, pixData, pixDataCairo)) {
    if (!iconName.empty()) return;
    (void)try_pixmap_prop(proxy, "AttentionIconPixmap", pixSurface, pixW, pixH, pixData, pixDataCairo);
  }
}

}
