#include "desktop_shell/Overview/overview_capture.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "configuration/shell_config.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "wl/capture/screencopy_png.hpp"
#include "wl/capture/toplevel_stream.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace eh::shell::overview {

static constexpr uint64_t kRecaptureSettleDelayMs = 1000;

static uint32_t window_handle_from_addr(const std::string& addr) {
  const char* p = addr.c_str();
  if (addr.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  uint64_t v = 0;
  for (; *p; ++p) {
    v <<= 4;
    if (*p >= '0' && *p <= '9')
      v += static_cast<uint64_t>(*p - '0');
    else if (*p >= 'a' && *p <= 'f')
      v += static_cast<uint64_t>(*p - 'a' + 10);
    else if (*p >= 'A' && *p <= 'F')
      v += static_cast<uint64_t>(*p - 'A' + 10);
  }
  return static_cast<uint32_t>(v & 0xFFFFFFFFu);
}

static void build_capture_thumbnail(WorkspaceCapture& cap) {
  const int w = cap.width;
  const int h = cap.height;
  if (w <= 0 || h <= 0) return;
  constexpr int kMaxDim = 3840;
  const double scale = std::min(1.0, static_cast<double>(kMaxDim) / std::max(w, h));
  const int tw = std::max(1, static_cast<int>(w * scale + 0.5));
  const int th = std::max(1, static_cast<int>(h * scale + 0.5));

  cairo_surface_t* src = cairo_image_surface_create_for_data(
      const_cast<unsigned char*>(cap.bgra.data()), CAIRO_FORMAT_ARGB32, w, h, w * 4);
  if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(src);
    return;
  }
  cairo_surface_t* dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
  cairo_t* cr = cairo_create(dst);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(src);

  const unsigned char* data = cairo_image_surface_get_data(dst);
  cap.thumbW = tw;
  cap.thumbH = th;
  cap.thumbBgra.assign(data, data + static_cast<size_t>(tw) * static_cast<size_t>(th) * 4u);
  cairo_surface_destroy(dst);
}

static std::shared_ptr<WorkspaceCapture> build_wallpaper_capture(const std::string& path, int outW, int outH) {
  if (path.empty() || outW <= 0 || outH <= 0) return nullptr;

  const auto decoded = eh::wallpaper::decode_thumbnail_to_rgba(path, std::max(outW, outH));
  if (decoded.failed || decoded.width <= 0 || decoded.height <= 0 || decoded.rgba.empty()) return nullptr;

  eh::wallpaper::write_thumbnail_disk_cache_maybe(decoded);

  cairo_surface_t* src = eh::wallpaper::thumbnail_decoded_to_surface(decoded);
  if (!src || cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(src);
    return nullptr;
  }

  auto cap = std::make_shared<WorkspaceCapture>();
  cap->width = outW;
  cap->height = outH;
  cap->bgra.resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u);

  cairo_surface_t* dst = cairo_image_surface_create_for_data(
      cap->bgra.data(), CAIRO_FORMAT_ARGB32, outW, outH, outW * 4);
  if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(dst);
    cairo_surface_destroy(src);
    return nullptr;
  }

  const double imgW = static_cast<double>(decoded.width);
  const double imgH = static_cast<double>(decoded.height);
  const double sc = std::max(static_cast<double>(outW) / imgW, static_cast<double>(outH) / imgH);
  cairo_t* cr = cairo_create(dst);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_save(cr);
  cairo_translate(cr, (static_cast<double>(outW) - imgW * sc) * 0.5,
                  (static_cast<double>(outH) - imgH * sc) * 0.5);
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, src, 0, 0);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(dst);
  cairo_surface_destroy(src);

  build_capture_thumbnail(*cap);
  return cap;
}

// anonymous helpers are now closed; we remain in namespace eh::shell::overview

Capture::Capture(Host& host) : host_(host) {}

bool Capture::live_enabled() const noexcept {
  if (!host_.open()) return false;
  if (host_.dock().compositorKind != CompositorKind::Hyprland) return false;
  const auto& ov = eh::config::shell_config_snapshot().appearance;
  if (!ov.overviewLiveUpdates) return false;
  return host_.wl() && host_.wl()->display() && host_.wl()->shm() &&
         host_.wl()->hyprland_toplevel_export_manager();
}

