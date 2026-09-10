#include "screenshot/screencopy.hpp"
#include "core/screenshot/logging.hpp"

#include <color-management-v1-client-protocol.h>
#include <ext-image-capture-source-v1-client-protocol.h>
#include <ext-image-copy-capture-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>
#include "core/screenshot/memfd.hpp"

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <wlr-screencopy-unstable-v1-client-protocol.h>

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>
#include <vector>

#ifdef EH_HAVE_LIBDRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../third_party/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#ifdef EH_HAVE_LIBJXL
#include <jxl/encode.h>
#include <jxl/encode_cxx.h>
#include <jxl/types.h>

static inline float hdr_linear_to_pq(float linear, float max_luminance) {
  float N = linear / max_luminance;
  if (N <= 0.0f) return 0.0f;
  constexpr float m1 = 2610.0f / 16384.0f;
  constexpr float m2 = 2523.0f / 4096.0f * 128.0f / 8.0f;
  constexpr float c1 = 3424.0f / 4096.0f;
  constexpr float c2 = 2413.0f / 4096.0f * 32.0f / 8.0f;
  constexpr float c3 = 2392.0f / 4096.0f * 32.0f / 8.0f;
  float Lm1 = std::pow(N, m1);
  return std::pow((c1 + c2 * Lm1) / (1.0f + c3 * Lm1), m2);
}
#endif
#ifdef EH_HAVE_LIBPNG
#include <png.h>
#endif

