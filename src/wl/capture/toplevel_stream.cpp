#include "wl/capture/toplevel_stream.hpp"

#include "desktop_shell/common/time/mono_time.hpp"
#include "wl/core/memfd.hpp"

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <hyprland-toplevel-export-v1-client-protocol.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

namespace eh::wayland {

namespace {

void stream_log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  FILE* f = fopen("/tmp/eh-shot.log", "a");
  if (f) {
    fprintf(f, "[live] ");
    vfprintf(f, fmt, ap);
    fprintf(f, "\n");
    fclose(f);
  }
  va_end(ap);
}

} // namespace

struct ToplevelStream::Session {
  uint32_t id = 0;
  hyprland_toplevel_export_frame_v1* frame = nullptr;
  wl_buffer* buf = nullptr;
  wl_shm_pool* pool = nullptr;
  int fd = -1;
  void* map = nullptr;
  size_t map_size = 0;

  uint32_t fmt = 0;
  int width = 0, height = 0, stride = 0;
  int buf_w = 0, buf_h = 0;
  bool y_invert = false;
  bool have_buffer = false;
  bool failed = false;
  bool stopped = false;
  int max_dim = 1024;

  // Rate limiting: a window whose compositor-side copy completes instantly
  // (nothing changed on screen) used to have its next capture re-requested
  // immediately, in a tight loop, on the same thread that dispatches pointer
  // events and repaints — that busy-looping is what caused drags to pause/
  // stutter. Sessions now cap themselves to kMinCaptureIntervalMs and, if a
  // frame finishes early, defer the next request instead of firing it right
  // away; ToplevelStream::tick() (driven by the shell's own vsync-paced
  // paint loop) releases the deferred request once the interval has passed.
  uint64_t last_begin_ms = 0;
  bool rechain_pending = false;
  bool one_shot = false;

  wl_shm* shm = nullptr;
  hyprland_toplevel_export_manager_v1* mgr = nullptr;
  Callback cb;
  ToplevelStream* owner = nullptr;
};

namespace {

using Session = ToplevelStream::Session;

// Cap capture re-requests to ~60Hz so an idle/unchanged window can't chain
// captures faster than the shell can actually paint, starving the main loop.
constexpr uint64_t kMinCaptureIntervalMs = 16;

// A one-shot session that has been in flight longer than this (no buffer, no
// ready, no failure) is presumed stalled — e.g. a window the compositor never
// answered for. It is torn down on the next sync so the window gets a fresh
// capture attempt instead of waiting forever on a dead session.
constexpr uint64_t kStaleOneShotSessionMs = 5000;

void session_release_frame(Session& s) {
  if (s.frame) {
    hyprland_toplevel_export_frame_v1_destroy(s.frame);
    s.frame = nullptr;
  }
}

void session_release_buffer(Session& s) {
  if (s.buf) {
    wl_buffer_destroy(s.buf);
    s.buf = nullptr;
  }
  if (s.pool) {
    wl_shm_pool_destroy(s.pool);
    s.pool = nullptr;
  }
  if (s.map) {
    munmap(s.map, s.map_size);
    s.map = nullptr;
  }
  if (s.fd >= 0) {
    ::close(s.fd);
    s.fd = -1;
  }
}

void session_begin(Session& s);
void session_stop(Session& s);

// Bytes-per-pixel for the wl_shm formats the live stream can receive.
static bool stream_fmt_bpp(uint32_t fmt, int& bpp) {
  switch (fmt) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888:
  case WL_SHM_FORMAT_ABGR8888:
  case WL_SHM_FORMAT_XBGR8888:
  case WL_SHM_FORMAT_RGBA8888:
  case WL_SHM_FORMAT_BGRA8888:
  case WL_SHM_FORMAT_RGBX8888:
  case WL_SHM_FORMAT_BGRX8888:
  case WL_SHM_FORMAT_XRGB2101010:
  case WL_SHM_FORMAT_XBGR2101010:
  case WL_SHM_FORMAT_ARGB2101010:
  case WL_SHM_FORMAT_ABGR2101010:
    bpp = 4;
    return true;
  case WL_SHM_FORMAT_XRGB16161616:
  case WL_SHM_FORMAT_XBGR16161616:
  case WL_SHM_FORMAT_ARGB16161616:
  case WL_SHM_FORMAT_ABGR16161616:
    bpp = 8;
    return true;
  default:
    return false;
  }
}