namespace {

// Which workspace indices are actually scrolled into view (plus a one-card
// margin on either side so neighbours are ready the instant a scroll lands
// on them). Streaming every window in every workspace continuously — what
// this used to do — means a shell with, say, 5 workspaces of 4 windows each
// keeps 20 toplevel-export sessions running at up to 60Hz simultaneously,
// all doing per-pixel format conversion on the main thread, even though at
// most 2-3 workspaces are ever on screen at once. That's the main driver of
// live mode dropping to single-digit fps: cut the session count down to
// what's actually visible.
std::pair<int, int> visible_workspace_range(const Host& host, int count) {
  if (count <= 0) return {0, -1};
  const auto& layout = host.layout();
  const double pitch = layout.pitch > 0.0 ? layout.pitch : 1.0;
  const double viewport = layout.axis == OverviewAxis::Vertical ? layout.h : layout.w;
  const double first = host.scroll_pos() / pitch;
  const double last = (host.scroll_pos() + std::max(viewport, pitch)) / pitch;
  int lo = static_cast<int>(std::floor(first)) - 1;
  int hi = static_cast<int>(std::ceil(last)) + 1;
  lo = std::clamp(lo, 0, count - 1);
  hi = std::clamp(hi, 0, count - 1);
  return {lo, hi};
}

// Live/snapshot frames only ever get drawn at workspace-card size (see
// overview_painter.cpp), never at native window resolution. Previously this
// requested every window at up to the full monitor's native resolution
// (primaryOutputWidthPx/HeightPx), so a 4K screen meant every captured
// window frame arrived near-4K and was box-filtered + BGRA-converted on the
// CPU every single tick just to be scaled back down to a few hundred
// pixels for display. Size the request to what a card can actually show.
int capture_max_dim_for_layout(const OverviewLayout& layout) {
  const double cardDim = std::max(layout.cardW, layout.cardH);
  if (cardDim <= 0.0) return 1024;
  // 1.5x headroom covers HiDPI output scaling without over-requesting.
  return std::clamp(static_cast<int>(cardDim * 1.5 + 0.5), 256, 2048);
}

} // namespace

void Capture::sync_live_streams() {
  if (!live_enabled()) {
    if (live_stream_) live_stream_->stop();
    return;
  }

  if (!live_stream_ || !live_stream_->active()) {
    live_stream_ = std::make_unique<eh::wayland::ToplevelStream>();
    if (!live_stream_->start(host_.wl()->display(), host_.wl()->hyprland_toplevel_export_manager(),
                             host_.wl()->shm(),
                             [this](eh::wayland::ToplevelStreamFrame&& f) {
                               handle_live_frame(std::move(f));
                             })) {
      live_stream_.reset();
      return;
    }
  }

  const auto& workspaces = host_.workspaces();
  const auto [lo, hi] = visible_workspace_range(host_, static_cast<int>(workspaces.size()));

  std::vector<uint32_t> ids;
  ids.reserve(host_.nav_windows().size());
  for (int i = lo; i <= hi; ++i) {
    for (const auto& w : workspaces[static_cast<size_t>(i)].windows) {
      if (w.special || w.addr.empty()) continue;
      ids.push_back(window_handle_from_addr(w.addr));
    }
  }
  live_stream_->sync_windows(ids, capture_max_dim_for_layout(host_.layout()));
}