namespace hpv::sc {
namespace {

struct ColorSpaceInfo {
  bool is_hdr = false;
  bool valid = false;
  uint32_t max_lum = 0;
};

struct ColorSpaceQuery {
  wl_display* display = nullptr;
  wp_color_manager_v1* mgr = nullptr;
  wl_output* output = nullptr;
  wp_color_management_output_v1* cm_out = nullptr;
  wp_image_description_v1* desc = nullptr;
  bool ready = false;
  bool failed = false;
  bool is_hdr = false;
};

static void csq_cm_out_image_desc_changed(void*, wp_color_management_output_v1*) {}

static constexpr wp_color_management_output_v1_listener kCsqCmOutListener = {
    .image_description_changed = csq_cm_out_image_desc_changed,
};

static void csq_desc_failed(void* data, wp_image_description_v1*, uint32_t, const char*) {
  static_cast<ColorSpaceQuery*>(data)->failed = true;
}

static void csq_desc_ready(void* data, wp_image_description_v1*, uint32_t) {
  static_cast<ColorSpaceQuery*>(data)->ready = true;
}

static void csq_desc_ready2(void* data, wp_image_description_v1*, uint32_t, uint32_t) {
  static_cast<ColorSpaceQuery*>(data)->ready = true;
}

static constexpr wp_image_description_v1_listener kCsqDescListener = {
    .failed = csq_desc_failed,
    .ready = csq_desc_ready,
    .ready2 = csq_desc_ready2,
};

struct ColorSpaceInfoCtx {
  bool done = false;
  bool is_hdr = false;
  uint32_t max_lum = 0;
};

static void csi_done(void* data, wp_image_description_info_v1*) {
  static_cast<ColorSpaceInfoCtx*>(data)->done = true;
}
static void csi_icc_file(void*, wp_image_description_info_v1*, int32_t, uint32_t) {}
static void csi_primaries(void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t, int32_t,
                          int32_t, int32_t, int32_t) {}
static void csi_primaries_named(void*, wp_image_description_info_v1*, uint32_t) {}
static void csi_tf_power(void*, wp_image_description_info_v1*, uint32_t) {}
static void csi_tf_named(void* data, wp_image_description_info_v1*, uint32_t tf) {
   
  auto* c = static_cast<ColorSpaceInfoCtx*>(data);
  if (tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ ||
      tf == WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG) {
    c->is_hdr = true;
  }
}
static void csi_luminances(void* data, wp_image_description_info_v1*, uint32_t, uint32_t max_lum, uint32_t) {
  static_cast<ColorSpaceInfoCtx*>(data)->max_lum = max_lum;
}
static void csi_target_primaries(void*, wp_image_description_info_v1*, int32_t, int32_t, int32_t, int32_t,
                                 int32_t, int32_t, int32_t, int32_t) {}
static void csi_target_luminance(void*, wp_image_description_info_v1*, uint32_t, uint32_t) {}
static void csi_target_max_cll(void*, wp_image_description_info_v1*, uint32_t) {}
static void csi_target_max_fall(void*, wp_image_description_info_v1*, uint32_t) {}

static constexpr wp_image_description_info_v1_listener kCsqInfoListener = {
    .done = csi_done,
    .icc_file = csi_icc_file,
    .primaries = csi_primaries,
    .primaries_named = csi_primaries_named,
    .tf_power = csi_tf_power,
    .tf_named = csi_tf_named,
    .luminances = csi_luminances,
    .target_primaries = csi_target_primaries,
    .target_luminance = csi_target_luminance,
    .target_max_cll = csi_target_max_cll,
    .target_max_fall = csi_target_max_fall,
};

static ColorSpaceInfo query_output_color_space(wl_display* display, wp_color_manager_v1* mgr,
                                               wl_output* output) {
   
  if (!display || !mgr || !output) return {};
  ColorSpaceQuery q{};
  q.display = display;
  q.mgr = mgr;
  q.output = output;

  q.cm_out = wp_color_manager_v1_get_output(mgr, output);
  if (!q.cm_out) return {};
  wp_color_management_output_v1_add_listener(q.cm_out, &kCsqCmOutListener, &q);

  q.desc = wp_color_management_output_v1_get_image_description(q.cm_out);
  if (!q.desc) {
    wp_color_management_output_v1_destroy(q.cm_out);
    return {};
  }
  wp_image_description_v1_add_listener(q.desc, &kCsqDescListener, &q);

  constexpr int kMax = 65536;
  wl_display_flush(display);
  for (int i = 0; i < kMax; ++i) {
    if (q.ready || q.failed) break;
    if (wl_display_dispatch(display) < 0) break;
  }
  if (q.failed || !q.ready) {
    wp_image_description_v1_destroy(q.desc);
    wp_color_management_output_v1_destroy(q.cm_out);
    return {};
  }

  ColorSpaceInfoCtx info{};
  wp_image_description_info_v1* inf = wp_image_description_v1_get_information(q.desc);
  if (!inf) {
    wp_image_description_v1_destroy(q.desc);
    wp_color_management_output_v1_destroy(q.cm_out);
    return {};
  }
  wp_image_description_info_v1_add_listener(inf, &kCsqInfoListener, &info);

  for (int i = 0; i < kMax; ++i) {
    if (info.done) break;
    wl_display_dispatch(display);
  }

  wp_image_description_v1_destroy(q.desc);
  wp_color_management_output_v1_destroy(q.cm_out);

  return {.is_hdr = info.is_hdr, .valid = info.done, .max_lum = info.is_hdr ? info.max_lum : 0u};
}

struct CaptureCtx {
  wl_display* display = nullptr;
  zwlr_screencopy_frame_v1* frame = nullptr;
  wl_buffer* wl_buf = nullptr;
  void* map = nullptr;
  size_t map_size = 0;
  int fd = -1;
  uint32_t fmt = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  bool have_buffer = false;
  bool failed = false;
  bool phase2_done = false;
  bool phase2_success = false;
  bool y_invert = false;
  bool raw_mode = false;
  std::string path_utf8;
  bool dmabuf_offered = false;
  uint32_t dmabuf_fmt = 0;
  bool buffer_done = false;
  bool using_dmabuf = false;
  int drm_fd = -1;
  uint32_t drm_handle = 0;
  uint32_t dumb_pitch = 0;
  size_t dumb_size = 0;
};

static void buf_release(void*, wl_buffer*) {}

static constexpr wl_buffer_listener kBufRelease = {.release = buf_release};

static bool unpack_pixel(const uint8_t* src, uint32_t fmt, unsigned char* rgba) {
  
  unsigned char r = 0, g = 0, b = 0, a = 255;
  switch (fmt) {
  case WL_SHM_FORMAT_ARGB8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    r = static_cast<unsigned char>((p >> 16) & 0xffu);
    a = static_cast<unsigned char>((p >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XRGB8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    r = static_cast<unsigned char>((p >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XBGR8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    b = static_cast<unsigned char>((p >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_ABGR8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    b = static_cast<unsigned char>((p >> 16) & 0xffu);
    a = static_cast<unsigned char>((p >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_RGBA8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    b = static_cast<unsigned char>((p >> 16) & 0xffu);
    a = static_cast<unsigned char>((p >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_BGRA8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    r = static_cast<unsigned char>((p >> 16) & 0xffu);
    a = static_cast<unsigned char>((p >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_RGBX8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    b = static_cast<unsigned char>((p >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_BGRX8888: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(p & 0xffu);
    g = static_cast<unsigned char>((p >> 8) & 0xffu);
    r = static_cast<unsigned char>((p >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XRGB2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(((p       ) & 0x3ffu) * 255 / 1023);
    g = static_cast<unsigned char>(((p >> 10 ) & 0x3ffu) * 255 / 1023);
    r = static_cast<unsigned char>(((p >> 20 ) & 0x3ffu) * 255 / 1023);
    break;
  }
  case WL_SHM_FORMAT_XBGR2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(((p       ) & 0x3ffu) * 255 / 1023);
    g = static_cast<unsigned char>(((p >> 10 ) & 0x3ffu) * 255 / 1023);
    b = static_cast<unsigned char>(((p >> 20 ) & 0x3ffu) * 255 / 1023);
    break;
  }
  case WL_SHM_FORMAT_ARGB2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<unsigned char>(((p       ) & 0x3ffu) * 255 / 1023);
    g = static_cast<unsigned char>(((p >> 10 ) & 0x3ffu) * 255 / 1023);
    r = static_cast<unsigned char>(((p >> 20 ) & 0x3ffu) * 255 / 1023);
    a = static_cast<unsigned char>(((p >> 30 ) & 0x003u) * 255 / 3);
    break;
  }
  case WL_SHM_FORMAT_ABGR2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<unsigned char>(((p       ) & 0x3ffu) * 255 / 1023);
    g = static_cast<unsigned char>(((p >> 10 ) & 0x3ffu) * 255 / 1023);
    b = static_cast<unsigned char>(((p >> 20 ) & 0x3ffu) * 255 / 1023);
    a = static_cast<unsigned char>(((p >> 30 ) & 0x003u) * 255 / 3);
    break;
  }
  default:
    return false;
  }
  rgba[0] = r;
  rgba[1] = g;
  rgba[2] = b;
  rgba[3] = a;
  return true;
}

static bool fmt_is_half_float(uint32_t fmt) {
  return fmt == WL_SHM_FORMAT_ABGR16161616F || fmt == WL_SHM_FORMAT_XBGR16161616F
      || fmt == WL_SHM_FORMAT_ARGB16161616F || fmt == WL_SHM_FORMAT_XRGB16161616F;
}

static bool fmt_is_16bit_int(uint32_t fmt) {
  return fmt == WL_SHM_FORMAT_ABGR16161616 || fmt == WL_SHM_FORMAT_XBGR16161616
      || fmt == WL_SHM_FORMAT_ARGB16161616 || fmt == WL_SHM_FORMAT_XRGB16161616;
}

static bool fmt_is_high_depth(uint32_t fmt) {
  if (fmt_is_half_float(fmt)) return true;
  if (fmt_is_16bit_int(fmt)) return true;
  switch (fmt) {
  case WL_SHM_FORMAT_XRGB2101010:
  case WL_SHM_FORMAT_XBGR2101010:
  case WL_SHM_FORMAT_ARGB2101010:
  case WL_SHM_FORMAT_ABGR2101010:
    return true;
  default:
    return false;
  }
}

static void read_high_depth_pixel(const uint8_t* src, uint32_t fmt, float& r, float& g, float& b) {
  r = g = b = 0.0f;
  switch (fmt) {
  case WL_SHM_FORMAT_XRGB2101010:
  case WL_SHM_FORMAT_ARGB2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    b = static_cast<float>((p       ) & 0x3ffu) / 1023.0f;
    g = static_cast<float>((p >> 10 ) & 0x3ffu) / 1023.0f;
    r = static_cast<float>((p >> 20 ) & 0x3ffu) / 1023.0f;
    break;
  }
  case WL_SHM_FORMAT_XBGR2101010:
  case WL_SHM_FORMAT_ABGR2101010: {
    uint32_t p; std::memcpy(&p, src, 4);
    r = static_cast<float>((p       ) & 0x3ffu) / 1023.0f;
    g = static_cast<float>((p >> 10 ) & 0x3ffu) / 1023.0f;
    b = static_cast<float>((p >> 20 ) & 0x3ffu) / 1023.0f;
    break;
  }
  case WL_SHM_FORMAT_XBGR16161616:
  case WL_SHM_FORMAT_ABGR16161616: {
    uint16_t comp[4]; std::memcpy(comp, src, 8);
    r = comp[0] / 65535.0f;
    g = comp[1] / 65535.0f;
    b = comp[2] / 65535.0f;
    break;
  }
  case WL_SHM_FORMAT_XRGB16161616:
  case WL_SHM_FORMAT_ARGB16161616: {
    uint16_t comp[4]; std::memcpy(comp, src, 8);
    b = comp[0] / 65535.0f;
    g = comp[1] / 65535.0f;
    r = comp[2] / 65535.0f;
    break;
  }
  default:
    break;
  }
}

static float half_to_float(uint16_t h) {
  uint32_t sign = (h >> 15) & 1;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;
  if (exp == 0) {
    return (mant == 0) ? 0.0f : (sign ? -1.0f : 1.0f) * (mant / 16777216.0f);
  }
  if (exp == 31) {
    return (mant == 0) ? (sign ? -1.0f / 0.0f : 1.0f / 0.0f)
                       : (sign ? -1.0f / 0.0f : 1.0f / 0.0f);
  }
  uint32_t f32 = (sign << 31) | ((exp + 112) << 23) | (mant << 13);
  float result;
  std::memcpy(&result, &f32, 4);
  return result;
}

static bool read_half_float_row(const uint8_t* src, float* dst_rgb, int w, uint32_t fmt) {
  int src_stride = w * 8;
  for (int x = 0; x < w; ++x) {
    uint16_t comp[4];
    std::memcpy(comp, src + x * 8, 8);
    float r, g, b, a = 1.0f;
    switch (fmt) {
    case WL_SHM_FORMAT_ABGR16161616F:
      r = half_to_float(comp[0]);
      g = half_to_float(comp[1]);
      b = half_to_float(comp[2]);
      a = half_to_float(comp[3]);
      break;
    case WL_SHM_FORMAT_XBGR16161616F:
      r = half_to_float(comp[0]);
      g = half_to_float(comp[1]);
      b = half_to_float(comp[2]);
      break;
    case WL_SHM_FORMAT_ARGB16161616F:
      b = half_to_float(comp[0]);
      g = half_to_float(comp[1]);
      r = half_to_float(comp[2]);
      a = half_to_float(comp[3]);
      break;
    case WL_SHM_FORMAT_XRGB16161616F:
      b = half_to_float(comp[0]);
      g = half_to_float(comp[1]);
      r = half_to_float(comp[2]);
      break;
    default:
      return false;
    }
    dst_rgb[x * 3 + 0] = r;
    dst_rgb[x * 3 + 1] = g;
    dst_rgb[x * 3 + 2] = b;
    (void)a;
    (void)src_stride;
  }
  return true;
}

static bool write_png_rgba(const uint8_t* src, int w, int h, int src_stride, uint32_t fmt, bool yinvert,
                           const char* path) {
  
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
  for (int y = 0; y < h; ++y) {
    const int sy = yinvert ? (h - 1 - y) : y;
    const auto* row = reinterpret_cast<const uint8_t*>(src + static_cast<size_t>(sy) * static_cast<size_t>(src_stride));
    unsigned char* dst = rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
    for (int x = 0; x < w; ++x) {
      if (!unpack_pixel(row + x * 4, fmt, dst + x * 4)) return false;
    }
  }
  return stbi_write_png(path, w, h, 4, rgba.data(), w * 4) != 0;
}

}

static void aces_tone_map(float& r, float& g, float& b) {
  float rr = r, gg = g, bb = b;
  rr = (rr * (2.51f * rr + 0.03f)) / (rr * (2.43f * rr + 0.59f) + 0.14f);
  gg = (gg * (2.51f * gg + 0.03f)) / (gg * (2.43f * gg + 0.59f) + 0.14f);
  bb = (bb * (2.51f * bb + 0.03f)) / (bb * (2.43f * bb + 0.59f) + 0.14f);
  float maxc = (std::max)({rr, gg, bb});
  if (maxc > 1.0f) { float inv = 1.0f / maxc; rr *= inv; gg *= inv; bb *= inv; }
  r = rr; g = gg; b = bb;
}

static float linear_to_sdr(float c) {
  c = (std::max)(c, 0.0f);
  return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
}

static float pq_to_linear(float pq_val, float max_lum) {
  if (pq_val <= 0.0f) return 0.0f;
  float m1 = 2610.0f / 16384.0f;
  float m2 = 2523.0f / 4096.0f * 128.0f / 256.0f;
  float c1 = 3424.0f / 4096.0f;
  float c2 = 2413.0f / 4096.0f * 32.0f / 256.0f;
  float c3 = 2392.0f / 4096.0f * 32.0f / 256.0f;
  float Lp = powf(pq_val, 1.0f / m2);
  float num = (std::max)(Lp - c1, 0.0f);
  float den = (std::max)(c2 - c3 * Lp, 0.001f);
  float linear_abs = powf(num / den, 1.0f / m1);
  float max_abs = static_cast<float>(max_lum) / 10000.0f;
  return (max_abs > 0.0f) ? (linear_abs / max_abs) : 0.0f;
}
#ifdef EH_HAVE_LIBJXL
bool write_jxl_hdr(const float* rgb, int w, int h,
                           const char* path, uint32_t max_lum) {
  
  auto enc = JxlEncoderMake(nullptr);
  if (!enc) return false;

  JxlBasicInfo info;
  JxlEncoderInitBasicInfo(&info);
  info.xsize = static_cast<uint32_t>(w);
  info.ysize = static_cast<uint32_t>(h);
  info.bits_per_sample = 32;
  info.exponent_bits_per_sample = 8;
  info.num_color_channels = 3;
  info.num_extra_channels = 0;
  info.alpha_bits = 0;
  info.alpha_exponent_bits = 0;
  info.uses_original_profile = JXL_TRUE;
  info.intensity_target = (max_lum > 0) ? static_cast<float>(max_lum) : 10000.0f;

  if (JxlEncoderSetBasicInfo(enc.get(), &info) != JXL_ENC_SUCCESS) return false;

  JxlColorEncoding color{};
  color.color_space = JXL_COLOR_SPACE_RGB;
  color.white_point = JXL_WHITE_POINT_D65;
  color.primaries = JXL_PRIMARIES_2100;
  color.transfer_function = JXL_TRANSFER_FUNCTION_PQ;
  color.rendering_intent = JXL_RENDERING_INTENT_PERCEPTUAL;

  if (JxlEncoderSetColorEncoding(enc.get(), &color) != JXL_ENC_SUCCESS) return false;

  std::vector<float> pq(static_cast<size_t>(w) * h * 3);
  float ml = (max_lum > 0) ? static_cast<float>(max_lum) : 10000.0f;
  for (size_t i = 0; i < static_cast<size_t>(w) * h * 3; i += 3) {
    pq[i + 0] = hdr_linear_to_pq(rgb[i + 0], ml);
    pq[i + 1] = hdr_linear_to_pq(rgb[i + 1], ml);
    pq[i + 2] = hdr_linear_to_pq(rgb[i + 2], ml);
  }

  auto* frame = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
  if (!frame) return false;

  JxlPixelFormat px_fmt{};
  px_fmt.num_channels = 3;
  px_fmt.data_type = JXL_TYPE_FLOAT;
  px_fmt.endianness = JXL_NATIVE_ENDIAN;
  px_fmt.align = 0;

  if (JxlEncoderAddImageFrame(frame, &px_fmt, pq.data(), pq.size() * sizeof(float)) != JXL_ENC_SUCCESS) {
    return false;
  }
  JxlEncoderCloseFrames(enc.get());
  JxlEncoderCloseInput(enc.get());

  std::vector<uint8_t> compressed(65536);
  uint8_t* next_out = compressed.data();
  size_t avail_out = compressed.size();
  while (true) {
    auto status = JxlEncoderProcessOutput(enc.get(), &next_out, &avail_out);
    if (status == JXL_ENC_SUCCESS) break;
    if (status != JXL_ENC_NEED_MORE_OUTPUT) return false;
    const size_t offset = static_cast<size_t>(next_out - compressed.data());
    compressed.resize(compressed.size() * 2);
    next_out = compressed.data() + offset;
    avail_out = compressed.size() - offset;
  }

  const size_t final_size = static_cast<size_t>(next_out - compressed.data());
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  const bool ok = fwrite(compressed.data(), 1, final_size, f) == final_size;
  fclose(f);
  return ok;
}
#endif

bool write_png_from_linear(const float* rgb, int w, int h, const char* path) {
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    unsigned char* dst = rgba.data() + static_cast<size_t>(y) * w * 4;
    const float* src = rgb + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x) {
      float r = src[x * 3 + 0], g = src[x * 3 + 1], b = src[x * 3 + 2];
      aces_tone_map(r, g, b);
      dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
      dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
      dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
      dst[x * 4 + 3] = 255;
    }
  }
  return stbi_write_png(path, w, h, 4, rgba.data(), w * 4) != 0;
}

#ifdef EH_HAVE_LIBPNG
bool write_png16_from_linear(const float* rgb, int w, int h, const char* path) {
  std::vector<uint16_t> pixels(static_cast<size_t>(w) * h * 3);
  for (int y = 0; y < h; ++y) {
    const float* src = rgb + static_cast<size_t>(y) * w * 3;
    uint16_t* dst = pixels.data() + static_cast<size_t>(y) * w * 3;
    for (int x = 0; x < w; ++x) {
      float r = src[x * 3 + 0], g = src[x * 3 + 1], b = src[x * 3 + 2];
      aces_tone_map(r, g, b);
      dst[x * 3 + 0] = static_cast<uint16_t>(std::clamp(linear_to_sdr(r), 0.0f, 1.0f) * 65535.0f + 0.5f);
      dst[x * 3 + 1] = static_cast<uint16_t>(std::clamp(linear_to_sdr(g), 0.0f, 1.0f) * 65535.0f + 0.5f);
      dst[x * 3 + 2] = static_cast<uint16_t>(std::clamp(linear_to_sdr(b), 0.0f, 1.0f) * 65535.0f + 0.5f);
    }
  }

  FILE* f = fopen(path, "wb");
  if (!f) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) { fclose(f); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, nullptr); fclose(f); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(f);
    return false;
  }

  png_init_io(png, f);

  png_set_IHDR(png, info, static_cast<uint32_t>(w), static_cast<uint32_t>(h), 16,
               PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  std::vector<png_bytep> rows(static_cast<size_t>(h));
  for (int y = 0; y < h; ++y) {
    rows[static_cast<size_t>(y)] = reinterpret_cast<png_bytep>(
        pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 3);
  }
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);

  png_destroy_write_struct(&png, &info);
  fclose(f);
  return true;
}
#endif

bool write_hdr_from_linear(const float* rgb, int w, int h,
                                   const char* path, uint32_t max_lum) {
#ifdef EH_HAVE_LIBJXL
  return write_jxl_hdr(rgb, w, h, path, max_lum);
#else
  (void)max_lum;
  return stbi_write_hdr(path, w, h, 3, rgb) != 0;
#endif
}

static void frame_buffer(void* data, zwlr_screencopy_frame_v1*, uint32_t format, uint32_t w, uint32_t h,
                         uint32_t stride) {
  SC_LOG("frame_buffer fmt=0x%x %ux%u stride=%u", format, w, h, stride);
  auto* c = static_cast<CaptureCtx*>(data);
  c->fmt = format;
  c->width = w;
  c->height = h;
  c->stride = stride;
  c->have_buffer = true;
}

static void frame_flags(void* data, zwlr_screencopy_frame_v1*, uint32_t flags) {
  SC_LOG("frame_flags flags=0x%x y_invert=%d", flags, (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0);
  auto* c = static_cast<CaptureCtx*>(data);
  c->y_invert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void frame_ready(void* data, zwlr_screencopy_frame_v1* fr, uint32_t  , uint32_t  ,
                         uint32_t  ) {
   SC_LOG("frame_ready raw_mode=%d path='%s'", static_cast<CaptureCtx*>(data)->raw_mode, static_cast<CaptureCtx*>(data)->path_utf8.c_str());
   auto* c = static_cast<CaptureCtx*>(data);
   (void)fr;
   c->phase2_done = true;
   if (!c->map || c->width == 0 || c->height == 0) {
     c->phase2_success = false;
     return;
   }
    if (!c->raw_mode) {
      if (fmt_is_high_depth(c->fmt)) {
        int w = static_cast<int>(c->width);
        int h = static_cast<int>(c->height);
        int stride = static_cast<int>(c->stride);
        int hd_bpp = fmt_is_16bit_int(c->fmt) ? 8 : 4;
        const auto* map = static_cast<const uint8_t*>(c->map);
        std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
        if (fmt_is_half_float(c->fmt)) {
          c->phase2_success = true;
          for (int y = 0; y < h && c->phase2_success; ++y) {
            int sy = c->y_invert ? (h - 1 - y) : y;
            const auto* row = map + static_cast<size_t>(sy) * stride;
            if (!read_half_float_row(row, rgb.data() + static_cast<size_t>(y) * w * 3, w, c->fmt)) {
              c->phase2_success = false;
            }
          }
        } else {
          for (int y = 0; y < h; ++y) {
            int sy = c->y_invert ? (h - 1 - y) : y;
            const auto* row = map + static_cast<size_t>(sy) * stride;
            float* dst = rgb.data() + static_cast<size_t>(y) * w * 3;
            for (int x = 0; x < w; ++x) {
              read_high_depth_pixel(row + x * hd_bpp, c->fmt, dst[x * 3 + 0], dst[x * 3 + 1], dst[x * 3 + 2]);
            }
          }
        }
        if (c->phase2_success) {
#ifdef EH_HAVE_LIBPNG
          c->phase2_success = write_png16_from_linear(rgb.data(), w, h, c->path_utf8.c_str());
#else
          c->phase2_success = write_png_from_linear(rgb.data(), w, h, c->path_utf8.c_str());
#endif
        }
      } else {
        c->phase2_success =
            write_png_rgba(static_cast<const uint8_t*>(c->map), static_cast<int>(c->width), static_cast<int>(c->height),
                           static_cast<int>(c->stride), c->fmt, c->y_invert, c->path_utf8.c_str());
      }
    } else {
     c->phase2_success = true;
   }
 }

static void frame_failed(void* data, zwlr_screencopy_frame_v1*) {
  SC_LOG("frame_failed");
  auto* c = static_cast<CaptureCtx*>(data);
  c->phase2_done = true;
  c->phase2_success = false;
}

static void frame_damage(void*, zwlr_screencopy_frame_v1*, uint32_t, uint32_t, uint32_t, uint32_t) {}

static void frame_linux_dmabuf(void* data, zwlr_screencopy_frame_v1*, uint32_t format, uint32_t w,
                                uint32_t h) {
  SC_LOG("frame_linux_dmabuf fmt=0x%x %ux%u", format, w, h);
  auto* c = static_cast<CaptureCtx*>(data);
  c->dmabuf_offered = true;
  c->dmabuf_fmt = format;
  if (!c->have_buffer) {
    c->fmt = format;
    c->width = w;
    c->height = h;
  }
}

static void frame_buffer_done(void* data, zwlr_screencopy_frame_v1*) {
  SC_LOG("frame_buffer_done");
  static_cast<CaptureCtx*>(data)->buffer_done = true;
}

static constexpr zwlr_screencopy_frame_v1_listener kFrameListener = {
    .buffer = frame_buffer,
    .flags = frame_flags,
    .ready = frame_ready,
    .failed = frame_failed,
    .damage = frame_damage,
    .linux_dmabuf = frame_linux_dmabuf,
    .buffer_done = frame_buffer_done,
};

static int bpp_from_fourcc(uint32_t fmt) {
  switch (fmt) {
  case WL_SHM_FORMAT_XRGB8888:
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XBGR8888:
  case WL_SHM_FORMAT_ABGR8888:
  case WL_SHM_FORMAT_RGBA8888:
  case WL_SHM_FORMAT_BGRA8888:
  case WL_SHM_FORMAT_RGBX8888:
  case WL_SHM_FORMAT_BGRX8888:
  case WL_SHM_FORMAT_XRGB2101010:
  case WL_SHM_FORMAT_XBGR2101010:
  case WL_SHM_FORMAT_ARGB2101010:
  case WL_SHM_FORMAT_ABGR2101010:
    return 32;
  case WL_SHM_FORMAT_ABGR16161616:
  case WL_SHM_FORMAT_XBGR16161616:
  case WL_SHM_FORMAT_ARGB16161616:
  case WL_SHM_FORMAT_XRGB16161616:
  case WL_SHM_FORMAT_ABGR16161616F:
  case WL_SHM_FORMAT_XBGR16161616F:
  case WL_SHM_FORMAT_ARGB16161616F:
  case WL_SHM_FORMAT_XRGB16161616F:
    return 64;
  default:
    return 0;
  }
}

#ifdef EH_HAVE_LIBDRM
static bool create_dmabuf_buffer(CaptureCtx& ctx, zwp_linux_dmabuf_v1* linux_dmabuf) {
  int bpp = bpp_from_fourcc(ctx.fmt);
  if (bpp <= 0) return false;

  for (int minor = 128; minor < 144; ++minor) {
    ctx.drm_fd = drmOpenRender(minor);
    if (ctx.drm_fd >= 0) break;
  }
  if (ctx.drm_fd < 0)
    ctx.drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
  if (ctx.drm_fd < 0) return false;

  struct drm_mode_create_dumb create = {};
  create.width = ctx.width;
  create.height = ctx.height;
  create.bpp = bpp;
  if (drmIoctl(ctx.drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) {
    close(ctx.drm_fd); ctx.drm_fd = -1; return false;
  }
  ctx.drm_handle = create.handle;
  ctx.dumb_size = create.size;
  ctx.dumb_pitch = create.pitch;
  ctx.stride = create.pitch;

  int prime_fd = -1;
  if (drmPrimeHandleToFD(ctx.drm_fd, ctx.drm_handle, 0, &prime_fd) < 0) {
    close(ctx.drm_fd); ctx.drm_fd = -1; return false;
  }

  auto* params = zwp_linux_dmabuf_v1_create_params(linux_dmabuf);
  zwp_linux_buffer_params_v1_add(params, prime_fd, 0, 0, ctx.dumb_pitch, 0, 0);
  ctx.wl_buf = zwp_linux_buffer_params_v1_create_immed(params, ctx.width, ctx.height, ctx.fmt, 0);
  zwp_linux_buffer_params_v1_destroy(params);
  close(prime_fd);

  if (!ctx.wl_buf) {
    close(ctx.drm_fd); ctx.drm_fd = -1; return false;
  }

  ctx.map_size = ctx.dumb_size;
  ctx.using_dmabuf = true;
  return true;
}

static bool mmap_dmabuf_buffer(CaptureCtx& ctx) {
  struct drm_mode_map_dumb map = {};
  map.handle = ctx.drm_handle;
  if (drmIoctl(ctx.drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) return false;
  ctx.map = mmap(nullptr, ctx.dumb_size, PROT_READ, MAP_SHARED, ctx.drm_fd, map.offset);
  return ctx.map != MAP_FAILED;
}
#endif

static bool dispatch_until(CaptureCtx& c, bool (*pred)(const CaptureCtx&)) {
  SC_LOG("dispatch_until");
  constexpr int kMax = 65536;
  for (int i = 0; i < kMax; ++i) {
    if (pred(c)) return true;
    const int r = wl_display_dispatch(c.display);
    if (r < 0) return false;
  }
  return false;
}

namespace { // second anonymous-ns block

static bool have_buffer_or_fail(const CaptureCtx& c) { return c.have_buffer || c.buffer_done || c.failed; }

static bool phase2_done(const CaptureCtx& c) { return c.phase2_done; }

static bool screencopy_to_png_impl(wl_display* display, zwlr_screencopy_manager_v1* screencopy_mgr, wl_shm* shm,
                                    wl_output* output, bool overlay_cursor, std::string_view png_path, bool use_region,
                                    int32_t region_x, int32_t region_y, int32_t region_w, int32_t region_h) {
  SC_LOG("screencopy_to_png_impl path='%.*s' use_region=%d region=%d,%d %dx%d",
         (int)png_path.size(), png_path.data(), use_region, region_x, region_y, region_w, region_h);
  if (!display || !screencopy_mgr || !shm || !output || png_path.empty()) { SC_LOG("screencopy_to_png_impl: invalid args"); return false; }
  if (use_region && (region_w <= 0 || region_h <= 0)) { SC_LOG("screencopy_to_png_impl: invalid region"); return false; }

  CaptureCtx ctx{};
  ctx.display = display;
  ctx.path_utf8 = std::string(png_path);

  zwlr_screencopy_frame_v1* fr = nullptr;
  if (use_region) {
    fr = zwlr_screencopy_manager_v1_capture_output_region(screencopy_mgr, overlay_cursor ? 1 : 0, output, region_x,
                                                           region_y, region_w, region_h);
  } else {
    fr = zwlr_screencopy_manager_v1_capture_output(screencopy_mgr, overlay_cursor ? 1 : 0, output);
  }
  if (!fr) { SC_LOG("screencopy_to_png_impl: capture_output[_region] returned null"); return false; }
  ctx.frame = fr;
  zwlr_screencopy_frame_v1_add_listener(fr, &kFrameListener, &ctx);

  if (!dispatch_until(ctx, have_buffer_or_fail)) {
    SC_LOG("screencopy_to_png_impl: phase1 dispatch failed (failed=%d have_buf=%d w=%d h=%d stride=%d)",
             ctx.failed, ctx.have_buffer, ctx.width, ctx.height, ctx.stride);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }
  if (ctx.failed || !ctx.have_buffer || ctx.width == 0 || ctx.height == 0 || ctx.stride < ctx.width * 4u) {
    SC_LOG("screencopy_to_png_impl: phase1 bad (failed=%d have_buf=%d w=%d h=%d stride=%d)",
             ctx.failed, ctx.have_buffer, ctx.width, ctx.height, ctx.stride);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }

  ctx.map_size = static_cast<size_t>(ctx.stride) * static_cast<size_t>(ctx.height);
  ctx.fd = memfd_create_compat("eh-screencopy", 0);
  if (ctx.fd < 0) {
    char tmpl[] = "/tmp/eh-sc-XXXXXX";
    ctx.fd = mkstemp(tmpl);
    if (ctx.fd >= 0) unlink(tmpl);
  }
  if (ctx.fd < 0) {
    SC_LOG("screencopy_to_png_impl: memfd/mkstemp failed");
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }
  if (ftruncate(ctx.fd, static_cast<off_t>(ctx.map_size)) != 0) {
    SC_LOG("screencopy_to_png_impl: ftruncate failed errno=%d", errno);
    close(ctx.fd);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }
  ctx.map = mmap(nullptr, ctx.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd, 0);
  if (ctx.map == MAP_FAILED) {
    SC_LOG("screencopy_to_png_impl: mmap failed errno=%d", errno);
    ctx.map = nullptr;
    close(ctx.fd);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, ctx.fd, static_cast<int>(ctx.map_size));
  if (!pool) {
    SC_LOG("screencopy_to_png_impl: wl_shm_create_pool failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }
  wl_buffer* buf = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(ctx.width), static_cast<int>(ctx.height),
                                             static_cast<int>(ctx.stride), static_cast<int>(ctx.fmt));
  wl_shm_pool_destroy(pool);
  if (!buf) {
    SC_LOG("screencopy_to_png_impl: wl_shm_pool_create_buffer failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }
  ctx.wl_buf = buf;
  wl_buffer_add_listener(buf, &kBufRelease, nullptr);

  zwlr_screencopy_frame_v1_copy(fr, buf);
  wl_display_flush(display);

  ctx.phase2_done = false;
  ctx.phase2_success = false;
  if (!dispatch_until(ctx, phase2_done)) {
    SC_LOG("screencopy_to_png_impl: phase2 dispatch failed");
    wl_buffer_destroy(buf);
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    zwlr_screencopy_frame_v1_destroy(fr);
    return false;
  }

  const bool ok = ctx.phase2_success;
  zwlr_screencopy_frame_v1_destroy(fr);
  ctx.frame = nullptr;
  wl_buffer_destroy(buf);
  ctx.wl_buf = nullptr;
  if (ctx.map) {
    munmap(ctx.map, ctx.map_size);
    ctx.map = nullptr;
  }
  if (ctx.fd >= 0) {
    close(ctx.fd);
    ctx.fd = -1;
  }
  SC_LOG("screencopy_to_png_impl: returning %d", ok);
  return ok;
}

}

bool screencopy_output_to_png(wl_display* display, zwlr_screencopy_manager_v1* screencopy_mgr, wl_shm* shm,
                              wl_output* output, bool overlay_cursor, std::string_view png_path) {
  SC_LOG("screencopy_output_to_png path='%.*s'", (int)png_path.size(), png_path.data());
  return screencopy_to_png_impl(display, screencopy_mgr, shm, output, overlay_cursor, png_path, false, 0, 0, 0, 0);
}

bool screencopy_output_region_to_png(wl_display* display, zwlr_screencopy_manager_v1* screencopy_mgr, wl_shm* shm,
                                     wl_output* output, int32_t region_x, int32_t region_y, int32_t region_w,
                                     int32_t region_h, bool overlay_cursor, std::string_view png_path) {
  SC_LOG("screencopy_output_region_to_png region=%d,%d %dx%d path='%.*s'", region_x, region_y, region_w, region_h,
         (int)png_path.size(), png_path.data());
  return screencopy_to_png_impl(display, screencopy_mgr, shm, output, overlay_cursor, png_path, true, region_x,
                                region_y, region_w, region_h);
}

bool batch_capture_outputs(
    wl_display* display,
    zwlr_screencopy_manager_v1* screencopy_mgr,
    wl_shm* shm,
    std::vector<BatchedCaptureOutput>& outputs,
    bool overlay_cursor,
    wp_color_manager_v1* color_mgr,
    zwp_linux_dmabuf_v1* linux_dmabuf)
{
  SC_LOG("batch_capture_outputs outputs=%zu", outputs.size());
  if (outputs.empty() || !display || !screencopy_mgr || !shm) { SC_LOG("batch_capture_outputs: invalid args"); return false; }

  std::vector<CaptureCtx> ctxs(outputs.size());

  for (size_t i = 0; i < outputs.size(); ++i) {
    auto& ctx = ctxs[i];
    ctx.display = display;
    ctx.raw_mode = true;

    if (!outputs[i].output) { SC_LOG("batch_capture_outputs[%zu]: null output", i); ctx.failed = true; continue; }

    auto* fr = zwlr_screencopy_manager_v1_capture_output(
        screencopy_mgr, overlay_cursor ? 1 : 0, outputs[i].output);
    if (!fr) { SC_LOG("batch_capture_outputs[%zu]: capture_output returned null", i); ctx.failed = true; continue; }
    ctx.frame = fr;
    zwlr_screencopy_frame_v1_add_listener(fr, &kFrameListener, &ctx);
  }

  {
    constexpr int kMax = 65536;
    for (int iter = 0; iter < kMax; ++iter) {
      bool all_done = true;
      for (auto& c : ctxs) {
        if (c.failed) continue;
        if (c.frame && !c.buffer_done) { all_done = false; break; }
      }
      if (all_done) break;

      bool any_alive = false;
      for (auto& c : ctxs) { if (c.frame && !c.failed) { any_alive = true; break; } }
      if (!any_alive) { SC_LOG("batch_capture_outputs phase1: no alive frames"); break; }

      if (wl_display_dispatch(display) < 0) { SC_LOG("batch_capture_outputs phase1: dispatch error"); goto batch_cleanup; }
      for (size_t i = 0; i < ctxs.size(); ++i) {
        if (ctxs[i].dmabuf_offered && !ctxs[i].failed) {
          SC_LOG("batch phase2: i=%zu DMABUF offered fmt=0x%x", i, ctxs[i].fmt);
        }
      }
    }
  }

  for (size_t i = 0; i < outputs.size(); ++i) {
    auto& ctx = ctxs[i];
    if (ctx.failed || ctx.width == 0 || ctx.height == 0) {
      if (!ctx.failed) SC_LOG("batch_capture_outputs[%zu]: bad dims w=%d h=%d", i, ctx.width, ctx.height);
      ctx.failed = true;
      continue;
    }

    if (ctx.dmabuf_offered && color_mgr) {
      SC_LOG("batch phase3: i=%zu using DMA-BUF (HDR path) fmt=0x%x", i, ctx.fmt);
      ctx.stride = static_cast<uint32_t>(ctx.width) * (bpp_from_fourcc(ctx.fmt) / 8);
#ifdef EH_HAVE_LIBDRM
      if (!create_dmabuf_buffer(ctx, linux_dmabuf)) {
        SC_LOG("batch_capture_outputs[%zu]: create_dmabuf_buffer failed", i);
        ctx.failed = true;
      }
#else
      SC_LOG("batch_capture_outputs[%zu]: DMABUF offered but no libdrm", i);
      ctx.failed = true;
#endif
    } else if (ctx.have_buffer) {
      ctx.map_size = static_cast<size_t>(ctx.stride) * static_cast<size_t>(ctx.height);
      ctx.fd = memfd_create_compat("eh-batch-sc", 0);
      if (ctx.fd < 0) {
        char tmpl[] = "/tmp/eh-batch-sc-XXXXXX";
        ctx.fd = mkstemp(tmpl);
        if (ctx.fd >= 0) unlink(tmpl);
      }
      if (ctx.fd < 0) { SC_LOG("batch_capture_outputs[%zu]: memfd/mkstemp failed", i); ctx.failed = true; continue; }
      if (ftruncate(ctx.fd, static_cast<off_t>(ctx.map_size)) != 0) {
        SC_LOG("batch_capture_outputs[%zu]: ftruncate failed errno=%d", i, errno);
        close(ctx.fd); ctx.fd = -1; ctx.failed = true; continue;
      }

      ctx.map = mmap(nullptr, ctx.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd, 0);
      if (ctx.map == MAP_FAILED) { SC_LOG("batch_capture_outputs[%zu]: mmap failed errno=%d", i, errno); ctx.map = nullptr; close(ctx.fd); ctx.fd = -1; ctx.failed = true; continue; }

      wl_shm_pool* pool = wl_shm_create_pool(shm, ctx.fd, static_cast<int>(ctx.map_size));
      if (!pool) { SC_LOG("batch_capture_outputs[%zu]: wl_shm_create_pool failed", i); ctx.failed = true; continue; }

      ctx.wl_buf = wl_shm_pool_create_buffer(
          pool, 0, static_cast<int>(ctx.width), static_cast<int>(ctx.height),
          static_cast<int>(ctx.stride), static_cast<int>(ctx.fmt));
      wl_shm_pool_destroy(pool);
      if (!ctx.wl_buf) { SC_LOG("batch_capture_outputs[%zu]: wl_shm_pool_create_buffer failed", i); ctx.failed = true; continue; }

      wl_buffer_add_listener(ctx.wl_buf, &kBufRelease, nullptr);
    } else if (ctx.dmabuf_offered) {
      ctx.stride = static_cast<uint32_t>(ctx.width) * (bpp_from_fourcc(ctx.fmt) / 8);
#ifdef EH_HAVE_LIBDRM
      if (!create_dmabuf_buffer(ctx, linux_dmabuf)) {
        SC_LOG("batch_capture_outputs[%zu]: create_dmabuf_buffer failed (no color_mgr)", i);
        ctx.failed = true;
      }
#else
      SC_LOG("batch_capture_outputs[%zu]: DMABUF offered but no libdrm (no color_mgr)", i);
      ctx.failed = true;
#endif
    } else {
      SC_LOG("batch_capture_outputs[%zu]: no buffer path (dmabuf_offered=%d have_buffer=%d)", i, ctx.dmabuf_offered, ctx.have_buffer);
      ctx.failed = true;
    }
  }

  for (auto& ctx : ctxs) {
    if (!ctx.failed && ctx.frame && ctx.wl_buf) {
      ctx.phase2_done = false;
      ctx.phase2_success = false;
      zwlr_screencopy_frame_v1_copy(ctx.frame, ctx.wl_buf);
    }
  }
  wl_display_flush(display);

  {
    constexpr int kMax = 65536;
    for (int iter = 0; iter < kMax; ++iter) {
      bool all_done = true;
      for (auto& c : ctxs) {
        if (c.failed) continue;
        if (c.frame && !c.phase2_done) { all_done = false; break; }
      }
      if (all_done) break;

      bool any_alive = false;
      for (auto& c : ctxs) { if (c.frame && !c.failed) { any_alive = true; break; } }
      if (!any_alive) { SC_LOG("batch_capture_outputs phase2: no alive frames"); break; }

      if (wl_display_dispatch(display) < 0) { SC_LOG("batch_capture_outputs phase2: dispatch error"); goto batch_cleanup; }
    }
  }

  {
    bool any_ok = false;
    for (size_t i = 0; i < outputs.size(); ++i) {
      auto& ctx = ctxs[i];
      auto& out = outputs[i];
      SC_LOG("batch phase6: i=%zu failed=%d phase2_success=%d map=%p w=%d h=%d stride=%d using_dmabuf=%d",
                i, (int)ctx.failed, (int)ctx.phase2_success, (void*)ctx.map,
                ctx.width, ctx.height, ctx.stride, (int)ctx.using_dmabuf);
      if (ctx.failed || !ctx.phase2_success) continue;

      if (ctx.using_dmabuf && !ctx.map) {
#ifdef EH_HAVE_LIBDRM
        if (!mmap_dmabuf_buffer(ctx)) {
          SC_LOG("batch phase6: mmap_dmabuf_buffer failed for i=%zu", i);
          continue;
        }
#else
        continue;
#endif
      }
      if (!ctx.map) continue;

      out.native_w = static_cast<int>(ctx.width);
      out.native_h = static_cast<int>(ctx.height);

      if (color_mgr) {
        ColorSpaceInfo cs = query_output_color_space(display, color_mgr, out.output);
        out.is_hdr = cs.is_hdr;
        out.max_lum = cs.max_lum;
      }

      int hd_bpp = fmt_is_half_float(ctx.fmt) || fmt_is_16bit_int(ctx.fmt) ? 8 : 4;
      if (fmt_is_half_float(ctx.fmt)) {
        SC_LOG("wlr batch: half-float fmt=0x%x", ctx.fmt);
        out.hdr_linear_rgb.resize(static_cast<size_t>(out.native_w) * out.native_h * 3);
        out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
        for (int y = 0; y < out.native_h; ++y) {
          int sy = ctx.y_invert ? (out.native_h - 1 - y) : y;
          const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(sy) * ctx.stride;
          read_half_float_row(row, out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3,
                              out.native_w, ctx.fmt);
          float* hdr = out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3;
          unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
          for (int x = 0; x < out.native_w; ++x) {
            float r = hdr[x * 3 + 0], g = hdr[x * 3 + 1], b = hdr[x * 3 + 2];
            if (out.is_hdr) {
              r = pq_to_linear(r, out.max_lum);
              g = pq_to_linear(g, out.max_lum);
              b = pq_to_linear(b, out.max_lum);
              hdr[x * 3 + 0] = r; hdr[x * 3 + 1] = g; hdr[x * 3 + 2] = b;
            }
            aces_tone_map(r, g, b);
            dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
            dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
            dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
            dst[x * 4 + 3] = 255;
          }
        }
      } else if (fmt_is_high_depth(ctx.fmt)) {
        out.hdr_linear_rgb.resize(static_cast<size_t>(out.native_w) * out.native_h * 3);
        out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
        for (int y = 0; y < out.native_h; ++y) {
          int sy = ctx.y_invert ? (out.native_h - 1 - y) : y;
          const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(sy) * ctx.stride;
          float* hdr = out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3;
          unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
          for (int x = 0; x < out.native_w; ++x) {
            read_high_depth_pixel(row + x * hd_bpp, ctx.fmt, hdr[x * 3 + 0], hdr[x * 3 + 1], hdr[x * 3 + 2]);
            float r = hdr[x * 3 + 0], g = hdr[x * 3 + 1], b = hdr[x * 3 + 2];
            aces_tone_map(r, g, b);
            dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
            dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
            dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
            dst[x * 4 + 3] = 255;
          }
        }
      } else {
        SC_LOG("wlr batch: 8-bit fmt=0x%x", ctx.fmt);
        out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
        for (int y = 0; y < out.native_h; ++y) {
          int sy = ctx.y_invert ? (out.native_h - 1 - y) : y;
          const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(sy) * ctx.stride;
          unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
          for (int x = 0; x < out.native_w; ++x) {
            unpack_pixel(row + x * 4, ctx.fmt, dst + x * 4);
          }
        }
      }

      out.captured = true;
      any_ok = true;
      SC_LOG("batch phase6: captured i=%zu native=%dx%d pix=%zu",
                i, out.native_w, out.native_h, out.rgba_pixels.size());
    }

    for (size_t i = 0; i < outputs.size(); ++i) {
      SC_LOG("batch final: i=%zu captured=%d native=%dx%d logical=%dx%d @(%d,%d)",
                i, (int)outputs[i].captured,
                outputs[i].native_w, outputs[i].native_h,
                outputs[i].logical_w, outputs[i].logical_h,
                outputs[i].logical_x, outputs[i].logical_y);
    }

    for (auto& ctx : ctxs) {
      if (ctx.frame) { zwlr_screencopy_frame_v1_destroy(ctx.frame); ctx.frame = nullptr; }
      if (ctx.wl_buf) { wl_buffer_destroy(ctx.wl_buf); ctx.wl_buf = nullptr; }
      if (ctx.map) { munmap(ctx.map, ctx.map_size); ctx.map = nullptr; }
      if (ctx.fd >= 0) { close(ctx.fd); ctx.fd = -1; }
      if (ctx.using_dmabuf && ctx.drm_fd >= 0) { close(ctx.drm_fd); ctx.drm_fd = -1; }
    }

    SC_LOG("batch_capture_outputs: returning %d", any_ok);
  return any_ok;
  }

batch_cleanup:
  for (auto& ctx : ctxs) {
    if (ctx.frame) zwlr_screencopy_frame_v1_destroy(ctx.frame);
    if (ctx.wl_buf) wl_buffer_destroy(ctx.wl_buf);
    if (ctx.map) munmap(ctx.map, ctx.map_size);
    if (ctx.fd >= 0) close(ctx.fd);
    if (ctx.using_dmabuf && ctx.drm_fd >= 0) close(ctx.drm_fd);
  }
  return false;
}

namespace {

struct ExtCaptureCtx {
  wl_display* display = nullptr;
  ext_image_copy_capture_session_v1* session = nullptr;
  ext_image_copy_capture_frame_v1* frame = nullptr;
  ext_image_capture_source_v1* source = nullptr;
  wl_buffer* wl_buf = nullptr;
  void* map = nullptr;
  size_t map_size = 0;
  int fd = -1;
  uint32_t fmt = WL_SHM_FORMAT_ARGB8888;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  bool constraints_done = false;
  bool frame_done = false;
  bool frame_ok = false;
  bool failed = false;
  bool is_hdr = false;
  uint32_t max_lum = 0;
  bool raw_mode = false;
  std::string path_utf8;
};

static void ext_buf_release(void*, wl_buffer*) {}

static constexpr wl_buffer_listener kExtBufRelease = {.release = ext_buf_release};

static void ext_buffer_size(void* data, ext_image_copy_capture_session_v1*, uint32_t w, uint32_t h) {
  SC_LOG("ext_buffer_size %ux%u", w, h);
  auto* c = static_cast<ExtCaptureCtx*>(data);
  c->width = w;
  c->height = h;
}

static void ext_shm_format(void* data, ext_image_copy_capture_session_v1*, uint32_t format) {
  SC_LOG("ext_shm_format fmt=0x%x", format);
  auto* c = static_cast<ExtCaptureCtx*>(data);
  c->fmt = format;
}

static void ext_done(void* data, ext_image_copy_capture_session_v1*) {
  SC_LOG("ext_done");
  auto* c = static_cast<ExtCaptureCtx*>(data);
  c->constraints_done = true;
}

static void ext_stopped(void* data, ext_image_copy_capture_session_v1*) {
  SC_LOG("ext_stopped");
  auto* c = static_cast<ExtCaptureCtx*>(data);
  c->failed = true;
}

static void ext_dmabuf_device(void*, ext_image_copy_capture_session_v1*, wl_array*) {}
static void ext_dmabuf_format(void*, ext_image_copy_capture_session_v1*, uint32_t, wl_array*) {}

static constexpr ext_image_copy_capture_session_v1_listener kExtSessionListener = {
    .buffer_size = ext_buffer_size,
    .shm_format = ext_shm_format,
    .dmabuf_device = ext_dmabuf_device,
    .dmabuf_format = ext_dmabuf_format,
    .done = ext_done,
    .stopped = ext_stopped,
};

static void ext_frame_damage(void*, ext_image_copy_capture_frame_v1*, int32_t, int32_t, int32_t, int32_t) {}

static void ext_frame_transform(void*, ext_image_copy_capture_frame_v1*, uint32_t) {}

static void ext_frame_presentation_time(void*, ext_image_copy_capture_frame_v1*, uint32_t, uint32_t, uint32_t) {}

static void ext_frame_ready(void* data, ext_image_copy_capture_frame_v1* fr) {
  SC_LOG("ext_frame_ready raw_mode=%d path='%s'", static_cast<ExtCaptureCtx*>(data)->raw_mode, static_cast<ExtCaptureCtx*>(data)->path_utf8.c_str());
  auto* c = static_cast<ExtCaptureCtx*>(data);
  (void)fr;
  c->frame_done = true;
  if (!c->map || c->width == 0 || c->height == 0) {
    SC_LOG("ext_frame_ready: bad state (map=%p w=%u h=%u)", (void*)c->map, c->width, c->height);
    c->frame_ok = false;
    return;
  }
  if (c->raw_mode) {
    c->frame_ok = true;
    return;
  }

  int w = static_cast<int>(c->width);
  int h = static_cast<int>(c->height);
  int stride = static_cast<int>(c->stride);
  const auto* map = static_cast<const uint8_t*>(c->map);

  if (c->is_hdr && fmt_is_half_float(c->fmt)) {
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    c->frame_ok = true;
    for (int y = 0; y < h && c->frame_ok; ++y) {
      if (!read_half_float_row(map + static_cast<size_t>(y) * stride, rgb.data() + static_cast<size_t>(y) * w * 3, w, c->fmt)) {
        c->frame_ok = false;
      }
    }
    if (c->frame_ok && c->is_hdr) {
      float ml = static_cast<float>(c->max_lum);
      for (size_t i = 0; i < static_cast<size_t>(w) * h * 3; i += 3) {
        rgb[i + 0] = pq_to_linear(rgb[i + 0], ml);
        rgb[i + 1] = pq_to_linear(rgb[i + 1], ml);
        rgb[i + 2] = pq_to_linear(rgb[i + 2], ml);
      }
    }
    if (c->frame_ok) {
      c->frame_ok = write_hdr_from_linear(rgb.data(), w, h, c->path_utf8.c_str(), c->max_lum);
    }
  } else if (c->is_hdr) {
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);

    for (int y = 0; y < h; ++y) {
      const auto* row = map + static_cast<size_t>(y) * stride;
      unsigned char* dr = rgba.data() + static_cast<size_t>(y) * w * 4;
      float* df = rgb.data() + static_cast<size_t>(y) * w * 3;
      for (int x = 0; x < w; ++x) {
        if (!unpack_pixel(row + x * 4, c->fmt, dr + x * 4)) {
          c->frame_ok = false;
          break;
        }
        df[x * 3 + 0] = dr[x * 4 + 0] / 255.0f;
        df[x * 3 + 1] = dr[x * 4 + 1] / 255.0f;
        df[x * 3 + 2] = dr[x * 4 + 2] / 255.0f;
      }
    }
    if (c->frame_ok) {
      c->frame_ok = write_png_from_linear(rgb.data(), w, h, c->path_utf8.c_str());
    }
  } else if (fmt_is_high_depth(c->fmt)) {
    std::vector<float> rgb(static_cast<size_t>(w) * h * 3);
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    int hd_bpp = fmt_is_16bit_int(c->fmt) ? 8 : 4;
    c->frame_ok = true;
    for (int y = 0; y < h && c->frame_ok; ++y) {
      const auto* row = map + static_cast<size_t>(y) * stride;
      float* df = rgb.data() + static_cast<size_t>(y) * w * 3;
      unsigned char* dr = rgba.data() + static_cast<size_t>(y) * w * 4;
      for (int x = 0; x < w; ++x) {
        read_high_depth_pixel(row + x * hd_bpp, c->fmt, df[x * 3 + 0], df[x * 3 + 1], df[x * 3 + 2]);
        float r = df[x * 3 + 0], g = df[x * 3 + 1], b = df[x * 3 + 2];
        aces_tone_map(r, g, b);
        dr[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
        dr[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
        dr[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
        dr[x * 4 + 3] = 255;
      }
    }
    if (c->frame_ok) {
#ifdef EH_HAVE_LIBPNG
      c->frame_ok = write_png16_from_linear(rgb.data(), w, h, c->path_utf8.c_str());
#else
      c->frame_ok = write_png_from_linear(rgb.data(), w, h, c->path_utf8.c_str());
#endif
    }
  } else {
    c->frame_ok = write_png_rgba(map, w, h, stride, c->fmt, false, c->path_utf8.c_str());
  }
}

static void ext_frame_failed(void* data, ext_image_copy_capture_frame_v1*, uint32_t) {
  SC_LOG("ext_frame_failed");
  auto* c = static_cast<ExtCaptureCtx*>(data);
  c->frame_done = true;
  c->frame_ok = false;
}

static constexpr ext_image_copy_capture_frame_v1_listener kExtFrameListener = {
    .transform = ext_frame_transform,
    .damage = ext_frame_damage,
    .presentation_time = ext_frame_presentation_time,
    .ready = ext_frame_ready,
    .failed = ext_frame_failed,
};

static bool ext_dispatch_until(ExtCaptureCtx& c, bool (*pred)(const ExtCaptureCtx&)) {
  SC_LOG("ext_dispatch_until");
  wl_display_flush(c.display);
  constexpr int kMax = 65536;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (int i = 0; i < kMax; ++i) {
    if (pred(c)) return !c.failed;
    if (c.failed) return false;
    if (std::chrono::steady_clock::now() >= deadline) {
      SC_LOG("ext_dispatch_until: timeout waiting for capture event");
      return false;
    }
    const int r = wl_display_roundtrip(c.display);
    if (r < 0) return false;
  }
  return false;
}

static bool ext_capture_source_to_png(wl_display* display,
                                      ext_image_copy_capture_manager_v1* copy_mgr,
                                      ext_image_capture_source_v1* source,
                                      wl_shm* shm, bool overlay_cursor,
                                      std::string_view png_path) {
  SC_LOG("ext_capture_source_to_png path='%.*s'", (int)png_path.size(), png_path.data());
  if (!display || !copy_mgr || !source || !shm || png_path.empty()) { SC_LOG("ext_capture_source_to_png: invalid args"); return false; }

  ExtCaptureCtx ctx{};
  ctx.display = display;
  ctx.path_utf8 = std::string(png_path);
  ctx.source = source;

  const uint32_t opts = overlay_cursor ? 1u : 0u;
  ctx.session = ext_image_copy_capture_manager_v1_create_session(copy_mgr, ctx.source, opts);
  if (!ctx.session) { SC_LOG("ext_capture_source_to_png: create_session failed"); return false; }
  ext_image_copy_capture_session_v1_add_listener(ctx.session, &kExtSessionListener, &ctx);

  if (!ext_dispatch_until(ctx, [](const ExtCaptureCtx& c) { return c.constraints_done || c.failed; })) {
    SC_LOG("ext_capture_source_to_png: constraints dispatch failed (failed=%d w=%d h=%d)", ctx.failed, ctx.width, ctx.height);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  if (ctx.failed || ctx.width == 0 || ctx.height == 0) {
    SC_LOG("ext_capture_source_to_png: constraints bad (failed=%d w=%d h=%d)", ctx.failed, ctx.width, ctx.height);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }

  int bpp = (fmt_is_half_float(ctx.fmt) || fmt_is_16bit_int(ctx.fmt)) ? 8 : 4;
  ctx.stride = ctx.width * bpp;
  ctx.map_size = static_cast<size_t>(ctx.stride) * static_cast<size_t>(ctx.height);
  ctx.fd = memfd_create_compat("eh-ext-sc", 0);
  if (ctx.fd < 0) {
    char tmpl[] = "/tmp/eh-ext-sc-XXXXXX";
    ctx.fd = mkstemp(tmpl);
    if (ctx.fd >= 0) unlink(tmpl);
  }
  if (ctx.fd < 0) {
    SC_LOG("ext_capture_source_to_png: memfd/mkstemp failed");
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  if (ftruncate(ctx.fd, static_cast<off_t>(ctx.map_size)) != 0) {
    SC_LOG("ext_capture_source_to_png: ftruncate failed errno=%d", errno);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  ctx.map = mmap(nullptr, ctx.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd, 0);
  if (ctx.map == MAP_FAILED) {
    SC_LOG("ext_capture_source_to_png: mmap failed errno=%d", errno);
    ctx.map = nullptr;
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, ctx.fd, static_cast<int>(ctx.map_size));
  if (!pool) {
    SC_LOG("ext_capture_source_to_png: wl_shm_create_pool failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  ctx.wl_buf = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(ctx.width), static_cast<int>(ctx.height),
                                         static_cast<int>(ctx.stride), static_cast<int>(ctx.fmt));
  wl_shm_pool_destroy(pool);
  if (!ctx.wl_buf) {
    SC_LOG("ext_capture_source_to_png: wl_shm_pool_create_buffer failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  wl_buffer_add_listener(ctx.wl_buf, &kExtBufRelease, nullptr);

  ctx.frame = ext_image_copy_capture_session_v1_create_frame(ctx.session);
  if (!ctx.frame) {
    SC_LOG("ext_capture_source_to_png: create_frame failed");
    wl_buffer_destroy(ctx.wl_buf);
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }
  ext_image_copy_capture_frame_v1_add_listener(ctx.frame, &kExtFrameListener, &ctx);

  ext_image_copy_capture_frame_v1_attach_buffer(ctx.frame, ctx.wl_buf);
  ext_image_copy_capture_frame_v1_capture(ctx.frame);
  wl_display_flush(display);

  if (!ext_dispatch_until(ctx, [](const ExtCaptureCtx& c) { return c.frame_done || c.failed; })) {
    SC_LOG("ext_capture_source_to_png: frame dispatch failed (failed=%d frame_ok=%d)", ctx.failed, ctx.frame_ok);
    ext_image_copy_capture_frame_v1_destroy(ctx.frame);
    wl_buffer_destroy(ctx.wl_buf);
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    return false;
  }

  const bool ok = ctx.frame_ok;
  ext_image_copy_capture_frame_v1_destroy(ctx.frame);
  ctx.frame = nullptr;
  wl_buffer_destroy(ctx.wl_buf);
  ctx.wl_buf = nullptr;
  if (ctx.map) {
    munmap(ctx.map, ctx.map_size);
    ctx.map = nullptr;
  }
  if (ctx.fd >= 0) {
    close(ctx.fd);
    ctx.fd = -1;
  }
  ext_image_copy_capture_session_v1_destroy(ctx.session);
  ctx.session = nullptr;
  SC_LOG("ext_capture_source_to_png: returning %d", ok);
  return ok;
}

}

bool ext_capture_output_to_png(wl_display* display,
                                ext_image_copy_capture_manager_v1* copy_mgr,
                                ext_output_image_capture_source_manager_v1* source_mgr,
                                wl_shm* shm, wl_output* output, bool overlay_cursor,
                                wp_color_manager_v1* color_mgr, std::string_view png_path) {
  SC_LOG("ext_capture_output_to_png path='%.*s'", (int)png_path.size(), png_path.data());
  if (!display || !copy_mgr || !source_mgr || !shm || !output || png_path.empty()) { SC_LOG("ext_capture_output_to_png: invalid args"); return false; }

  std::string path = std::string(png_path);

  ColorSpaceInfo cs = query_output_color_space(display, color_mgr, output);

  ExtCaptureCtx ctx{};
  ctx.display = display;
  ctx.path_utf8 = path;
  ctx.is_hdr = cs.is_hdr;
  ctx.max_lum = cs.max_lum;

  ctx.source = ext_output_image_capture_source_manager_v1_create_source(source_mgr, output);
  if (!ctx.source) { SC_LOG("ext_capture_output_to_png: create_source failed"); return false; }

  const uint32_t opts = overlay_cursor ? 1u : 0u;
  ctx.session = ext_image_copy_capture_manager_v1_create_session(copy_mgr, ctx.source, opts);
  if (!ctx.session) {
    SC_LOG("ext_capture_output_to_png: create_session failed");
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  ext_image_copy_capture_session_v1_add_listener(ctx.session, &kExtSessionListener, &ctx);

  if (!ext_dispatch_until(ctx, [](const ExtCaptureCtx& c) { return c.constraints_done || c.failed; })) {
    SC_LOG("ext_capture_output_to_png: constraints dispatch failed");
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  if (ctx.failed || ctx.width == 0 || ctx.height == 0) {
    SC_LOG("ext_capture_output_to_png: constraints bad (failed=%d w=%d h=%d)", ctx.failed, ctx.width, ctx.height);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }

  int bpp2 = (fmt_is_half_float(ctx.fmt) || fmt_is_16bit_int(ctx.fmt)) ? 8 : 4;
  ctx.stride = ctx.width * bpp2;
  ctx.map_size = static_cast<size_t>(ctx.stride) * static_cast<size_t>(ctx.height);
  ctx.fd = memfd_create_compat("eh-ext-sc", 0);
  if (ctx.fd < 0) {
    char tmpl[] = "/tmp/eh-ext-sc-XXXXXX";
    ctx.fd = mkstemp(tmpl);
    if (ctx.fd >= 0) unlink(tmpl);
  }
  if (ctx.fd < 0) {
    SC_LOG("ext_capture_output_to_png: memfd/mkstemp failed");
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  if (ftruncate(ctx.fd, static_cast<off_t>(ctx.map_size)) != 0) {
    SC_LOG("ext_capture_output_to_png: ftruncate failed errno=%d", errno);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  ctx.map = mmap(nullptr, ctx.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd, 0);
  if (ctx.map == MAP_FAILED) {
    SC_LOG("ext_capture_output_to_png: mmap failed errno=%d", errno);
    ctx.map = nullptr;
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, ctx.fd, static_cast<int>(ctx.map_size));
  if (!pool) {
    SC_LOG("ext_capture_output_to_png: wl_shm_create_pool failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  ctx.wl_buf = wl_shm_pool_create_buffer(pool, 0, static_cast<int>(ctx.width), static_cast<int>(ctx.height),
                                         static_cast<int>(ctx.stride), static_cast<int>(ctx.fmt));
  wl_shm_pool_destroy(pool);
  if (!ctx.wl_buf) {
    SC_LOG("ext_capture_output_to_png: wl_shm_pool_create_buffer failed");
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  wl_buffer_add_listener(ctx.wl_buf, &kExtBufRelease, nullptr);

  ctx.frame = ext_image_copy_capture_session_v1_create_frame(ctx.session);
  if (!ctx.frame) {
    SC_LOG("ext_capture_output_to_png: create_frame failed");
    wl_buffer_destroy(ctx.wl_buf);
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }
  ext_image_copy_capture_frame_v1_add_listener(ctx.frame, &kExtFrameListener, &ctx);

  ext_image_copy_capture_frame_v1_attach_buffer(ctx.frame, ctx.wl_buf);
  ext_image_copy_capture_frame_v1_capture(ctx.frame);
  wl_display_flush(display);

  if (!ext_dispatch_until(ctx, [](const ExtCaptureCtx& c) { return c.frame_done || c.failed; })) {
    SC_LOG("ext_capture_output_to_png: frame dispatch failed");
    ext_image_copy_capture_frame_v1_destroy(ctx.frame);
    wl_buffer_destroy(ctx.wl_buf);
    munmap(ctx.map, ctx.map_size);
    close(ctx.fd);
    ext_image_copy_capture_session_v1_destroy(ctx.session);
    ext_image_capture_source_v1_destroy(ctx.source);
    return false;
  }

  const bool ok = ctx.frame_ok;
  ext_image_copy_capture_frame_v1_destroy(ctx.frame);
  ctx.frame = nullptr;
  wl_buffer_destroy(ctx.wl_buf);
  ctx.wl_buf = nullptr;
  if (ctx.map) {
    munmap(ctx.map, ctx.map_size);
    ctx.map = nullptr;
  }
  if (ctx.fd >= 0) {
    close(ctx.fd);
    ctx.fd = -1;
  }
  ext_image_copy_capture_session_v1_destroy(ctx.session);
  ctx.session = nullptr;
  ext_image_capture_source_v1_destroy(ctx.source);
  ctx.source = nullptr;
  SC_LOG("ext_capture_output_to_png: returning %d", ok);
  return ok;
}

bool ext_capture_toplevel_to_png(wl_display* display,
                                  ext_image_copy_capture_manager_v1* copy_mgr,
                                  ext_foreign_toplevel_image_capture_source_manager_v1* toplevel_src_mgr,
                                  ext_foreign_toplevel_handle_v1* toplevel,
                                  wl_shm* shm, bool overlay_cursor,
                                  std::string_view png_path) {
  SC_LOG("ext_capture_toplevel_to_png path='%.*s'", (int)png_path.size(), png_path.data());
  if (!display || !copy_mgr || !toplevel_src_mgr || !toplevel || !shm || png_path.empty()) {
    SC_LOG("ext_capture_toplevel_to_png: invalid args display=%d copy_mgr=%d src_mgr=%d toplevel=%d shm=%d",
             (int)(display != nullptr), (int)(copy_mgr != nullptr), (int)(toplevel_src_mgr != nullptr),
             (int)(toplevel != nullptr), (int)(shm != nullptr));
    return false;
  }

  ext_image_capture_source_v1* source =
      ext_foreign_toplevel_image_capture_source_manager_v1_create_source(toplevel_src_mgr, toplevel);
  if (!source) { SC_LOG("ext_capture_toplevel_to_png: create_source failed"); return false; }

  const bool ok = ext_capture_source_to_png(display, copy_mgr, source, shm, overlay_cursor, png_path);
  ext_image_capture_source_v1_destroy(source);
  SC_LOG("ext_capture_toplevel_to_png: returning %d", ok);
  return ok;
}

static bool ext_batch_dispatch_until(
    wl_display* display,
    std::vector<ExtCaptureCtx>& ctxs,
    bool (*pred)(const ExtCaptureCtx&),
    int timeout_seconds)
{
  SC_LOG("ext_batch_dispatch_until ctxs=%zu timeout=%d", ctxs.size(), timeout_seconds);
  wl_display_flush(display);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
  while (true) {
    bool all_done = true;
    bool any_alive = false;
    for (auto& c : ctxs) {
      if (c.failed) continue;
      if (c.session || c.frame) any_alive = true;
      if (!pred(c)) { all_done = false; }  // continue checking others
    }
    if (all_done) return true;
    if (!any_alive) { SC_LOG("ext_batch_dispatch_until: no alive sessions"); return false; }
    if (std::chrono::steady_clock::now() >= deadline) { SC_LOG("ext_batch_dispatch_until: timeout"); return false; }
    if (wl_display_dispatch(display) < 0) { SC_LOG("ext_batch_dispatch_until: dispatch error"); return false; }
  }
}

bool batch_capture_outputs_ext(
    wl_display* display,
    ext_image_copy_capture_manager_v1* copy_mgr,
    ext_output_image_capture_source_manager_v1* source_mgr,
    wl_shm* shm,
    std::vector<BatchedCaptureOutput>& outputs,
    bool overlay_cursor,
    wp_color_manager_v1* color_mgr)
{
  SC_LOG("batch_capture_outputs_ext outputs=%zu", outputs.size());
  if (outputs.empty() || !display || !copy_mgr || !source_mgr || !shm) { SC_LOG("batch_capture_outputs_ext: invalid args"); return false; }

  std::vector<ExtCaptureCtx> ctxs(outputs.size());

  auto cleanup_all = [&](bool also_maps) {
    for (auto& ctx : ctxs) {
      if (ctx.frame) ext_image_copy_capture_frame_v1_destroy(ctx.frame);
      if (ctx.wl_buf) wl_buffer_destroy(ctx.wl_buf);
      if (ctx.session) ext_image_copy_capture_session_v1_destroy(ctx.session);
      if (ctx.source) ext_image_capture_source_v1_destroy(ctx.source);
      if (also_maps) {
        if (ctx.map) munmap(ctx.map, ctx.map_size);
        if (ctx.fd >= 0) close(ctx.fd);
      }
    }
  };

  for (size_t i = 0; i < outputs.size(); ++i) {
    auto& ctx = ctxs[i];
    ctx.display = display;
    ctx.raw_mode = true;

    if (!outputs[i].output) { SC_LOG("batch_capture_outputs_ext[%zu]: null output", i); ctx.failed = true; continue; }

    ctx.source = ext_output_image_capture_source_manager_v1_create_source(source_mgr, outputs[i].output);
    if (!ctx.source) { SC_LOG("batch_capture_outputs_ext[%zu]: create_source failed", i); ctx.failed = true; continue; }

    const uint32_t opts = overlay_cursor ? 1u : 0u;
    ctx.session = ext_image_copy_capture_manager_v1_create_session(copy_mgr, ctx.source, opts);
    if (!ctx.session) { SC_LOG("batch_capture_outputs_ext[%zu]: create_session failed", i); ctx.failed = true; continue; }

    ext_image_copy_capture_session_v1_add_listener(ctx.session, &kExtSessionListener, &ctx);
  }

  {
    auto constraints_pred = [](const ExtCaptureCtx& c) { return c.constraints_done || c.failed; };
    if (!ext_batch_dispatch_until(display, ctxs, constraints_pred, 10)) {
      SC_LOG("batch_capture_outputs_ext: constraints dispatch failed");
      cleanup_all(true);
      return false;
    }
  }

  for (size_t i = 0; i < outputs.size(); ++i) {
    auto& ctx = ctxs[i];
    if (ctx.failed || ctx.width == 0 || ctx.height == 0) {
      if (ctx.failed) SC_LOG("batch_capture_outputs_ext[%zu]: failed during constraints phase", i);
      else SC_LOG("batch_capture_outputs_ext[%zu]: bad dims w=%d h=%d", i, ctx.width, ctx.height);
      ctx.failed = true; continue;
    }

    int bpp3 = (fmt_is_half_float(ctx.fmt) || fmt_is_16bit_int(ctx.fmt)) ? 8 : 4;
    ctx.stride = ctx.width * bpp3;
    ctx.map_size = static_cast<size_t>(ctx.stride) * static_cast<size_t>(ctx.height);
    ctx.fd = memfd_create_compat("eh-ext-batch", 0);
    if (ctx.fd < 0) {
      char tmpl[] = "/tmp/eh-ext-batch-XXXXXX";
      ctx.fd = mkstemp(tmpl);
      if (ctx.fd >= 0) unlink(tmpl);
    }
    if (ctx.fd < 0) { SC_LOG("batch_capture_outputs_ext[%zu]: memfd/mkstemp failed", i); ctx.failed = true; continue; }
    if (ftruncate(ctx.fd, static_cast<off_t>(ctx.map_size)) != 0) {
      SC_LOG("batch_capture_outputs_ext[%zu]: ftruncate failed errno=%d", i, errno);
      close(ctx.fd); ctx.fd = -1; ctx.failed = true; continue;
    }

    ctx.map = mmap(nullptr, ctx.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx.fd, 0);
    if (ctx.map == MAP_FAILED) { SC_LOG("batch_capture_outputs_ext[%zu]: mmap failed errno=%d", i, errno); ctx.map = nullptr; close(ctx.fd); ctx.fd = -1; ctx.failed = true; continue; }

    wl_shm_pool* pool = wl_shm_create_pool(shm, ctx.fd, static_cast<int>(ctx.map_size));
    if (!pool) { SC_LOG("batch_capture_outputs_ext[%zu]: wl_shm_create_pool failed", i); ctx.failed = true; continue; }

    ctx.wl_buf = wl_shm_pool_create_buffer(
        pool, 0, static_cast<int>(ctx.width), static_cast<int>(ctx.height),
        static_cast<int>(ctx.stride), static_cast<int>(ctx.fmt));
    wl_shm_pool_destroy(pool);
    if (!ctx.wl_buf) { SC_LOG("batch_capture_outputs_ext[%zu]: wl_shm_pool_create_buffer failed", i); ctx.failed = true; continue; }

    wl_buffer_add_listener(ctx.wl_buf, &kExtBufRelease, nullptr);

    ctx.frame = ext_image_copy_capture_session_v1_create_frame(ctx.session);
    if (!ctx.frame) { SC_LOG("batch_capture_outputs_ext[%zu]: create_frame failed", i); ctx.failed = true; continue; }

    ext_image_copy_capture_frame_v1_add_listener(ctx.frame, &kExtFrameListener, &ctx);
    ext_image_copy_capture_frame_v1_attach_buffer(ctx.frame, ctx.wl_buf);
    ext_image_copy_capture_frame_v1_capture(ctx.frame);
  }
  wl_display_flush(display);

  {
    auto frame_pred = [](const ExtCaptureCtx& c) { return c.frame_done || c.failed; };
    if (!ext_batch_dispatch_until(display, ctxs, frame_pred, 10)) {
      SC_LOG("batch_capture_outputs_ext: frame dispatch failed");
      cleanup_all(true);
      return false;
    }
  }

  bool any_ok = false;
  for (size_t i = 0; i < outputs.size(); ++i) {
    auto& ctx = ctxs[i];
    auto& out = outputs[i];
    if (ctx.failed || !ctx.frame_ok || !ctx.map) continue;

    out.native_w = static_cast<int>(ctx.width);
    out.native_h = static_cast<int>(ctx.height);

    if (color_mgr) {
      ColorSpaceInfo cs = query_output_color_space(display, color_mgr, out.output);
      out.is_hdr = cs.is_hdr;
      out.max_lum = cs.max_lum;
    }

    if (fmt_is_half_float(ctx.fmt)) {
      SC_LOG("ext batch: half-float fmt=0x%x", ctx.fmt);
      out.hdr_linear_rgb.resize(static_cast<size_t>(out.native_w) * out.native_h * 3);
      out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
      for (int y = 0; y < out.native_h; ++y) {
        const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(y) * ctx.stride;
        read_half_float_row(row, out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3,
                            out.native_w, ctx.fmt);
        float* hdr = out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3;
        unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
        for (int x = 0; x < out.native_w; ++x) {
          float r = hdr[x * 3 + 0], g = hdr[x * 3 + 1], b = hdr[x * 3 + 2];
          if (out.is_hdr) {
            r = pq_to_linear(r, out.max_lum);
            g = pq_to_linear(g, out.max_lum);
            b = pq_to_linear(b, out.max_lum);
            hdr[x * 3 + 0] = r; hdr[x * 3 + 1] = g; hdr[x * 3 + 2] = b;
          }
          aces_tone_map(r, g, b);
          dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
          dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
          dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
          dst[x * 4 + 3] = 255;
        }
      }
    } else if (fmt_is_high_depth(ctx.fmt)) {
      SC_LOG("ext batch: high-depth fmt=0x%x", ctx.fmt);
      int hd_bpp = fmt_is_16bit_int(ctx.fmt) ? 8 : 4;
      out.hdr_linear_rgb.resize(static_cast<size_t>(out.native_w) * out.native_h * 3);
      out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
      for (int y = 0; y < out.native_h; ++y) {
        const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(y) * ctx.stride;
        float* hdr = out.hdr_linear_rgb.data() + static_cast<size_t>(y) * out.native_w * 3;
        unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
        for (int x = 0; x < out.native_w; ++x) {
          read_high_depth_pixel(row + x * hd_bpp, ctx.fmt, hdr[x * 3 + 0], hdr[x * 3 + 1], hdr[x * 3 + 2]);
          float r = hdr[x * 3 + 0], g = hdr[x * 3 + 1], b = hdr[x * 3 + 2];
          aces_tone_map(r, g, b);
          dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
          dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
          dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
          dst[x * 4 + 3] = 255;
        }
      }
    } else {
      SC_LOG("ext batch: 8-bit fmt=0x%x", ctx.fmt);
      out.rgba_pixels.resize(static_cast<size_t>(out.native_w) * out.native_h * 4);
      for (int y = 0; y < out.native_h; ++y) {
        const auto* row = static_cast<const uint8_t*>(ctx.map) + static_cast<size_t>(y) * ctx.stride;
        unsigned char* dst = out.rgba_pixels.data() + static_cast<size_t>(y) * out.native_w * 4;
        for (int x = 0; x < out.native_w; ++x) {
          unpack_pixel(row + x * 4, ctx.fmt, dst + x * 4);
        }
      }
    }

    out.captured = true;
    any_ok = true;
  }

  cleanup_all(true);
  SC_LOG("batch_capture_outputs_ext: returning %d", any_ok);
  return any_ok;
}

}
