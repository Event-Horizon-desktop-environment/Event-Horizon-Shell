#include "wallpaper/raster/wallpaper_raster_decode.hpp"

#include <cstring>
#include <utility>
#include <vector>

#include <webp/decode.h>

#include "wallpaper/wallpaper_log.hpp"

#define WUFFS_IMPLEMENTATION
#include "wuffs-v0.4.c"

namespace eh::wallpaper {
namespace {

class RgbaDecodeCallbacks final : public wuffs_aux::DecodeImageCallbacks {
public:
  wuffs_base__pixel_format SelectPixfmt(const wuffs_base__image_config& image_config) override {
    WP_SCOPE();
    
    (void)image_config;
    return wuffs_base__make_pixel_format(WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL);
  }
};

bool is_webp(const std::uint8_t* data, std::size_t size) {
  WP_SCOPE();
    
  return size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' && data[8] == 'W' &&
         data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
}

std::optional<RasterDecoded> decode_webp_buffer(const std::uint8_t* data, std::size_t size,
                                                 std::string* error_message) {
  WP_SCOPE();
    
  int width = 0, height = 0;
  std::uint8_t* rgba = WebPDecodeRGBA(data, size, &width, &height);
  if (rgba == nullptr) {
    if (error_message != nullptr) *error_message = "libwebp: failed to decode WebP image";
    return std::nullopt;
  }
  RasterDecoded decoded;
  decoded.width = width;
  decoded.height = height;
  const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
  decoded.pixels.resize(bytes);
  std::memcpy(decoded.pixels.data(), rgba, bytes);
  WebPFree(rgba);
  return decoded;
}

}

std::optional<RasterDecoded> decode_raster_from_bytes(const std::uint8_t* data, std::size_t size,
                                                     std::string* error_message) {
  WP_SCOPE();
  WP_LOG("size=%zu bytes", size);
  if (data == nullptr || size == 0) {
    if (error_message != nullptr) *error_message = "empty image buffer";
    return std::nullopt;
  }
  if (is_webp(data, size)) return decode_webp_buffer(data, size, error_message);

  auto input = wuffs_aux::sync_io::MemoryInput(data, size);
  auto callbacks = RgbaDecodeCallbacks();
  auto result = wuffs_aux::DecodeImage(callbacks, input);

  if (!result.pixbuf.pixcfg.is_valid()) {
    if (error_message != nullptr) {
      *error_message = result.error_message.empty() ? "wuffs: invalid pixel buffer" : result.error_message;
    }
    return std::nullopt;
  }

  auto plane = result.pixbuf.plane(0);
  if (plane.ptr == nullptr || plane.width == 0 || plane.height == 0) {
    if (error_message != nullptr) *error_message = "decoded image has no pixel data";
    return std::nullopt;
  }

  RasterDecoded decoded;
  decoded.width = static_cast<int>(result.pixbuf.pixcfg.width());
  decoded.height = static_cast<int>(result.pixbuf.pixcfg.height());
  decoded.pixels.resize(plane.width * plane.height);
  std::memcpy(decoded.pixels.data(), plane.ptr, decoded.pixels.size());
  WP_LOG("decoded %dx%d", decoded.width, decoded.height);
  return decoded;
}

}