void Capture::sync_snapshot_streams() {
  if (live_enabled()) {
    if (live_stream_) live_stream_->stop();
    return;
  }
  if (!host_.wl() || !host_.wl()->display() || !host_.wl()->shm() ||
      !host_.wl()->hyprland_toplevel_export_manager()) return;
  if (host_.dock().compositorKind != CompositorKind::Hyprland) return;

  if (!live_stream_ || !live_stream_->active()) {
    live_stream_ = std::make_unique<eh::wayland::ToplevelStream>();
    if (!live_stream_->start(host_.wl()->display(), host_.wl()->hyprland_toplevel_export_manager(),
                             host_.wl()->shm(),
                             [this](eh::wayland::ToplevelStreamFrame&& f) {
                               handle_live_frame(std::move(f));
                             })) {
      live_stream_.reset();
      return;
    }
  }
  live_stream_->set_one_shot(true);

  // Unlike the live path, snapshot mode's sessions are one-shot (one frame,
  // then the session stops), so requesting every window here — including
  // ones scrolled off-screen — is cheap and lets RevealPass's off-screen
  // trick (see overview_reveal.cpp) still fill in workspaces the user
  // hasn't scrolled to yet. Only the resolution needs trimming, same reason
  // as the live path.
  std::vector<uint32_t> ids;
  ids.reserve(host_.nav_windows().size());
  for (const auto& ws : host_.workspaces()) {
    for (const auto& w : ws.windows) {
      if (w.special || w.addr.empty()) continue;
      ids.push_back(window_handle_from_addr(w.addr));
    }
  }
  live_stream_->sync_windows(ids, capture_max_dim_for_layout(host_.layout()));
}

void Capture::handle_live_frame(eh::wayland::ToplevelStreamFrame&& frame) {
  for (auto& ws : host_.workspaces_mut()) {
    for (auto& w : ws.windows) {
      if (w.addr.empty() || window_handle_from_addr(w.addr) != frame.window_id) continue;
      const bool was_valid = w.liveValid;
      w.liveValid = true;
      w.liveW = frame.width;
      w.liveH = frame.height;
      w.liveBgra = std::move(frame.bgra);
      if (!was_valid) host_.set_cards_invalidated();
      if (host_.open() && host_.surface() && !host_.has_frame_cb()) host_.schedule_frame();
      return;
    }
  }
}