// Decode one source pixel into straight 8-bit RGBA. Mirror of the proven
// decode in screencopy_png.cpp; the compositor delivers 10-bit (e.g. ABGR2101010)
// shm content when high-depth is enabled, so a hardcoded 8-bit bgra read
// would turn every pixel nearly transparent and swap channels.
static bool stream_pixel_rgba(const uint8_t* p, uint32_t fmt, unsigned char* rgba) {
  unsigned char r = 0, g = 0, b = 0, a = 255;
  switch (fmt) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_BGRA8888: {
    uint32_t w; std::memcpy(&w, p, 4);
    b = static_cast<unsigned char>(w & 0xffu);
    g = static_cast<unsigned char>((w >> 8) & 0xffu);
    r = static_cast<unsigned char>((w >> 16) & 0xffu);
    a = static_cast<unsigned char>((w >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XRGB8888:
  case WL_SHM_FORMAT_BGRX8888: {
    uint32_t w; std::memcpy(&w, p, 4);
    b = static_cast<unsigned char>(w & 0xffu);
    g = static_cast<unsigned char>((w >> 8) & 0xffu);
    r = static_cast<unsigned char>((w >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_ABGR8888:
  case WL_SHM_FORMAT_RGBA8888: {
    uint32_t w; std::memcpy(&w, p, 4);
    r = static_cast<unsigned char>(w & 0xffu);
    g = static_cast<unsigned char>((w >> 8) & 0xffu);
    b = static_cast<unsigned char>((w >> 16) & 0xffu);
    a = static_cast<unsigned char>((w >> 24) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XBGR8888:
  case WL_SHM_FORMAT_RGBX8888: {
    uint32_t w; std::memcpy(&w, p, 4);
    r = static_cast<unsigned char>(w & 0xffu);
    g = static_cast<unsigned char>((w >> 8) & 0xffu);
    b = static_cast<unsigned char>((w >> 16) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XRGB2101010:
  case WL_SHM_FORMAT_ARGB2101010: {
    uint32_t w; std::memcpy(&w, p, 4);
    b = static_cast<unsigned char>(((w & 0x3ffu) * 255u / 1023u) & 0xffu);
    g = static_cast<unsigned char>((((w >> 10) & 0x3ffu) * 255u / 1023u) & 0xffu);
    r = static_cast<unsigned char>((((w >> 20) & 0x3ffu) * 255u / 1023u) & 0xffu);
    if (fmt == WL_SHM_FORMAT_ARGB2101010)
      a = static_cast<unsigned char>((((w >> 30) & 0x3u) * 255u / 3u) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XBGR2101010:
  case WL_SHM_FORMAT_ABGR2101010: {
    uint32_t w; std::memcpy(&w, p, 4);
    r = static_cast<unsigned char>(((w & 0x3ffu) * 255u / 1023u) & 0xffu);
    g = static_cast<unsigned char>((((w >> 10) & 0x3ffu) * 255u / 1023u) & 0xffu);
    b = static_cast<unsigned char>((((w >> 20) & 0x3ffu) * 255u / 1023u) & 0xffu);
    if (fmt == WL_SHM_FORMAT_ABGR2101010)
      a = static_cast<unsigned char>((((w >> 30) & 0x3u) * 255u / 3u) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XBGR16161616:
  case WL_SHM_FORMAT_ABGR16161616: {
    uint16_t c[4]; std::memcpy(c, p, 8);
    r = static_cast<unsigned char>((c[0] * 255u / 65535u) & 0xffu);
    g = static_cast<unsigned char>((c[1] * 255u / 65535u) & 0xffu);
    b = static_cast<unsigned char>((c[2] * 255u / 65535u) & 0xffu);
    if (fmt == WL_SHM_FORMAT_ABGR16161616)
      a = static_cast<unsigned char>((c[3] * 255u / 65535u) & 0xffu);
    break;
  }
  case WL_SHM_FORMAT_XRGB16161616:
  case WL_SHM_FORMAT_ARGB16161616: {
    uint16_t c[4]; std::memcpy(c, p, 8);
    b = static_cast<unsigned char>((c[0] * 255u / 65535u) & 0xffu);
    g = static_cast<unsigned char>((c[1] * 255u / 65535u) & 0xffu);
    r = static_cast<unsigned char>((c[2] * 255u / 65535u) & 0xffu);
    if (fmt == WL_SHM_FORMAT_ARGB16161616)
      a = static_cast<unsigned char>((c[3] * 255u / 65535u) & 0xffu);
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

// Box-filtered downsample into the premultiplied BGRA output. Only ever
// shrinks (never upscales — a source already at or under max_dim is copied
// 1:1), and averages a small (up to 4x4) block of source pixels per
// destination pixel instead of point-sampling a single one, which used to
// alias badly (blocky/noisy) on real desktop content once shrunk.
void session_emit_frame(Session& s) {
  if (s.width <= 0 || s.height <= 0 || !s.map) {
    stream_log("frame %08x bad dims %dx%d", s.id, s.width, s.height);
    return;
  }

  int bpp = 0;
  if (!stream_fmt_bpp(s.fmt, bpp)) {
    stream_log("frame %08x unsupported format 0x%x", s.id, s.fmt);
    return;
  }

  const double longSide = static_cast<double>(std::max(s.width, s.height));
  const double scale = longSide > static_cast<double>(s.max_dim)
                           ? longSide / static_cast<double>(s.max_dim)
                           : 1.0;
  const int tw = std::max(1, static_cast<int>(std::lround(s.width / scale)));
  const int th = std::max(1, static_cast<int>(s.height / scale));

  // Sample block size: 1x1 when not downscaling (plain copy), growing up to
  // 4x4 as the shrink factor increases, capped so cost stays bounded even
  // for very large sources.
  const int samples = std::clamp(static_cast<int>(std::lround(scale)), 1, 4);

  ToplevelStreamFrame out;
  out.window_id = s.id;
  out.width = tw;
  out.height = th;
  out.bgra.resize(static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u);

  const auto* src = static_cast<const uint8_t*>(s.map);
  for (int ty = 0; ty < th; ++ty) {
    const int sy0 = static_cast<int>(ty * scale);
    auto* dst = out.bgra.data() + static_cast<size_t>(ty) * static_cast<size_t>(tw) * 4u;
    for (int tx = 0; tx < tw; ++tx) {
      const int sx0 = static_cast<int>(tx * scale);
      unsigned int sumB = 0, sumG = 0, sumR = 0, sumA = 0, count = 0;
      for (int by = 0; by < samples; ++by) {
        int sy = sy0 + by;
        if (sy >= s.height) break;
        int fy = s.y_invert ? s.height - 1 - sy : sy;
        const auto* row = src + static_cast<size_t>(fy) * static_cast<size_t>(s.stride);
        for (int bx = 0; bx < samples; ++bx) {
          int sx = sx0 + bx;
          if (sx >= s.width) break;
          const auto* p = row + static_cast<size_t>(sx) * static_cast<size_t>(bpp);
          unsigned char rgba[4];
          if (!stream_pixel_rgba(p, s.fmt, rgba)) continue;
          sumR += rgba[0];
          sumG += rgba[1];
          sumB += rgba[2];
          sumA += rgba[3];
          ++count;
        }
      }
      if (count == 0) continue;
      const unsigned int a = sumA / count;
      const unsigned int r = sumR / count;
      const unsigned int g = sumG / count;
      const unsigned int b = sumB / count;
      dst[tx * 4 + 0] = static_cast<uint8_t>((b * a + 127u) / 255u);
      dst[tx * 4 + 1] = static_cast<uint8_t>((g * a + 127u) / 255u);
      dst[tx * 4 + 2] = static_cast<uint8_t>((r * a + 127u) / 255u);
      dst[tx * 4 + 3] = static_cast<uint8_t>(a);
    }
  }

  if (s.cb) s.cb(std::move(out));
}

void session_buffer_done(Session& s) {
  if (s.stopped || s.failed) return;
  if (s.width <= 0 || s.height <= 0 || s.stride <= 0) {
    stream_log("frame %08x no buffer info", s.id);
    s.failed = true;
    return;
  }

  // Recreate the shm buffer when the window resized between frames.
  if (!s.buf || s.buf_w != s.width || s.buf_h != s.height) {
    session_release_buffer(s);
    const size_t stride = static_cast<size_t>(s.stride);
    s.map_size = stride * static_cast<size_t>(s.height);
    s.fd = memfd_create_compat("eh-live", 0);
    if (s.fd < 0) {
      char tmpl[] = "/tmp/eh-live-XXXXXX";
      s.fd = mkstemp(tmpl);
      if (s.fd >= 0) unlink(tmpl);
    }
    if (s.fd < 0) {
      s.failed = true;
      return;
    }
    if (ftruncate(s.fd, static_cast<off_t>(s.map_size)) != 0) {
      ::close(s.fd);
      s.fd = -1;
      s.failed = true;
      return;
    }
    s.map = mmap(nullptr, s.map_size, PROT_READ | PROT_WRITE, MAP_SHARED, s.fd, 0);
    if (s.map == MAP_FAILED) {
      s.map = nullptr;
      ::close(s.fd);
      s.fd = -1;
      s.failed = true;
      return;
    }
    s.pool = wl_shm_create_pool(s.shm, s.fd, static_cast<int>(s.map_size));
    if (!s.pool) {
      munmap(s.map, s.map_size);
      s.map = nullptr;
      ::close(s.fd);
      s.fd = -1;
      s.failed = true;
      return;
    }
    s.buf = wl_shm_pool_create_buffer(s.pool, 0, s.width, s.height, s.stride,
                                      static_cast<int>(s.fmt));
    if (!s.buf) {
      session_release_buffer(s);
      s.failed = true;
      return;
    }
    s.buf_w = s.width;
    s.buf_h = s.height;
  }

  hyprland_toplevel_export_frame_v1_copy(s.frame, s.buf, /*ignore_damage=*/1);
}

void session_frame_ready(Session& s) {
  if (s.stopped || s.failed) return;
  session_emit_frame(s);
  session_release_frame(s);
  if (s.stopped) return;

  // One-shot snapshot: a single frame per window is enough; tear the session
  // down so the stream does not keep re-requesting captures for a static card.
  if (s.one_shot) {
    session_stop(s);
    return;
  }

  // Chain the next capture immediately unless we're being asked to go
  // faster than kMinCaptureIntervalMs (the window's copy completed with
  // nothing changed) — in that case defer to ToplevelStream::tick(), which
  // fires the deferred capture once the interval elapses instead of letting
  // this callback busy-loop back-to-back captures on the main thread.
  const uint64_t now = eh::shell::now_mono_ms();
  if (now - s.last_begin_ms >= kMinCaptureIntervalMs) {
    session_begin(s);
  } else {
    s.rechain_pending = true;
  }
}

void session_frame_failed(Session& s) {
  stream_log("frame %08x failed", s.id);
  s.failed = true;
  session_release_frame(s);
}

// Frame listeners.

void on_frame_buffer(void* data, hyprland_toplevel_export_frame_v1*, uint32_t format, uint32_t w,
                     uint32_t h, uint32_t stride) {
  auto& s = *static_cast<ToplevelStream::Session*>(data);
  s.fmt = format;
  s.width = static_cast<int>(w);
  s.height = static_cast<int>(h);
  s.stride = static_cast<int>(stride);
  s.have_buffer = true;
}

void on_frame_damage(void*, hyprland_toplevel_export_frame_v1*, uint32_t, uint32_t, uint32_t,
                     uint32_t) {}

void on_frame_flags(void* data, hyprland_toplevel_export_frame_v1*, uint32_t flags) {
  auto& s = *static_cast<ToplevelStream::Session*>(data);
  s.y_invert = (flags & 1u) != 0;
}

void on_frame_ready(void* data, hyprland_toplevel_export_frame_v1*, uint32_t, uint32_t, uint32_t) {
  session_frame_ready(*static_cast<ToplevelStream::Session*>(data));
}

void on_frame_failed(void* data, hyprland_toplevel_export_frame_v1*) {
  session_frame_failed(*static_cast<ToplevelStream::Session*>(data));
}

void on_frame_linux_dmabuf(void*, hyprland_toplevel_export_frame_v1*, uint32_t, uint32_t, uint32_t) {}

void on_frame_buffer_done(void* data, hyprland_toplevel_export_frame_v1*) {
  session_buffer_done(*static_cast<ToplevelStream::Session*>(data));
}

constexpr hyprland_toplevel_export_frame_v1_listener kFrameListener = {
    .buffer = on_frame_buffer,
    .damage = on_frame_damage,
    .flags = on_frame_flags,
    .ready = on_frame_ready,
    .failed = on_frame_failed,
    .linux_dmabuf = on_frame_linux_dmabuf,
    .buffer_done = on_frame_buffer_done,
};

void session_begin(Session& s) {
  if (s.stopped || s.failed || !s.mgr) return;
  s.rechain_pending = false;
  s.last_begin_ms = eh::shell::now_mono_ms();
  s.have_buffer = false;
  s.y_invert = false;
  s.frame = hyprland_toplevel_export_manager_v1_capture_toplevel(s.mgr, /*overlay_cursor=*/0, s.id);
  if (!s.frame) {
    s.failed = true;
    return;
  }
  hyprland_toplevel_export_frame_v1_add_listener(s.frame, &kFrameListener, &s);
}

void session_stop(ToplevelStream::Session& s) {
  s.stopped = true;
  session_release_frame(s);
  session_release_buffer(s);
}

} // namespace

// ToplevelStream.

ToplevelStream::ToplevelStream() = default;

ToplevelStream::~ToplevelStream() {
  stop();
}

bool ToplevelStream::start(wl_display* display, hyprland_toplevel_export_manager_v1* mgr,
                           wl_shm* shm, Callback cb) {
  if (!display || !mgr || !shm) return false;
  stop();
  display_ = display;
  mgr_ = mgr;
  shm_ = shm;
  cb_ = std::move(cb);
  return true;
}

void ToplevelStream::sync_windows(const std::vector<uint32_t>& window_ids, int max_dim) {
  const int dim = std::max(64, max_dim);

  // Stop sessions whose window is gone. Failed or stalled sessions are also
  // torn down so the window gets a fresh capture attempt this sync: previously
  // a session that failed once (or never got a frame from the compositor)
  // stayed in the list forever, so that window was never captured again.
  const uint64_t now = eh::shell::now_mono_ms();
  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                 [&](const std::unique_ptr<Session>& s) {
                                   const bool gone =
                                       std::find(window_ids.begin(), window_ids.end(), s->id) ==
                                       window_ids.end();
                                   const bool stalled =
                                       s->one_shot && !s->stopped && !s->failed &&
                                       now - s->last_begin_ms >= kStaleOneShotSessionMs;
                                   if (gone || s->failed || stalled) session_stop(*s);
                                   return gone || s->failed || stalled;
                                 }),
                  sessions_.end());

  // Start new sessions.
  for (uint32_t id : window_ids) {
    const bool exists =
        std::any_of(sessions_.begin(), sessions_.end(),
                    [id](const std::unique_ptr<Session>& s) { return s->id == id; });
    if (exists) continue;
    auto s = std::make_unique<Session>();
    s->id = id;
    s->owner = this;
    s->max_dim = dim;
    s->shm = shm_;
    s->mgr = mgr_;
    s->cb = cb_;
    s->one_shot = one_shot_;
    Session* raw = s.get();
    sessions_.push_back(std::move(s));
    session_begin(*raw);
  }
}

void ToplevelStream::set_one_shot(bool one_shot) {
  one_shot_ = one_shot;
}

void ToplevelStream::tick() {
  const uint64_t now = eh::shell::now_mono_ms();
  for (auto& s : sessions_) {
    if (s->rechain_pending && !s->stopped && !s->failed &&
        now - s->last_begin_ms >= kMinCaptureIntervalMs) {
      session_begin(*s);
    }
  }
}

void ToplevelStream::stop() {
  for (auto& s : sessions_) session_stop(*s);
  sessions_.clear();
  display_ = nullptr;
  mgr_ = nullptr;
  shm_ = nullptr;
  cb_ = {};
}

} // namespace eh::wayland
