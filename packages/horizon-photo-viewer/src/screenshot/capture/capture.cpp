#include "screenshot/capture/capture.hpp"
#include "screenshot/capture/drm_kms.hpp"
#include "screenshot/app.hpp"

#include "screenshot/screencopy.hpp"
#include "core/screenshot/foreign_toplevels.hpp"
#include <cairo/cairo.h>
#include <color-management-v1-client-protocol.h>
#include <wayland/core/connection.hpp>

#include <ext-foreign-toplevel-list-v1-client-protocol.h>
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb/stb_image_write.h"
#include "stb/stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <cstdarg>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <sys/mman.h>

#include <wayland-client.h>

#ifdef EH_HAVE_LIBPNG
#include <png.h>
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

#include "core/screenshot/logging.hpp"

namespace hpv::sc {

static void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    cairo_move_to(cr, x + r, y);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
    cairo_close_path(cr);
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

#ifndef EH_HAVE_LIBPNG
static bool write_png8_from_linear(const float* rgb, int w, int h, const char* path) {
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
#endif

bool capture_screen(
    hpv::WaylandConnection& wl,
    const std::string& out_path)
{
  SC_LOG("capture_screen: enter out_path=%s", out_path.c_str());
  auto* display = wl.display();
  auto* shm = wl.shm();
  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* ext_src_mgr = wl.ext_output_image_capture_source_manager();
  auto* wlr_mgr = wl.screencopy_manager();
  auto* color_mgr = wl.color_manager();

  wl.refresh_logical_outputs();
  auto bounds = wl.logical_output_bounds();
  const hpv::LogicalOutputBounds* picked = nullptr;
  int largest_area = 0;
  for (const auto& b : bounds) {
    int area = b.width * b.height;
    if (area > largest_area) { largest_area = area; picked = &b; }
  }
  if (!picked) { SC_LOG("capture_screen: no output found"); return false; }
  SC_LOG("capture_screen: picked output=%p w=%d h=%d",
           (void*)picked->output, picked->width, picked->height);

  if (ext_mgr && ext_src_mgr) {
    SC_LOG("capture_screen: using ext-image-copy-capture");
    bool ok = hpv::sc::ext_capture_output_to_png(
        display, ext_mgr, ext_src_mgr, shm, picked->output, false, color_mgr, out_path);
    SC_LOG("capture_screen: ext-image-copy-capture %s", ok ? "ok" : "FAILED");
    return ok;
  }
  if (wlr_mgr) {
    SC_LOG("capture_screen: using wlr-screencopy");
    bool ok = hpv::sc::screencopy_output_to_png(
        display, wlr_mgr, shm, picked->output, false, out_path);
    SC_LOG("capture_screen: wlr-screencopy %s", ok ? "ok" : "FAILED");
    return ok;
  }
  SC_LOG("capture_screen: no capture method available");
  SC_LOG("capture_screen: exit false");
  return false;
}

std::vector<OutputInfo> list_outputs(hpv::WaylandConnection& wl)
{
  SC_LOG("list_outputs: enter");
  std::vector<OutputInfo> result;
  auto bounds = wl.logical_output_bounds();
  auto* color_mgr = wl.color_manager();
  auto* display = wl.display();
  for (const auto& b : bounds) {
    OutputInfo oi{b.output, b.name, b.global_x, b.global_y, b.width, b.height};
    if (color_mgr) {
      auto ci = hpv::sc::query_output_color_info(display, color_mgr, b.output);
      oi.is_hdr = ci.is_hdr;
      oi.is_10bit = ci.is_10bit;
      oi.max_lum = ci.max_lum;
    }
    result.push_back(std::move(oi));
    SC_LOG("list_outputs: name=%s x=%d y=%d w=%d h=%d hdr=%d",
             b.name.c_str(), b.global_x, b.global_y, b.width, b.height, (int)oi.is_hdr);
  }
  SC_LOG("list_outputs: exit %zu outputs", result.size());
  return result;
}

static void rgba_to_argb32_premul(unsigned char* p, int w, int h)
{
  int n = w * h;
  for (int i = 0; i < n; ++i) {
    unsigned char* px = p + i * 4;
    unsigned char r = px[0], g = px[1], b = px[2], a = px[3];
    unsigned char bp = static_cast<unsigned char>((static_cast<int>(b) * a + 127) / 255);
    unsigned char gp = static_cast<unsigned char>((static_cast<int>(g) * a + 127) / 255);
    unsigned char rp = static_cast<unsigned char>((static_cast<int>(r) * a + 127) / 255);
    px[0] = bp;
    px[1] = gp;
    px[2] = rp;
    px[3] = a;
  }
}

bool capture_output(
    hpv::WaylandConnection& wl,
    wl_output* output,
    const std::string& out_path,
    HdrData* out_hdr)
{
  auto* display = wl.display();
  auto* shm = wl.shm();
  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* ext_src_mgr = wl.ext_output_image_capture_source_manager();
  auto* wlr_mgr = wl.screencopy_manager();

  if (!output) { SC_LOG("capture_output: null output"); return false; }

  SC_LOG("capture_output: enter output=%p out_path=%s", (void*)output, out_path.c_str());
  SC_LOG("capture_output: ext_mgr=%d ext_src_mgr=%d wlr_mgr=%d",
           (int)(ext_mgr != nullptr), (int)(ext_src_mgr != nullptr), (int)(wlr_mgr != nullptr));

  bool want_hdr = false;
  auto* color_mgr = wl.color_manager();
  if (color_mgr && out_hdr) {
    auto ci = hpv::sc::query_output_color_info(display, color_mgr, output);
    want_hdr = ci.is_hdr;
    if (want_hdr) {
      SC_LOG("capture_output: output is HDR, using batch-style capture");
      std::vector<hpv::sc::BatchedCaptureOutput> batch(1);
      batch[0].output = output;

      bool batch_ok = false;
      if (ext_mgr && ext_src_mgr) {
        batch_ok = hpv::sc::batch_capture_outputs_ext(display, ext_mgr, ext_src_mgr, shm, batch, false, color_mgr);
      }
      if (batch_ok && batch[0].captured && !batch[0].hdr_linear_rgb.empty()) {
        SC_LOG("capture_output: ext batch-style success, hdr_linear=%zu",
                 batch[0].hdr_linear_rgb.size());
      } else if (batch_ok && batch[0].captured) {
        SC_LOG("capture_output: ext returned 8-bit (%zu hdr_linear), retrying wlr with DMA-BUF",
                 batch[0].hdr_linear_rgb.size());
        batch[0] = {}; batch[0].output = output;
        batch_ok = false;
      }
      if (!batch_ok && wlr_mgr) {
        batch_ok = hpv::sc::batch_capture_outputs(display, wlr_mgr, shm, batch, false, color_mgr, wl.linux_dmabuf());
      }
      if (batch_ok && batch[0].captured) {
        const auto& b = batch[0];
        int w = b.native_w;
        int h = b.native_h;

        if (!b.hdr_linear_rgb.empty()) {
          out_hdr->linear_rgb = b.hdr_linear_rgb;
          out_hdr->width = w;
          out_hdr->height = h;
          out_hdr->max_lum = b.max_lum;
          out_hdr->valid = true;
        }

        std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
        if (!b.hdr_linear_rgb.empty()) {
          for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
              size_t fi = static_cast<size_t>(y) * w * 3 + x * 3;
              float r = b.hdr_linear_rgb[fi + 0];
              float g = b.hdr_linear_rgb[fi + 1];
              float bl = b.hdr_linear_rgb[fi + 2];
              aces_tone_map(r, g, bl);
              size_t pi = static_cast<size_t>(y) * w * 4 + x * 4;
              rgba[pi + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
              rgba[pi + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
              rgba[pi + 2] = static_cast<unsigned char>(linear_to_sdr(bl) * 255.0f + 0.5f);
              rgba[pi + 3] = 255;
            }
          }
        } else {
          memcpy(rgba.data(), b.rgba_pixels.data(), static_cast<size_t>(w) * h * 4);
        }
        bool wrote = stbi_write_png(out_path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
        SC_LOG("capture_output: batch-style %s, hdr_linear=%zu",
                 wrote ? "success" : "fail", b.hdr_linear_rgb.size());
        return wrote;
      }
      {
        std::string output_name;
        for (const auto& b : wl.logical_output_bounds()) {
          if (b.output == output) { output_name = b.name; break; }
        }
        if (!output_name.empty()) {
          std::string drm_path = find_drm_card_for_output(output_name.c_str());
          if (!drm_path.empty()) {
            HdrData drm_hdr;
            if (capture_output_drm(drm_path.c_str(), output_name.c_str(), drm_hdr)) {
              *out_hdr = std::move(drm_hdr);
              bool wrote = write_preview_from_hdr(*out_hdr, out_path);
              SC_LOG("capture_output: DRM bypass %s (%s)",
                       wrote ? "success" : "preview fail", drm_path.c_str());
              return wrote;
            }
          }
        }
      }

      SC_LOG("capture_output: batch-style failed, falling to non-HDR path");
    }
  }

  if (ext_mgr && ext_src_mgr) {
    if (hpv::sc::ext_capture_output_to_png(
            display, ext_mgr, ext_src_mgr, shm, output, false, nullptr, out_path)) {
      SC_LOG("capture_output: ext success");
      return true;
    }
    SC_LOG("capture_output: ext failed, falling through to wlr");
  }
  if (wlr_mgr) {
    SC_LOG("capture_output: using wlr-screencopy");
    bool ok = hpv::sc::screencopy_output_to_png(
        display, wlr_mgr, shm, output, false, out_path);
    SC_LOG("capture_output: wlr-screencopy %s", ok ? "ok" : "FAILED");
    return ok;
  }
  SC_LOG("capture_output: exit false");
  return false;
}

bool capture_all_screens(
    hpv::WaylandConnection& wl,
    const std::string& out_path,
    HdrData* out_hdr)
{
  SC_LOG("capture_all_screens: enter out_path=%s", out_path.c_str());

  wl.refresh_logical_outputs();
  wl_display_roundtrip(wl.display());
  wl_display_roundtrip(wl.display());
  auto outputs = list_outputs(wl);
  SC_LOG("capture_all_screens: %zu outputs after list", outputs.size());
  if (outputs.empty()) { SC_LOG("capture_all_screens: no outputs"); return false; }

  if (outputs.size() == 1) {
    SC_LOG("capture_all_screens: single output, delegating to capture_output");
    bool single_ok = capture_output(wl, outputs[0].output, out_path);
    SC_LOG("capture_all_screens: single output result=%d", single_ok);
    return single_ok;
  }

  int min_x = outputs[0].global_x, min_y = outputs[0].global_y;
  int max_x = min_x + outputs[0].width, max_y = min_y + outputs[0].height;
  for (const auto& o : outputs) {
    SC_LOG("capture_all_screens: output %s @(%d,%d) %dx%d",
             o.name.c_str(), o.global_x, o.global_y, o.width, o.height);
    min_x = (std::min)(min_x, o.global_x);
    min_y = (std::min)(min_y, o.global_y);
    max_x = (std::max)(max_x, o.global_x + o.width);
    max_y = (std::max)(max_y, o.global_y + o.height);
  }
  int canvas_w = max_x - min_x;
  int canvas_h = max_y - min_y;
  SC_LOG("capture_all_screens: canvas %dx%d (min=(%d,%d))", canvas_w, canvas_h, min_x, min_y);

  auto fill_batch = [&]() {
    std::vector<hpv::sc::BatchedCaptureOutput> batch;
    batch.reserve(outputs.size());
    for (const auto& o : outputs) {
      hpv::sc::BatchedCaptureOutput bco{};
      bco.output = o.output;
      bco.logical_x = o.global_x;
      bco.logical_y = o.global_y;
      bco.logical_w = o.width;
      bco.logical_h = o.height;
      batch.push_back(std::move(bco));
    }
    return batch;
  };

  auto composite_batch = [&](std::vector<hpv::sc::BatchedCaptureOutput>& batch) -> bool {
    auto fill_hdr_data = [&](const std::vector<float>& rgb, int w, int h) {
      if (out_hdr) {
        uint32_t ml = 0;
        for (const auto& b : batch) {
          if (b.captured && b.max_lum > ml) ml = b.max_lum;
        }
        out_hdr->linear_rgb = rgb;
        out_hdr->width = w;
        out_hdr->height = h;
        out_hdr->max_lum = ml;
        out_hdr->valid = true;
      }
    };
    bool any_hdr = false;
    for (const auto& b : batch) {
      if (b.captured && !b.hdr_linear_rgb.empty()) { any_hdr = true; break; }
    }

    if (any_hdr) {
      std::vector<float> canvas_rgb(static_cast<size_t>(canvas_w) * canvas_h * 3, 0.0f);

      for (const auto& b : batch) {
        if (!b.captured) continue;
        if (b.hdr_linear_rgb.empty()) {
          for (int y = 0; y < b.native_h; ++y) {
            int dy = b.logical_y - min_y + y;
            if (dy < 0 || dy >= canvas_h) continue;
            float* cd = canvas_rgb.data() + static_cast<size_t>(dy) * canvas_w * 3
                        + static_cast<size_t>(b.logical_x - min_x) * 3;
            const auto* src = b.rgba_pixels.data() + static_cast<size_t>(y) * b.native_w * 4;
            for (int x = 0; x < b.native_w; ++x) {
              cd[x * 3 + 0] = src[x * 4 + 0] / 255.0f;
              cd[x * 3 + 1] = src[x * 4 + 1] / 255.0f;
              cd[x * 3 + 2] = src[x * 4 + 2] / 255.0f;
            }
          }
        } else {
          for (int y = 0; y < b.native_h; ++y) {
            int dy = b.logical_y - min_y + y;
            if (dy < 0 || dy >= canvas_h) continue;
            const float* src = b.hdr_linear_rgb.data() + static_cast<size_t>(y) * b.native_w * 3;
            float* cd = canvas_rgb.data() + static_cast<size_t>(dy) * canvas_w * 3
                        + static_cast<size_t>(b.logical_x - min_x) * 3;
            std::memcpy(cd, src, static_cast<size_t>(b.native_w) * 3 * sizeof(float));
          }
        }
      }

      fill_hdr_data(canvas_rgb, canvas_w, canvas_h);

      if (any_hdr && out_path != "-") {
#ifdef EH_HAVE_LIBJXL
        bool hdr_any = false;
        for (const auto& b : batch) {
          if (b.captured && b.is_hdr) { hdr_any = true; break; }
        }
        if (hdr_any) {
          uint32_t max_lum = 0;
          for (const auto& b : batch) {
            if (b.captured && b.max_lum > max_lum) max_lum = b.max_lum;
          }
          return write_jxl_hdr(canvas_rgb.data(), canvas_w, canvas_h, out_path.c_str(), max_lum);
        } else
#endif
        {
#ifdef EH_HAVE_LIBPNG
          return write_png16_from_linear(canvas_rgb.data(), canvas_w, canvas_h, out_path.c_str());
#else
          return write_png8_from_linear(canvas_rgb.data(), canvas_w, canvas_h, out_path.c_str());
#endif
        }
      }
      return !out_path.empty();
    }

    std::vector<unsigned char> canvas(static_cast<size_t>(canvas_w) * canvas_h * 4, 0);

    for (const auto& b : batch) {
      if (!b.captured) {
        SC_LOG("composite: skipping uncaptured output @(%d,%d) %dx%d",
                 b.logical_x, b.logical_y, b.logical_w, b.logical_h);
        continue;
      }
      SC_LOG("composite: output @(%d,%d) %dx%d native=%dx%d",
               b.logical_x, b.logical_y, b.logical_w, b.logical_h,
               b.native_w, b.native_h);

      int dx = b.logical_x - min_x;
      int dy = b.logical_y - min_y;

      for (int y = 0; y < b.native_h; ++y) {
        const auto* src = b.rgba_pixels.data() + static_cast<size_t>(y) * b.native_w * 4;
        auto* dst = canvas.data()
          + static_cast<size_t>(dy + y) * canvas_w * 4
          + static_cast<size_t>(dx) * 4;
        __builtin_memcpy(dst, src, static_cast<size_t>(b.native_w) * 4);
      }
    }

    return stbi_write_png(out_path.c_str(), canvas_w, canvas_h, 4, canvas.data(), canvas_w * 4) != 0;
  };

  auto* wlr_mgr = wl.screencopy_manager();
  auto* color_mgr = wl.color_manager();
  if (wlr_mgr) {
    SC_LOG("capture_all_screens: trying batch wlr-screencopy");
    auto batch = fill_batch();
    if (hpv::sc::batch_capture_outputs(wl.display(), wlr_mgr, wl.shm(), batch, false, color_mgr,
                                            wl.linux_dmabuf())) {
      SC_LOG("capture_all_screens: batch wlr success");
      return composite_batch(batch);
    }
    SC_LOG("capture_all_screens: batch wlr failed");
  } else {
    SC_LOG("capture_all_screens: no wlr screencopy manager");
  }

  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* ext_src_mgr = wl.ext_output_image_capture_source_manager();
  if (ext_mgr && ext_src_mgr) {
    SC_LOG("capture_all_screens: trying batch ext-image-copy-capture");
    auto batch = fill_batch();
    if (hpv::sc::batch_capture_outputs_ext(
            wl.display(), ext_mgr, ext_src_mgr, wl.shm(), batch, false, color_mgr)) {
      SC_LOG("capture_all_screens: batch ext success");
      return composite_batch(batch);
    }
    SC_LOG("capture_all_screens: batch ext failed");
  } else {
    SC_LOG("capture_all_screens: ext_mgr=%d ext_src_mgr=%d",
             (int)(ext_mgr != nullptr), (int)(ext_src_mgr != nullptr));
  }

  SC_LOG("capture_all_screens: falling back to sequential capture");
  cairo_surface_t* canvas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, canvas_w, canvas_h);
  cairo_t* cr = cairo_create(canvas);

  auto* display = wl.display();

  for (const auto& o : outputs) {
    SC_LOG("capture_all_screens: sequential capture of %s @(%d,%d)",
             o.name.c_str(), o.global_x, o.global_y);
    char tmp_pattern[] = "/tmp/eh-shot-all-XXXXXX";
    int fd = mkstemp(tmp_pattern);
    if (fd < 0) { SC_LOG("capture_all_screens: mkstemp failed for %s", o.name.c_str()); continue; }
    std::string tmp_path(tmp_pattern);
    close(fd);

    if (!capture_output(wl, o.output, tmp_path)) {
      SC_LOG("capture_all_screens: capture_output failed for %s", o.name.c_str());
      unlink(tmp_path.c_str());
      wl_display_dispatch_pending(display);
      wl_display_roundtrip(display);
      continue;
    }

    wl_display_dispatch_pending(display);
    wl_display_roundtrip(display);

    int native_w = 0, native_h = 0;
    unsigned char* px = stbi_load(tmp_path.c_str(), &native_w, &native_h, nullptr, 4);
    unlink(tmp_path.c_str());
    if (!px) {
      SC_LOG("capture_all_screens: stbi_load failed for %s", o.name.c_str());
      continue;
    }
    SC_LOG("capture_all_screens: %s loaded native=%dx%d", o.name.c_str(), native_w, native_h);

    rgba_to_argb32_premul(px, native_w, native_h);
    cairo_surface_t* img = cairo_image_surface_create_for_data(
        px, CAIRO_FORMAT_ARGB32, native_w, native_h, native_w * 4);

    double sx = static_cast<double>(o.width) / native_w;
    double sy = static_cast<double>(o.height) / native_h;
    cairo_save(cr);
    cairo_translate(cr, o.global_x - min_x, o.global_y - min_y);
    cairo_rectangle(cr, 0, 0, o.width, o.height);
    cairo_clip(cr);
    cairo_scale(cr, sx, sy);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_surface_destroy(img);
    stbi_image_free(px);
  }

  cairo_destroy(cr);

  cairo_surface_flush(canvas);
  unsigned char* data = cairo_image_surface_get_data(canvas);
  int stride = cairo_image_surface_get_stride(canvas);

  std::vector<unsigned char> rgba(static_cast<size_t>(canvas_w) * canvas_h * 4);
  for (int y = 0; y < canvas_h; ++y) {
    const unsigned char* row = data + static_cast<size_t>(y) * stride;
    unsigned char* dst = rgba.data() + static_cast<size_t>(y) * canvas_w * 4;
    for (int x = 0; x < canvas_w; ++x) {
      unsigned char bp = row[x * 4 + 0], gp = row[x * 4 + 1];
      unsigned char rp = row[x * 4 + 2], a = row[x * 4 + 3];
      unsigned char r, g, b;
      if (a > 0) {
        r = static_cast<unsigned char>((static_cast<int>(rp) * 255 + a / 2) / a);
        g = static_cast<unsigned char>((static_cast<int>(gp) * 255 + a / 2) / a);
        b = static_cast<unsigned char>((static_cast<int>(bp) * 255 + a / 2) / a);
      } else { r = g = b = 0; }
      dst[x * 4 + 0] = r;
      dst[x * 4 + 1] = g;
      dst[x * 4 + 2] = b;
      dst[x * 4 + 3] = a;
    }
  }

  bool ok = stbi_write_png(out_path.c_str(), canvas_w, canvas_h, 4, rgba.data(), canvas_w * 4) != 0;
  SC_LOG("capture_all_screens: sequential png write %s", ok ? "ok" : "FAILED");
  cairo_surface_destroy(canvas);
  SC_LOG("capture_all_screens: exit ok=%d", ok);
  return ok;
}

bool capture_focused_window(
    hpv::WaylandConnection& wl,
    const std::string& out_path)
{
  SC_LOG("capture_focused_window: enter out_path=%s", out_path.c_str());
  auto* display = wl.display();
  auto* shm = wl.shm();
  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* toplevel_src_mgr = wl.ext_foreign_toplevel_image_capture_source_manager();

  SC_LOG("capture_focused_window: ext_mgr=%d toplevel_src_mgr=%d",
           (int)(ext_mgr != nullptr), (int)(toplevel_src_mgr != nullptr));
  if (!ext_mgr || !toplevel_src_mgr) {
    SC_LOG("capture_focused_window: missing ext manager - falling back to screen capture");
    return capture_screen(wl, out_path);
  }
  if (!wl.has_ext_foreign_toplevel_list()) { SC_LOG("capture_focused_window: no ext toplevel list"); return false; }

  wl_display_roundtrip(display);

  const auto& toplevels = wl.ext_foreign_toplevels().list();
  SC_LOG("capture_focused_window: %zu toplevels found", toplevels.size());
  if (toplevels.empty()) { SC_LOG("capture_focused_window: no toplevels"); return false; }

  bool ok = hpv::sc::ext_capture_toplevel_to_png(
      display, ext_mgr, toplevel_src_mgr,
      toplevels[0].handle, shm, false, out_path);
  SC_LOG("capture_focused_window: exit ok=%d", ok);
  return ok;
}

bool capture_window_by_handle(
    hpv::WaylandConnection& wl,
    ext_foreign_toplevel_handle_v1* handle,
    const std::string& out_path)
{
  SC_LOG("capture_window_by_handle: enter handle=%p out_path=%s", (void*)handle, out_path.c_str());
  auto* display = wl.display();
  auto* shm = wl.shm();
  auto* ext_mgr = wl.ext_image_copy_capture_manager();
  auto* toplevel_src_mgr = wl.ext_foreign_toplevel_image_capture_source_manager();

  SC_LOG("capture_window_by_handle: handle=%p ext_mgr=%d toplevel_src_mgr=%d",
           (void*)handle, (int)(ext_mgr != nullptr), (int)(toplevel_src_mgr != nullptr));
  if (!ext_mgr || !toplevel_src_mgr || !handle) {
    SC_LOG("capture_window_by_handle: missing manager or handle - falling back to screen capture");
    return capture_screen(wl, out_path);
  }

  bool ok = hpv::sc::ext_capture_toplevel_to_png(
      display, ext_mgr, toplevel_src_mgr, handle, shm, false, out_path);
  SC_LOG("capture_window_by_handle: exit ok=%d", ok);
  return ok;
}

struct SelOverlay {
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  wl_buffer* buffer[2] = {};
  uint32_t* shm_data[2] = {};
  int buf_w = 0, buf_h = 0;
  int shm_size = 0;
  int global_x = 0, global_y = 0;
  int front = 0;
};

struct SelState {
  wl_display* display = nullptr;
  std::vector<SelOverlay> overlays;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;

  int cur_gx = 0, cur_gy = 0;
  bool dragging = false;
  bool done = false;
  bool cancelled = false;
  int drag_sx = 0, drag_sy = 0;
  int drag_ex = 0, drag_ey = 0;
  int result_x = 0, result_y = 0, result_w = 0, result_h = 0;
  int last_enter_idx = -1;
};

static void sel_overlay_configure(void* data, zwlr_layer_surface_v1* ls,
                                    uint32_t serial, uint32_t w, uint32_t h)
{
  SC_LOG("sel_overlay_configure: serial=%u w=%u h=%u", serial, w, h);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  auto* state = static_cast<SelState*>(data);
  for (auto& o : state->overlays) {
    if (o.layer == ls) {
      o.buf_w = static_cast<int>(w);
      o.buf_h = static_cast<int>(h);
      break;
    }
  }
}

static void sel_overlay_closed(void* data, zwlr_layer_surface_v1*)
{
  SC_LOG("sel_overlay_closed");
  auto* state = static_cast<SelState*>(data);
  state->cancelled = true;
  state->done = true;
}

static constexpr zwlr_layer_surface_v1_listener kSelLayerListener{
  .configure = sel_overlay_configure,
  .closed = sel_overlay_closed,
};

static void sel_pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface* surf,
                              wl_fixed_t sx, wl_fixed_t sy)
{
  auto* state = static_cast<SelState*>(data);
  SC_LOG("sel_pointer_enter: surf=%p sx=%d sy=%d", (void*)surf, wl_fixed_to_int(sx), wl_fixed_to_int(sy));
  state->last_enter_idx = -1;
  for (size_t i = 0; i < state->overlays.size(); ++i) {
    if (state->overlays[i].surface == surf) {
      state->last_enter_idx = static_cast<int>(i);
      state->cur_gx = state->overlays[i].global_x + wl_fixed_to_int(sx);
      state->cur_gy = state->overlays[i].global_y + wl_fixed_to_int(sy);
      break;
    }
  }
}

static void sel_pointer_motion(void* data, wl_pointer*, uint32_t,
                                wl_fixed_t sx, wl_fixed_t sy)
{
  auto* state = static_cast<SelState*>(data);
  SC_LOG("sel_pointer_motion: sx=%d sy=%d", wl_fixed_to_int(sx), wl_fixed_to_int(sy));
  if (state->last_enter_idx < 0) return;
  const auto& o = state->overlays[state->last_enter_idx];
  state->cur_gx = o.global_x + wl_fixed_to_int(sx);
  state->cur_gy = o.global_y + wl_fixed_to_int(sy);
  if (state->dragging) {
    state->drag_ex = state->cur_gx;
    state->drag_ey = state->cur_gy;
  }
}

static void sel_pointer_button(void* data, wl_pointer*, uint32_t, uint32_t,
                                uint32_t button, uint32_t state)
{
  auto* s = static_cast<SelState*>(data);
  SC_LOG("sel_pointer_button: button=%u state=%u", button, state);
  if (button == 273) { s->cancelled = true; s->done = true; return; }
  if (button != 272) return;
  if (state) {
    s->dragging = true;
    s->drag_sx = s->cur_gx;
    s->drag_sy = s->cur_gy;
    s->drag_ex = s->cur_gx;
    s->drag_ey = s->cur_gy;
  } else {
    if (!s->dragging) return;
    s->dragging = false;
    int x1 = (std::min)(s->drag_sx, s->drag_ex);
    int y1 = (std::min)(s->drag_sy, s->drag_ey);
    int x2 = (std::max)(s->drag_sx, s->drag_ex);
    int y2 = (std::max)(s->drag_sy, s->drag_ey);
    int w = x2 - x1, h = y2 - y1;
    if (w < 2 || h < 2) { s->cancelled = true; s->done = true; return; }
    s->result_x = x1; s->result_y = y1;
    s->result_w = w; s->result_h = h;
    s->done = true;
  }
}

static void sel_render_frame(SelState* state)
{
  SC_LOG("sel_render_frame: enter dragging=%d overlays=%zu", state->dragging, state->overlays.size());
  for (auto& o : state->overlays) {
    uint32_t* data = o.shm_data[o.front];
    uint32_t dark = 0x44000000;
    std::fill_n(data, static_cast<size_t>(o.buf_w) * o.buf_h, dark);

    if (state->dragging) {
      int gx1 = state->drag_sx;
      int gy1 = state->drag_sy;
      int gx2 = state->drag_ex;
      int gy2 = state->drag_ey;
      if (gx1 > gx2) std::swap(gx1, gx2);
      if (gy1 > gy2) std::swap(gy1, gy2);

      int ox = o.global_x, oy = o.global_y;
      int ow = o.buf_w, oh = o.buf_h;

      if (gx2 < ox || gx1 >= ox + ow || gy2 < oy || gy1 >= oy + oh)
        goto skip_selection;

      int x1 = (std::max)(gx1 - ox, 0);
      int y1 = (std::max)(gy1 - oy, 0);
      int x2 = (std::min)(gx2 - ox, ow - 1);
      int y2 = (std::min)(gy2 - oy, oh - 1);

      for (int y = y1 + 1; y < y2; ++y) {
        uint32_t* row = data + static_cast<size_t>(y) * ow;
        std::fill_n(row + x1 + 1, static_cast<size_t>(x2 - x1 - 1), 0x00000000);
      }

      for (int x = x1; x <= x2; ++x) {
        uint32_t* row_top = data + static_cast<size_t>(y1) * ow;
        row_top[x] = 0xFFFFFFFF;
        if (y2 > y1) {
          uint32_t* row_bot = data + static_cast<size_t>(y2) * ow;
          row_bot[x] = 0xFFFFFFFF;
        }
      }
      for (int y = y1 + 1; y < y2; ++y) {
        uint32_t* row = data + static_cast<size_t>(y) * ow;
        row[x1] = 0xFFFFFFFF;
        if (x2 > x1) row[x2] = 0xFFFFFFFF;
      }
    }
skip_selection:

    wl_surface_attach(o.surface, o.buffer[o.front], 0, 0);
    wl_surface_damage_buffer(o.surface, 0, 0, o.buf_w, o.buf_h);
    wl_surface_commit(o.surface);
    o.front = 1 - o.front;
  }
  wl_display_flush(state->display);
}

static void sel_cleanup(SelState& state)
{
  SC_LOG("sel_cleanup: enter");
  if (state.pointer) wl_pointer_destroy(state.pointer);
  if (state.keyboard) wl_keyboard_destroy(state.keyboard);
  for (auto& o : state.overlays) {
    if (o.buffer[0]) wl_buffer_destroy(o.buffer[0]);
    if (o.buffer[1]) wl_buffer_destroy(o.buffer[1]);
    if (o.shm_data[0]) munmap(o.shm_data[0], static_cast<std::size_t>(o.shm_size * 2));
    if (o.layer) zwlr_layer_surface_v1_destroy(o.layer);
    if (o.surface) wl_surface_destroy(o.surface);
  }
  state.overlays.clear();
}

bool capture_selection_interactive(
    hpv::WaylandConnection& wl,
    const std::vector<hpv::LogicalOutputBounds>& all_bounds,
    const std::string& out_path)
{
  SC_LOG("capture_selection_interactive: enter out_path=%s", out_path.c_str());
  auto* display = wl.display();
  auto* compositor = wl.compositor();
  auto* shm = wl.shm();
  auto* layer_shell = wl.layer_shell();
  auto* seat = wl.seat();

  SC_LOG("capture_selection_interactive: %zu bounds", all_bounds.size());
  for (const auto& b : all_bounds) {
    SC_LOG("  bound: name=%s output=%p @(%d,%d) %dx%d",
             b.name.c_str(), (void*)b.output, b.global_x, b.global_y, b.width, b.height);
  }
  if (!layer_shell || !compositor || !shm || !seat) {
    SC_LOG("capture_selection_interactive: missing required globals");
    return false;
  }

  SelState state{};
  state.display = display;

  for (const auto& b : all_bounds) {
    if (!b.output) continue;

    SelOverlay o;
    o.global_x = b.global_x;
    o.global_y = b.global_y;
    o.surface = wl_compositor_create_surface(compositor);
    if (!o.surface) { SC_LOG("capture_selection_interactive: wl_compositor_create_surface failed"); sel_cleanup(state); return false; }

    o.layer = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, o.surface, b.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "horizon-shot-sel");
    if (!o.layer) {
      SC_LOG("capture_selection_interactive: zwlr_layer_shell_v1_get_layer_surface failed");
      wl_surface_destroy(o.surface);
      sel_cleanup(state);
      return false;
    }

    zwlr_layer_surface_v1_add_listener(o.layer, &kSelLayerListener, &state);
    zwlr_layer_surface_v1_set_anchor(o.layer,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(o.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(o.layer, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

    wl_surface_commit(o.surface);
    state.overlays.push_back(std::move(o));
  }

  if (state.overlays.empty()) { SC_LOG("capture_selection_interactive: no overlays created"); return false; }
  wl_display_flush(display);
  wl_display_roundtrip(display);

  for (auto& o : state.overlays) {
    if (o.buf_w <= 0 || o.buf_h <= 0) { SC_LOG("capture_selection_interactive: overlay bad buf %dx%d", o.buf_w, o.buf_h); sel_cleanup(state); return false; }
    o.shm_size = o.buf_w * 4 * o.buf_h;
    int total = o.shm_size * 2;
    int fd = memfd_create("wl-shm", MFD_CLOEXEC);
    if (fd < 0) { SC_LOG("capture_selection_interactive: memfd_create failed"); sel_cleanup(state); return false; }
    if (ftruncate(fd, total) < 0) { SC_LOG("capture_selection_interactive: ftruncate failed errno=%d", errno); close(fd); sel_cleanup(state); return false; }
    o.shm_data[0] = static_cast<uint32_t*>(
        mmap(nullptr, static_cast<std::size_t>(total),
             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (!o.shm_data[0] || o.shm_data[0] == MAP_FAILED) { SC_LOG("capture_selection_interactive: mmap failed"); close(fd); sel_cleanup(state); return false; }
    o.shm_data[1] = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uint8_t*>(o.shm_data[0]) + o.shm_size);
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, total);
    o.buffer[0] = wl_shm_pool_create_buffer(pool, 0, o.buf_w, o.buf_h,
                                             o.buf_w * 4, WL_SHM_FORMAT_ARGB8888);
    o.buffer[1] = wl_shm_pool_create_buffer(pool, o.shm_size, o.buf_w, o.buf_h,
                                             o.buf_w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
  }

  sel_render_frame(&state);

  wl_display_roundtrip(display);

  state.pointer = wl_seat_get_pointer(seat);
  if (!state.pointer) { sel_cleanup(state); return false; }

  static constexpr wl_pointer_listener kSelPointerListener{
    .enter = sel_pointer_enter,
    .leave = [](void*, wl_pointer*, uint32_t, wl_surface*) {},
    .motion = sel_pointer_motion,
    .button = sel_pointer_button,
    .axis = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {},
    .frame = [](void*, wl_pointer*) {},
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {},
  };
  wl_pointer_add_listener(state.pointer, &kSelPointerListener, &state);

  state.keyboard = wl_seat_get_keyboard(seat);
  if (state.keyboard) {
    static constexpr wl_keyboard_listener kSelKeyboardListener{
      .keymap = [](void*, wl_keyboard*, uint32_t, int32_t, uint32_t) { (void)0; },
      .enter = [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {},
      .leave = [](void*, wl_keyboard*, uint32_t, wl_surface*) {},
      .key = [](void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t keycode, uint32_t st) {
        if (st == WL_KEYBOARD_KEY_STATE_PRESSED && (keycode == 1 || keycode == 9)) {
          auto* s = static_cast<SelState*>(data);
          s->cancelled = true; s->done = true;
        }
      },
      .modifiers = [](void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {},
      .repeat_info = [](void*, wl_keyboard*, int32_t, int32_t) {},
    };
    wl_keyboard_add_listener(state.keyboard, &kSelKeyboardListener, &state);
  }

  wl_display_flush(display);

  struct pollfd pfd;
  pfd.fd = wl_display_get_fd(display);
  pfd.events = POLLIN;

  while (!state.done) {
    wl_display_dispatch_pending(display);
    wl_display_flush(display);
    if (state.done) break;

    pfd.revents = 0;
    int pr = poll(&pfd, 1, 4);
    if (pr < 0) break;
    if (pr == 0) continue;
    if (wl_display_dispatch(display) < 0) break;

    if (state.dragging)
      sel_render_frame(&state);
  }

  sel_cleanup(state);

  if (state.cancelled) { SC_LOG("capture_selection_interactive: cancelled by user"); return false; }

  SC_LOG("capture_selection_interactive: result rect (%d,%d) %dx%d",
           state.result_x, state.result_y, state.result_w, state.result_h);

  int rx = state.result_x, ry = state.result_y;
  int rw = state.result_w, rh = state.result_h;

  struct SelCapturePiece {
    int dst_x = 0, dst_y = 0;
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
  };
  std::vector<SelCapturePiece> pieces;

  for (const auto& b : all_bounds) {
    if (!b.output) continue;
    int ox = b.global_x, oy = b.global_y;
    int ow = b.width, oh = b.height;
    int inter_x = (std::max)(rx, ox);
    int inter_y = (std::max)(ry, oy);
    int inter_r = (std::min)(rx + rw, ox + ow);
    int inter_b = (std::min)(ry + rh, oy + oh);
    if (inter_x >= inter_r || inter_y >= inter_b) continue;

    char tmp_pattern[] = "/tmp/eh-shot-sel-XXXXXX";
    int fd = mkstemp(tmp_pattern);
    if (fd < 0) continue;
    std::string tmp_path(tmp_pattern);
    close(fd);

    SC_LOG("  capturing %s @(%d,%d) %dx%d", b.name.c_str(), ox, oy, ow, oh);
    if (!capture_output(wl, b.output, tmp_path)) {
      SC_LOG("  capture_output failed for %s", b.name.c_str());
      unlink(tmp_path.c_str());
      continue;
    }

    int img_w = 0, img_h = 0;
    unsigned char* pixels = stbi_load(tmp_path.c_str(), &img_w, &img_h, nullptr, 4);
    unlink(tmp_path.c_str());
    if (!pixels) continue;

    double sx = static_cast<double>(img_w) / ow;
    double sy = static_cast<double>(img_h) / oh;
    int local_x = static_cast<int>((inter_x - ox) * sx);
    int local_y = static_cast<int>((inter_y - oy) * sy);
    int crop_w = static_cast<int>((inter_r - inter_x) * sx);
    int crop_h = static_cast<int>((inter_b - inter_y) * sy);
    local_x = (std::max)(0, local_x);
    local_y = (std::max)(0, local_y);
    crop_w = (std::min)(crop_w, img_w - local_x);
    crop_h = (std::min)(crop_h, img_h - local_y);
    if (crop_w <= 0 || crop_h <= 0) { stbi_image_free(pixels); continue; }

    SelCapturePiece pc;
    pc.dst_x = inter_x - rx;
    pc.dst_y = inter_y - ry;
    pc.w = crop_w;
    pc.h = crop_h;
    pc.rgba.resize(static_cast<size_t>(crop_w) * crop_h * 4);
    for (int y = 0; y < crop_h; ++y) {
      std::memcpy(&pc.rgba[static_cast<size_t>(y) * crop_w * 4],
                  &pixels[(static_cast<size_t>(local_y + y) * img_w + local_x) * 4],
                  static_cast<size_t>(crop_w) * 4);
    }
    stbi_image_free(pixels);
    pieces.push_back(std::move(pc));
  }

  SC_LOG("capture_selection: %zu pieces captured", pieces.size());
  if (pieces.empty()) { SC_LOG("capture_selection: no pieces captured"); return false; }

  if (pieces.size() == 1) {
    bool ok = stbi_write_png(out_path.c_str(), pieces[0].w, pieces[0].h, 4,
                             pieces[0].rgba.data(), pieces[0].w * 4) != 0;
    SC_LOG("capture_selection: single piece png %s", ok ? "ok" : "FAILED");
    return ok;
  }

  cairo_surface_t* sel_canvas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, rw, rh);
  cairo_t* cr = cairo_create(sel_canvas);

  std::vector<std::vector<unsigned char>> premul_bufs;
  premul_bufs.reserve(pieces.size());

  for (const auto& pc : pieces) {
    if (pc.w <= 0 || pc.h <= 0) continue;

    premul_bufs.emplace_back(pc.rgba);
    auto& premul = premul_bufs.back();
    int npx = pc.w * pc.h;
    for (int i = 0; i < npx; ++i) {
      unsigned char* px = premul.data() + i * 4;
      unsigned char r = px[0], g = px[1], bl = px[2], a = px[3];
      unsigned char bp = static_cast<unsigned char>((static_cast<int>(bl) * a + 127) / 255);
      unsigned char gp = static_cast<unsigned char>((static_cast<int>(g)  * a + 127) / 255);
      unsigned char rp = static_cast<unsigned char>((static_cast<int>(r)  * a + 127) / 255);
      px[0] = bp; px[1] = gp; px[2] = rp; px[3] = a;
    }

    cairo_surface_t* img = cairo_image_surface_create_for_data(
        premul.data(), CAIRO_FORMAT_ARGB32, pc.w, pc.h, pc.w * 4);

    cairo_set_source_surface(cr, img, pc.dst_x, pc.dst_y);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);

    cairo_surface_destroy(img);
  }

  cairo_destroy(cr);

  cairo_surface_flush(sel_canvas);
  unsigned char* data = cairo_image_surface_get_data(sel_canvas);
  int stride = cairo_image_surface_get_stride(sel_canvas);

  std::vector<unsigned char> sel_rgba(static_cast<size_t>(rw) * rh * 4);
  for (int y = 0; y < rh; ++y) {
    const unsigned char* row = data + static_cast<size_t>(y) * stride;
    unsigned char* dst = sel_rgba.data() + static_cast<size_t>(y) * rw * 4;
    for (int x = 0; x < rw; ++x) {
      unsigned char bp = row[x * 4 + 0], gp = row[x * 4 + 1];
      unsigned char rp = row[x * 4 + 2], a = row[x * 4 + 3];
      unsigned char r, g, b;
      if (a > 0) {
        r = static_cast<unsigned char>((static_cast<int>(rp) * 255 + a / 2) / a);
        g = static_cast<unsigned char>((static_cast<int>(gp) * 255 + a / 2) / a);
        b = static_cast<unsigned char>((static_cast<int>(bp) * 255 + a / 2) / a);
      } else { r = g = b = 0; }
      dst[x * 4 + 0] = r;
      dst[x * 4 + 1] = g;
      dst[x * 4 + 2] = b;
      dst[x * 4 + 3] = a;
    }
  }

  bool ok = stbi_write_png(out_path.c_str(), rw, rh, 4, sel_rgba.data(), rw * 4) != 0;
  SC_LOG("capture_selection: multi-output stitch png %s", ok ? "ok" : "FAILED");
  cairo_surface_destroy(sel_canvas);
  SC_LOG("capture_selection_interactive: exit ok=%d", ok);
  return ok;
}

bool write_preview_from_hdr(const HdrData& hdr, const std::string& out_path)
{
  SC_LOG("write_preview_from_hdr: enter w=%d h=%d valid=%d", hdr.width, hdr.height, hdr.valid);
  if (!hdr.valid || hdr.linear_rgb.empty() || hdr.width <= 0 || hdr.height <= 0) return false;

  auto linear_to_sdr = [](float c) -> float {
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
  };

  std::vector<unsigned char> rgba(static_cast<size_t>(hdr.width) * hdr.height * 4);
  for (int y = 0; y < hdr.height; ++y) {
    const float* src = hdr.linear_rgb.data() + static_cast<size_t>(y) * hdr.width * 3;
    auto* dst = rgba.data() + static_cast<size_t>(y) * hdr.width * 4;
    for (int x = 0; x < hdr.width; ++x) {
      float r = src[x * 3 + 0], g = src[x * 3 + 1], b = src[x * 3 + 2];
      auto aces = [](float c) {
        float a = 2.51f * c, b_ = 0.03f * c;
        float c2 = c + 0.03f, d = 2.43f * c + 0.59f;
        return (c > 0.0f) ? (a + b_) / (c2 + d) : 0.0f;
      };
      r = aces(r); g = aces(g); b = aces(b);
      dst[x * 4 + 0] = static_cast<unsigned char>(linear_to_sdr(r) * 255.0f + 0.5f);
      dst[x * 4 + 1] = static_cast<unsigned char>(linear_to_sdr(g) * 255.0f + 0.5f);
      dst[x * 4 + 2] = static_cast<unsigned char>(linear_to_sdr(b) * 255.0f + 0.5f);
      dst[x * 4 + 3] = 255;
    }
  }
  bool ok = stbi_write_png(out_path.c_str(), hdr.width, hdr.height, 4, rgba.data(), hdr.width * 4) != 0;
  SC_LOG("write_preview_from_hdr: exit ok=%d", ok);
  return ok;
}

CapturedImage load_capture(const std::string& path)
{
  SC_LOG("load_capture: enter path=%s", path.c_str());
  CapturedImage result;
  int w = 0, h = 0;
  unsigned char* pixels = stbi_load(path.c_str(), &w, &h, nullptr, 4);
  if (!pixels) { SC_LOG("load_capture: stbi_load failed for %s", path.c_str()); return result; }

  rgba_to_argb32_premul(pixels, w, h);

  result.width = w;
  result.height = h;
  size_t size = static_cast<size_t>(w) * h * 4;
  result.pixels.assign(pixels, pixels + size);
  result.valid = true;
  stbi_image_free(pixels);
  SC_LOG("load_capture: exit valid=true w=%d h=%d", w, h);
  return result;
}

static void compose_frame(cairo_t* cr, int img_w, int img_h, const FrameSettings& frame)
{
  int pad = frame.inset;
  double radius = frame.cornerRadius;
  double shadow = frame.shadow;

  if (shadow > 0) {
    double shadow_a = shadow / 200.0;
    cairo_set_source_rgba(cr, 0, 0, 0, shadow_a);
    double blur = 8.0 + shadow * 0.3;
    cairo_set_line_width(cr, blur * 2);
    draw_rounded_rect(cr, pad - blur + 4, pad - blur + 6, img_w + blur * 2 - 8, img_h + blur * 2 - 8, radius + blur * 0.5);
    cairo_stroke(cr);
  }

  cairo_set_source_rgb(cr, 1, 1, 1);
  draw_rounded_rect(cr, pad, pad, img_w, img_h, radius);
  cairo_fill(cr);

  if (frame.showBorder) {
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, 0.8);
    cairo_set_line_width(cr, 1.5);
    draw_rounded_rect(cr, pad, pad, img_w, img_h, radius);
    cairo_stroke(cr);
  }
}

bool save_composed_png(const CapturedImage& img, const FrameSettings& frame, const std::string& out_path)
{
  SC_LOG("save_composed_png: enter out_path=%s img=%dx%d", out_path.c_str(), img.width, img.height);
  if (!img.valid || img.width <= 0 || img.height <= 0) { SC_LOG("save_composed_png: invalid image"); return false; }

  int pad = frame.inset;
  int out_w = img.width + pad * 2;
  int out_h = img.height + pad * 2;

  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out_w, out_h);
  cairo_t* cr = cairo_create(surf);

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  compose_frame(cr, img.width, img.height, frame);

  cairo_surface_t* img_surf = cairo_image_surface_create_for_data(
      const_cast<unsigned char*>(img.pixels.data()), CAIRO_FORMAT_ARGB32,
      img.width, img.height, img.width * 4);

  double radius = frame.cornerRadius;
  cairo_save(cr);
  draw_rounded_rect(cr, pad, pad, img.width, img.height, radius);
  cairo_clip(cr);
  cairo_set_source_surface(cr, img_surf, pad, pad);
  cairo_paint(cr);
  cairo_restore(cr);

  cairo_surface_destroy(img_surf);

  cairo_surface_flush(surf);
  unsigned char* data = cairo_image_surface_get_data(surf);
  int stride = cairo_image_surface_get_stride(surf);
  std::vector<unsigned char> rgba(static_cast<size_t>(out_w) * out_h * 4);
  for (int y = 0; y < out_h; ++y) {
    const unsigned char* row = data + static_cast<size_t>(y) * stride;
    unsigned char* dst = rgba.data() + static_cast<size_t>(y) * out_w * 4;
    for (int x = 0; x < out_w; ++x) {
      unsigned char bp = row[x * 4 + 0];
      unsigned char gp = row[x * 4 + 1];
      unsigned char rp = row[x * 4 + 2];
      unsigned char a  = row[x * 4 + 3];
      unsigned char r, g, b;
      if (a > 0) {
        r = static_cast<unsigned char>((static_cast<int>(rp) * 255 + a / 2) / a);
        g = static_cast<unsigned char>((static_cast<int>(gp) * 255 + a / 2) / a);
        b = static_cast<unsigned char>((static_cast<int>(bp) * 255 + a / 2) / a);
      } else {
        r = g = b = 0;
      }
      dst[x * 4 + 0] = r;
      dst[x * 4 + 1] = g;
      dst[x * 4 + 2] = b;
      dst[x * 4 + 3] = a;
    }
  }
  bool ok = stbi_write_png(out_path.c_str(), out_w, out_h, 4, rgba.data(), out_w * 4) != 0;

  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  SC_LOG("save_composed_png: exit ok=%d", ok);
  return ok;
}

}