void Capture::capture_workspaces(bool preserving) {
  auto& dock = host_.dock();
  auto& workspaces = host_.workspaces_mut();
  const auto& ov = eh::config::shell_config_snapshot().appearance;
  const bool live = ov.overviewLiveUpdates;

  if (live) {
    for (auto& ws : workspaces) {
      if (ws.windows.empty() || ws.overflow || (!ws.monitor.empty() && ws.active)) ws.capture.reset();
    }
  } else if (preserving) {
    if (live) {
      for (auto& ws : workspaces) {
        if (!ws.windows.empty() && !ws.active) {
          debug_log("overview", "capture_workspaces(preserve): dropping ws %d (windows=%d active=%d)",
                    ws.id, static_cast<int>(ws.windows.size()), ws.active);
          ws.capture.reset();
        }
      }
    }
  } else {
    for (auto& ws : workspaces) {
      if (ws.active || !ws.capture || ws.windows.empty() || ws.overflow) ws.capture.reset();
    }
  }

  if (ov.overviewCaptureMode != 1)
    debug_log("overview", "capture_workspaces: mode=%d (Snapshot) — taking static captures",
              ov.overviewCaptureMode);
  if (dock.compositorKind != CompositorKind::Hyprland) {
    debug_log("overview", "capture_workspaces: compositorKind=%d (not Hyprland) — skipping",
              static_cast<int>(dock.compositorKind));
    return;
  }
  if (!host_.wl() || !host_.wl()->display() || !host_.wl()->screencopy_manager() || !host_.wl()->shm()) {
    debug_log("overview", "capture_workspaces: wl_/screencopy/shm unavailable — skipping");
    return;
  }

  if (live) {
    bool anyNeed = false;
    for (const auto& ws : workspaces) {
      if (ws.windows.empty() || ws.overflow) {
        anyNeed = true;
        break;
      }
    }
    if (!anyNeed) return;
  }

  if (preserving) return;

  const auto bounds = host_.wl()->logical_output_bounds();
  std::vector<eh::wayland::BatchedCaptureOutput> outs;
  outs.reserve(bounds.size());
  for (const auto& b : bounds) {
    eh::wayland::BatchedCaptureOutput o;
    o.output = b.output;
    o.logical_w = b.width;
    o.logical_h = b.height;
    outs.push_back(std::move(o));
  }
  if (outs.empty()) return;

  if (!eh::wayland::batch_capture_outputs(host_.wl()->display(), host_.wl()->screencopy_manager(),
                                          host_.wl()->shm(), outs, /*overlay_cursor=*/false,
                                          /*color_mgr=*/nullptr, /*linux_dmabuf=*/nullptr))
    return;

  std::vector<std::pair<std::string, std::shared_ptr<WorkspaceCapture>>> byMon;
  byMon.reserve(outs.size());
  for (size_t i = 0; i < outs.size(); ++i) {
    const auto& out = outs[i];
    if (!out.captured || out.native_w <= 0 || out.native_h <= 0) continue;

    auto cap = std::make_shared<WorkspaceCapture>();
    cap->width = out.native_w;
    cap->height = out.native_h;
    cap->bgra.resize(static_cast<size_t>(cap->width) * static_cast<size_t>(cap->height) * 4u);

    const size_t n = static_cast<size_t>(cap->width) * static_cast<size_t>(cap->height);
    for (size_t p = 0; p < n; ++p) {
      const unsigned int r = out.rgba_pixels[p * 4 + 0];
      const unsigned int g = out.rgba_pixels[p * 4 + 1];
      const unsigned int b = out.rgba_pixels[p * 4 + 2];
      const unsigned int a = out.rgba_pixels[p * 4 + 3];
      cap->bgra[p * 4 + 0] = static_cast<uint8_t>((b * a + 127u) / 255u);
      cap->bgra[p * 4 + 1] = static_cast<uint8_t>((g * a + 127u) / 255u);
      cap->bgra[p * 4 + 2] = static_cast<uint8_t>((r * a + 127u) / 255u);
      cap->bgra[p * 4 + 3] = static_cast<uint8_t>(a);
    }

    build_capture_thumbnail(*cap);
    byMon.emplace_back(bounds[i].name, std::move(cap));
  }

  const auto& cfg = eh::config::shell_config_snapshot();
  std::shared_ptr<const WorkspaceCapture> desktop;
  if (cfg.wallpaperEnabled && !cfg.wallpaperImage.empty()) {
    if (desktop_wallpaper_path_ != cfg.wallpaperImage) {
      int outW = 640, outH = 360;
      if (dock.primaryOutputWidthPx > 0 && dock.primaryOutputHeightPx > 0) {
        constexpr int kMaxDim = 640;
        if (dock.primaryOutputWidthPx >= dock.primaryOutputHeightPx) {
          outW = kMaxDim;
          outH = std::max(1, static_cast<int>(
              kMaxDim * static_cast<double>(dock.primaryOutputHeightPx) / dock.primaryOutputWidthPx + 0.5));
        } else {
          outH = kMaxDim;
          outW = std::max(1, static_cast<int>(
              kMaxDim * static_cast<double>(dock.primaryOutputWidthPx) / dock.primaryOutputHeightPx + 0.5));
        }
      }
      desktop_wallpaper_path_ = cfg.wallpaperImage;
      desktop_wallpaper_capture_ = build_wallpaper_capture(cfg.wallpaperImage, outW, outH);
    }
    desktop = desktop_wallpaper_capture_;
  }
  desktop_capture_ = desktop;

  for (auto& ws : workspaces) {
    if (!ws.monitor.empty()) {
      for (const auto& [mon, cap] : byMon) {
        if (mon == ws.monitor) {
          ws.capture = cap;
          break;
        }
      }
    }
    if (!ws.capture && ws.windows.empty()) ws.capture = desktop;
  }

  for (auto& ws : workspaces) {
    if (!ws.capture) ws.capture = desktop;
  }

  for (const auto& ws : workspaces) {
    const bool hasCap = ws.capture && ws.capture->width > 0 && ws.capture->height > 0;
    debug_log("overview", "capture_workspaces: ws %d mon=%s active=%d cap=%s%dx%d",
              ws.id, ws.monitor.c_str(), ws.active,
              hasCap ? "ok " : "NONE ",
              hasCap ? ws.capture->width : 0, hasCap ? ws.capture->height : 0);
  }
  debug_log("overview", "capture_workspaces: byMon=%zu desktop=%s outputs=%zu",
            byMon.size(),
            desktop && desktop->width > 0 ? "ok" : "NONE",
            bounds.size());
}

} // namespace eh::shell::overview
