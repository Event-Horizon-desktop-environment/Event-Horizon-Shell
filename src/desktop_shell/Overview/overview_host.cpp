#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/Overview/overview_profiler.hpp"

#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/icon_cache/icon_cache.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "configuration/shell_config.hpp"
#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/shared/popup/chrome/chrome.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/workspaces/workspaces_paint.hpp"
#include "wl/surface/layer_surface.hpp"

#include "viewporter-client-protocol.h"

#include <cairo/cairo.h>
#include <wayland-client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

namespace eh::shell::overview {

namespace {

constexpr float kOpenDurationMs = 250.f;
constexpr float kCloseDurationMs = 200.f;
constexpr eh::shell::Easing kOpenEasing = eh::shell::Easing::EaseOutCubic;
constexpr eh::shell::Easing kCloseEasing = eh::shell::Easing::EaseOutQuad;

constexpr double kScrollEaseRatePerSec = 32.0;
constexpr double kScrollSettleEpsilonPx = 0.25;
constexpr double kDragAutoScrollPitchPerSec = 1.6;

constexpr double kDockSpacingPx = 25.0;
constexpr double kTaskbarSpacingPx = 12.0;
constexpr double kOverflowMargin = 0.05;
constexpr uint64_t kDataRefreshIntervalMs = 2000;
constexpr uint64_t kEventCoalesceMs = 50;
constexpr uint64_t kWatchdogIntervalMs = 30000;

// Pick the primary output for the overview layer surface.
// In multi-monitor mode, this is the largest output.  In single-monitor mode,
// this is also the largest output (the one where the dock lives).
wl_output* pick_primary_output(eh::wayland::WaylandConnection* wl) {
  if (!wl) return nullptr;
  return wl->pick_largest_logical_output().output;
}

// Present rendered BGRA pixels on a layer surface.  Prefers the Vulkan path;
// when Vulkan is disabled or unavailable it blits into a plain shm buffer so
// the overview keeps working in CPU mode.
bool present_content_pixels(eh::wayland::VulkanLayerSurface* vk_layer,
                            eh::wayland::VulkanDisplayContext* vk_ctx,
                            eh::wayland::ShmBuffer& shm_fallback,
                            wl_shm* shm, wl_surface* surface, int w, int h,
                            const unsigned char* bgra, int stride) {
  if (!surface || !bgra || w <= 0 || h <= 0) return false;
  if (vk_layer && vk_ctx)
    return vk_layer->present_cpu_bgra(*vk_ctx, bgra, w, h, stride);
  if (!shm || shm_fallback.busy()) return false;
  if (!shm_fallback.ensure(shm, kOverviewNamespace, w, h)) return false;
  const int rowBytes = std::min(stride, shm_fallback.stride());
  auto* dst = static_cast<unsigned char*>(shm_fallback.data());
  for (int y = 0; y < h; ++y)
    std::memcpy(dst + static_cast<size_t>(y) * shm_fallback.stride(),
                bgra + static_cast<size_t>(y) * stride, static_cast<size_t>(rowBytes));
  wl_surface_attach(surface, shm_fallback.wl(), 0, 0);
  wl_surface_damage_buffer(surface, 0, 0, w, h);
  shm_fallback.mark_busy();
  return true;
}

const wl_seat_listener kSeatListener = {
  .capabilities = Host::seat_capabilities,
  .name         = Host::seat_name,
};

const wl_pointer_listener kPointerListener = {
  .enter              = Host::pointer_enter,
  .leave              = Host::pointer_leave,
  .motion             = Host::pointer_motion,
  .button             = Host::pointer_button,
  .axis               = Host::pointer_axis,
  .frame              = Host::pointer_frame,
  .axis_source        = Host::pointer_axis_source,
  .axis_stop          = Host::pointer_axis_stop,
  .axis_discrete      = Host::pointer_axis_discrete,
  .axis_value120      = nullptr,
  .axis_relative_direction = nullptr,
#ifdef EH_HAVE_POINTER_WARP
  .warp = [](void* data, wl_pointer* ptr, wl_fixed_t sx, wl_fixed_t sy) {
    Host::pointer_motion(data, ptr, 0, sx, sy);
  },
#endif
};

const wl_keyboard_listener kKeyboardListener = {
  .keymap      = Host::keyboard_keymap,
  .enter       = Host::keyboard_enter,
  .leave       = Host::keyboard_leave,
  .key         = Host::keyboard_key,
  .modifiers   = Host::keyboard_modifiers,
  .repeat_info = Host::keyboard_repeat_info,
};

const zwlr_layer_surface_v1_listener kLayerListener = {
  .configure = Host::layer_configure,
  .closed    = Host::layer_closed,
};

const wl_callback_listener kFrameListener = {
  .done = overview_host_page_frame_done,
};

} // namespace

void overview_host_page_frame_done(void* data, wl_callback* cb, uint32_t /*compositor_time_ms*/) {
  auto* self = static_cast<Host*>(data);
  wl_callback_destroy(cb);
  self->frame_cb_ = nullptr;
  if (!self->surface_ || !self->wl_) return;
  const auto frameT0 = std::chrono::steady_clock::now();

  const uint64_t now = now_mono_ms();
  double dt = 0.016;
  if (self->last_frame_ms_ != 0 && now > self->last_frame_ms_)
    dt = std::clamp(static_cast<double>(now - self->last_frame_ms_) / 1000.0, 0.0, 0.1);
  self->last_frame_ms_ = now;

  if (self->input_->trackpad_settle_until_ms_ != 0 && now >= self->input_->trackpad_settle_until_ms_) {
    self->input_->trackpad_settle_until_ms_ = 0;
    self->snap_scroll();
  }

  overview::OverviewProfiler::instance().begin_frame(
      static_cast<int>(self->workspaces_.size()), self->count_painted_windows(),
      self->w_, self->h_);

  {
    overview::StageGuard pg(overview::Stage::AnimTick);
    self->anim_.tick();
  }
  if (self->input_->drag_active && self->input_->drag_auto_scroll_dir != 0 && self->layout_.pitch > 0.0) {
    self->scroll_target_ += static_cast<double>(self->input_->drag_auto_scroll_dir) *
                            self->layout_.pitch * kDragAutoScrollPitchPerSec * dt;
    self->recompute_scroll_bounds();
  }
  {
    overview::StageGuard pg(overview::Stage::UpdateScroll);
    self->update_scroll(dt);
  }
  // Tick scroll animation for extra outputs.
  for (auto& ep : self->extra_outputs_) {
    if (ep->scroll_pos == ep->scroll_target) continue;
    const double factor = std::exp(-kScrollEaseRatePerSec * dt);
    ep->scroll_pos = ep->scroll_target + (ep->scroll_pos - ep->scroll_target) * factor;
    if (std::abs(ep->scroll_pos - ep->scroll_target) <= kScrollSettleEpsilonPx)
      ep->scroll_pos = ep->scroll_target;
    ep->selected_index = static_cast<int>(
        overview_selected_index(self->layout_, ep->scroll_pos,
                                static_cast<int>(ep->workspaces.size())));
  }
  // Tick app-grid scroll animation.
  if (self->show_apps_ && self->app_scroll_pos_ != self->app_scroll_target_) {
    const double factor = std::exp(-kScrollEaseRatePerSec * dt);
    self->app_scroll_pos_ = self->app_scroll_target_ +
        (self->app_scroll_pos_ - self->app_scroll_target_) * factor;
    if (std::abs(self->app_scroll_pos_ - self->app_scroll_target_) <= kScrollSettleEpsilonPx)
      self->app_scroll_pos_ = self->app_scroll_target_;
    if (self->input_->pointer_focus() && !self->input_->drag_active)
      self->input_->update_hover_from_pointer();
  }
  {
    overview::StageGuard pg(overview::Stage::MaybeRefreshData);
    self->maybe_refresh_data();
  }
  {
    overview::StageGuard pg(overview::Stage::RevealTick);
    self->reveal_->tick();
  }
  {
    overview::StageGuard pg(overview::Stage::LiveStreamTick);
    if (self->capture_->live_stream_) self->capture_->live_stream_->tick();
  }
  // Note: app-grid icons no longer need frame-budgeted prewarming — the
  // painter enqueues them on the icon cache's background loader and draws
  // placeholders until the rasters land.
  self->repaint_all();

  if (self->show_apps_ && !self->apps_first_paint_logged_ && self->apps_open_req_ms_ != 0) {
    self->apps_first_paint_logged_ = true;
    debug_log("overview", "appgrid first frame committed %.2f ms after click",
              static_cast<double>(now_mono_ms() - self->apps_open_req_ms_));
    eh::icons::eh_icons_perf_log_reset("appgrid_first_paint");
  }

  overview::OverviewProfiler::instance().end_frame();

  const double frameMs = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - frameT0).count();
  if (frameMs > 30.0)
    debug_log("overview", "slow frame: %.2f ms (show_apps=%d, apps=%zu)",
              frameMs, self->show_apps_ ? 1 : 0, self->apps_.size());

  if (self->open_ || self->anim_.has_active())
    self->schedule_frame();
}

void Host::layer_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t w, uint32_t h) {
  auto& self = *static_cast<Host*>(data);

  // Check primary / backdrop surfaces
  if (surface == self.layer_ || surface == self.backdrop_layer_) {
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    const int oldW = self.w_;
    const int oldH = self.h_;
    if (w > 0) self.w_ = static_cast<int>(w);
    if (h > 0) self.h_ = static_cast<int>(h);
    if (self.w_ <= 0 || self.h_ <= 0) {
      if (self.dock_.primaryOutputWidthPx > 0) self.w_ = self.dock_.primaryOutputWidthPx;
      if (self.dock_.primaryOutputHeightPx > 0) self.h_ = self.dock_.primaryOutputHeightPx;
    }
    if (self.w_ != oldW || self.h_ != oldH) self.backdrop_dirty_ = true;
    self.recompute_layout();
    self.input_->update_hover_from_pointer();
    if (surface == self.backdrop_layer_)
      self.paint_backdrop();
    else if (surface == self.layer_)
      self.paint_content();
    if (self.wl_) wl_display_flush(self.wl_->display());
    return;
  }

  // Extra output surfaces — just update dimensions; the next repaint_all()
  // cycle will present content to them.
  for (auto& e : self.extra_outputs_) {
    if (surface == e->layer || surface == e->backdrop_layer) {
      zwlr_layer_surface_v1_ack_configure(surface, serial);
      if (w > 0) e->w = static_cast<int>(w);
      if (h > 0) e->h = static_cast<int>(h);
      if (e->w <= 0 || e->h <= 0) {
        if (self.dock_.primaryOutputWidthPx > 0) e->w = self.dock_.primaryOutputWidthPx;
        if (self.dock_.primaryOutputHeightPx > 0) e->h = self.dock_.primaryOutputHeightPx;
      }
      if (self.wl_) wl_display_flush(self.wl_->display());
      return;
    }
  }
}

void Host::layer_closed(void* data, zwlr_layer_surface_v1* surface) {
  auto& self = *static_cast<Host*>(data);
  if (surface == self.backdrop_layer_) {
    if (self.backdrop_viewport_) { wp_viewport_destroy(self.backdrop_viewport_); self.backdrop_viewport_ = nullptr; }
    if (self.backdrop_layer_) { zwlr_layer_surface_v1_destroy(self.backdrop_layer_); self.backdrop_layer_ = nullptr; }
    if (self.backdrop_surface_) { wl_surface_destroy(self.backdrop_surface_); self.backdrop_surface_ = nullptr; }
    self.backdrop_buf_.destroy();
    self.backdrop_dirty_ = true;
    return;
  }
  // Check extra outputs — a closed extra just gets removed without tearing
  // down the whole overview.
  for (auto it = self.extra_outputs_.begin(); it != self.extra_outputs_.end(); ++it) {
    auto& e = *it;
    if (surface == e->layer || surface == e->backdrop_layer) {
      if (e->vk_layer.valid()) e->vk_layer.destroy();
      if (e->layer) { zwlr_layer_surface_v1_destroy(e->layer); e->layer = nullptr; }
      if (e->surface) { wl_surface_destroy(e->surface); e->surface = nullptr; }
      if (e->backdrop_viewport) { wp_viewport_destroy(e->backdrop_viewport); e->backdrop_viewport = nullptr; }
      if (e->backdrop_layer) { zwlr_layer_surface_v1_destroy(e->backdrop_layer); e->backdrop_layer = nullptr; }
      if (e->backdrop_surface) { wl_surface_destroy(e->backdrop_surface); e->backdrop_surface = nullptr; }
      e->backdrop_buf.destroy();
      e->cpu_buf.destroy();
      self.extra_outputs_.erase(it);
      return;
    }
  }
  if (!self.open_ && !self.closing_) return;
  self.open_ = false;
  self.closing_ = false;
  self.reveal_->reset();
  if (self.capture_->live_stream_) self.capture_->live_stream_->stop();
  self.anim_.cancel_all();
  self.progress_ = 0.f;
  self.destroy_layer();
  if (self.wl_) wl_display_flush(self.wl_->display());
}

void Host::seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
  auto& self = *static_cast<Host*>(data);
  self.input_->on_seat_capabilities(seat, capabilities);
}

void Host::seat_name(void*, wl_seat*, const char*) {
}

void Host::pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                          wl_fixed_t surface_x, wl_fixed_t surface_y) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_pointer_enter(surface, wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
}

void Host::pointer_leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_pointer_leave();
}

void Host::pointer_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t surface_x, wl_fixed_t surface_y) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_pointer_motion(wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
}

void Host::pointer_button(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_pointer_button(button, state);
}

void Host::pointer_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
  auto& self = *static_cast<Host*>(data);
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
    self.input_->handle_axis(wl_fixed_to_double(value));
}

void Host::pointer_frame(void*, wl_pointer*) {
}

void Host::pointer_axis_source(void* data, wl_pointer*, uint32_t axis_source) {
  auto& self = *static_cast<Host*>(data);
  self.input_->axis_source_ = axis_source;
}

void Host::pointer_axis_stop(void* data, wl_pointer*, uint32_t, uint32_t axis) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_axis_stop(axis);
}

void Host::pointer_axis_discrete(void* data, wl_pointer*, uint32_t axis, int32_t discrete) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_axis_discrete(axis, discrete);
}

void Host::keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto& self = *static_cast<Host*>(data);
  self.input_->on_keymap(format, fd, size);
}

void Host::keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {
}

void Host::keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {
}

void Host::keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
  auto& self = *static_cast<Host*>(data);
  self.input_->handle_key(key, state);
}

void Host::keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  auto& self = *static_cast<Host*>(data);
  self.input_->on_keyboard_modifiers(mods_depressed, mods_latched, mods_locked, group);
}

void Host::keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {
}

Host::Host(DockApp& dock, std::unique_ptr<eh::wayland::WaylandConnection> wl)
    : dock_(dock), wl_(std::move(wl)) {
  input_ = std::make_unique<Input>(*this);
  capture_ = std::make_unique<Capture>(*this);
  actions_ = std::make_unique<Actions>(*this);
  reveal_ = std::make_unique<RevealPass>(*this);
}

Host::~Host() {
  close();
  input_.reset();
  actions_.reset();
  capture_.reset();
  reveal_.reset();
}

void Host::select_workspace(int wsIndex) { actions_->select_workspace(wsIndex); }
void Host::activate_window(int flatIdx) { actions_->activate_window(flatIdx); }
void Host::close_window(int flatIdx) { actions_->close_window(flatIdx); }
void Host::move_window_to_workspace(int flatIdx, int targetWs) { actions_->move_window_to_workspace(flatIdx, targetWs); }
void Host::move_window_to_new_workspace(int flatIdx) { actions_->move_window_to_new_workspace(flatIdx); }
void Host::swap_windows_in_place(int flatIdx, int targetFlatIdx) { actions_->swap_windows_in_place(flatIdx, targetFlatIdx); }

void Host::set_recapture() {
  capture_->recapture_pending = true;
  capture_->recapture_after_ms = now_mono_ms() + 1000;
}

void Host::maybe_refresh_data() {
  if (!open_) return;
  const uint64_t now = now_mono_ms();
  const bool hardActive = anim_.has_active() || input_->drag_active || input_->button_pressed || scroll_active();

  const auto& ov = eh::config::shell_config_snapshot().appearance;
  if (!hardActive && capture_->recapture_pending && now >= capture_->recapture_after_ms) {
    refresh_workspace_data();
    last_data_refresh_ms_ = now;
    data_dirty_ = false;
    capture_->recapture_pending = false;
    capture_->recapture_after_ms = 0;
    capture_->capture_workspaces(/*preserving=*/true);
    if (!frame_cb_) schedule_frame();
  }

  if (hardActive || has_hover_animation_active()) {
    return;
  }

  // Event-driven refresh: fire when data_dirty_ is set by on_hyprland_event()
  // with a short debounce to coalesce rapid-fire events.  Also keep a long
  // watchdog timer as a fallback for missed events.
  const bool eventReady = event_pending_ && now >= event_coalesce_deadline_ms_;
  const bool watchdogReady = (now - last_data_refresh_ms_ >= kWatchdogIntervalMs);
  if (!(data_dirty_ || eventReady || watchdogReady)) return;

  debug_log("overview", "maybe_refresh_data: refreshing (dirty=%d event=%d watchdog=%d)",
            data_dirty_, eventReady, watchdogReady);
  refresh_workspace_data();
  last_data_refresh_ms_ = now;
  data_dirty_ = false;
  event_pending_ = false;
  event_coalesce_deadline_ms_ = 0;
  if (input_->pointer_focus()) input_->update_hover_from_pointer();

  bool needsCapture = false;
  for (const auto& ws : workspaces_) {
    if (!ws.capture) { needsCapture = true; break; }
  }
  if (needsCapture) capture_->capture_workspaces();

  if (ov.overviewLiveUpdates) {
    capture_->sync_live_streams();
  } else {
    capture_->sync_snapshot_streams();
  }
}

void Host::refresh_workspace_data() {
  std::unordered_map<int, std::shared_ptr<const WorkspaceCapture>> prevCaptures;
  prevCaptures.reserve(workspaces_.size());
  for (const auto& ws : workspaces_) {
    if (ws.capture) prevCaptures.emplace(ws.id, ws.capture);
  }

  struct LiveThumb {
    bool valid = false;
    int w = 0, h = 0;
    std::vector<uint8_t> bgra;
  };
  std::unordered_map<std::string, LiveThumb> prevLive;
  for (const auto& ws : workspaces_) {
    for (const auto& w : ws.windows) {
      if (!w.liveValid) continue;
      LiveThumb t;
      t.valid = true;
      t.w = w.liveW;
      t.h = w.liveH;
      t.bgra = w.liveBgra;
      prevLive.emplace(w.addr, std::move(t));
    }
  }

  workspaces_.clear();
  bool ok = false;
  if (dock_.compositorKind == CompositorKind::Hyprland) ok = refresh_from_hyprland();
  if (!ok) {
    debug_log("overview", "refresh_workspace_data: hyprland refresh FAILED, using dock fallback");
    refresh_from_dock();
  }
  std::stable_sort(workspaces_.begin(), workspaces_.end(),
                   [](const OverviewWorkspace& a, const OverviewWorkspace& b) { return a.id < b.id; });
  {
    std::string s;
    for (const auto& ws : workspaces_)
      s += std::to_string(ws.id) + "=" + std::to_string(ws.windows.size()) + ", ";
    debug_log("overview", "refresh_workspace_data: %s", s.c_str());
  }
  for (auto& ws : workspaces_) {
    if (auto it = prevCaptures.find(ws.id); it != prevCaptures.end()) ws.capture = it->second;
    for (auto& w : ws.windows) {
      auto it = prevLive.find(w.addr);
      if (it == prevLive.end()) continue;
      w.liveValid = it->second.valid;
      w.liveW = it->second.w;
      w.liveH = it->second.h;
      w.liveBgra = it->second.bgra;
    }
  }

  for (auto& ws : workspaces_) {
    if (ws.overflow) continue;
    std::string wsSig;
    for (const auto& w : ws.windows) wsSig += w.addr + ',';
    auto it = capture_->last_ws_signatures_.find(ws.id);
    if (it != capture_->last_ws_signatures_.end() && it->second == wsSig) continue;
    capture_->last_ws_signatures_[ws.id] = std::move(wsSig);
    if (ws.capture) {
      debug_log("overview", "refresh_workspace_data: ws %d window set changed -> capture dropped", ws.id);
      ws.capture.reset();
    }
  }
  for (auto& ws : workspaces_) {
    if ((ws.windows.empty() || ws.overflow) && !ws.capture && capture_->desktop_capture_)
      ws.capture = capture_->desktop_capture_;
  }
  actions_->build_nav_windows();
  recompute_scroll_bounds();

  std::string sig;
  for (const auto& ws : workspaces_) {
    sig += std::to_string(ws.id) + ':';
    for (const auto& w : ws.windows) sig += w.addr + ',';
    sig += ';';
  }
  if (sig != capture_->last_data_signature_) {
    capture_->last_data_signature_ = sig;
    capture_->recapture_pending = true;
    capture_->recapture_after_ms = now_mono_ms() + 1000;
  }
}

bool Host::refresh_from_hyprland() {
  auto wsRaw = hyprland_ipc_request("j/workspaces");
  auto clRaw = hyprland_ipc_request("j/clients");
  if (!wsRaw || !clRaw) return false;

  nlohmann::json wsJson;
  nlohmann::json clJson;
  try {
    wsJson = nlohmann::json::parse(*wsRaw);
    clJson = nlohmann::json::parse(*clRaw);
  } catch (const nlohmann::json::parse_error&) {
    return false;
  }
  if (!wsJson.is_array() || !clJson.is_array()) return false;

  std::unordered_set<int> displayedWsIds;

  // Derive activeId from the "focused" field in j/workspaces instead of a
  // separate j/activeworkspace round-trip, saving one blocking IPC call.
  int activeId = -1;
  for (const auto& ws : wsJson) {
    if (ws.is_object() && ws.value("focused", false)) {
      activeId = ws.value("id", -1);
      break;
    }
  }

  // Monitor geometry rarely changes; cache it to avoid a third IPC call
  // every refresh cycle.  Re-fetch at most once per kMonCacheTtlMs.
  const uint64_t now = now_mono_ms();
  if (mon_cache_.empty() || now - mon_cache_ms_ >= kMonCacheTtlMs) {
    mon_cache_.clear();
    if (auto monRaw = hyprland_ipc_request("j/monitors"); monRaw) {
      try {
        nlohmann::json monJson = nlohmann::json::parse(*monRaw);
        if (monJson.is_array()) {
          for (const auto& m : monJson) {
            if (!m.is_object()) continue;
            MonitorGeom g;
            g.x = m.value("x", 0.0);
            g.y = m.value("y", 0.0);
            g.w = m.value("width", 0.0);
            g.h = m.value("height", 0.0);
            if (g.w > 0.0 && g.h > 0.0) mon_cache_[m.value("id", -1)] = g;
            if (auto aw = m.find("activeWorkspace"); aw != m.end() && aw->is_object()) {
              const int awId = aw->value("id", -1);
              if (awId > 0) displayedWsIds.insert(awId);
            }
          }
        }
      } catch (const nlohmann::json::parse_error&) {
      }
      mon_cache_ms_ = now;
    }
  } else {
    // Rebuild displayedWsIds from the workspace data when using cached monitors.
    // Use the "visible" field the compositor sets on the workspace shown on
    // each monitor — this matches the per-monitor activeWorkspace we'd get
    // from a fresh j/monitors query.
    if (displayedWsIds.empty()) {
      for (const auto& ws : wsJson) {
        if (!ws.is_object()) continue;
        if (ws.value("visible", false)) {
          const int wsId = ws.value("id", -1);
          if (wsId > 0) displayedWsIds.insert(wsId);
        }
      }
    }
  }

  struct ClientEntry {
    OverviewWindow win;
    int monId = -1;
  };
  struct WsContent {
    bool has = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
  };
  std::unordered_map<int, std::vector<ClientEntry>> byWs;
  std::unordered_map<int, WsContent> wsContent;
  for (auto& c : clJson) {
    if (!c.is_object()) continue;
    ClientEntry e;
    OverviewWindow& w = e.win;
    w.addr = c.value("address", std::string{});
    w.appId = c.value("class", std::string{});
    if (w.appId.empty()) w.appId = c.value("initialClass", std::string{});
    w.title = c.value("title", std::string{});
    w.floating = c.value("floating", false);
    if (!c.value("mapped", true)) continue;

    int wsId = -1;
    if (auto wit = c.find("workspace"); wit != c.end() && wit->is_object())
      wsId = wit->value("id", -1);
    if (wsId <= 0) continue;
    w.special = false;

    if (auto at = c.find("at"); at != c.end() && at->is_array() && at->size() >= 2) {
      w.x = (*at)[0].get<double>();
      w.y = (*at)[1].get<double>();
    }
    if (auto sz = c.find("size"); sz != c.end() && sz->is_array() && sz->size() >= 2) {
      w.w = (*sz)[0].get<double>();
      w.h = (*sz)[1].get<double>();
    }
    e.monId = c.value("monitor", -1);
    if (auto fh = c.find("focusHistoryID"); fh != c.end() && fh->is_number_integer() &&
        fh->get<int>() == 0)
      w.focused = true;

    if (w.w > 0.0 && w.h > 0.0) {
      WsContent& con = wsContent[wsId];
      if (!con.has) {
        con.has = true;
        con.minX = w.x;
        con.minY = w.y;
        con.maxX = w.x + w.w;
        con.maxY = w.y + w.h;
      } else {
        con.minX = std::min(con.minX, w.x);
        con.minY = std::min(con.minY, w.y);
        con.maxX = std::max(con.maxX, w.x + w.w);
        con.maxY = std::max(con.maxY, w.y + w.h);
      }
    }
    byWs[wsId].push_back(std::move(e));
  }

  std::unordered_map<int, bool> wsOverflow;
  std::unordered_map<int, int> wsMonId;
  for (const auto& [wsId, entries] : byWs) {
    const auto conIt = wsContent.find(wsId);
    if (conIt == wsContent.end() || !conIt->second.has) continue;
    const auto& con = conIt->second;
    int monId = -1;
    for (const auto& e : entries) {
      if (e.monId >= 0) {
        monId = e.monId;
        break;
      }
    }
    auto mit = mon_cache_.find(monId);
    if (mit == mon_cache_.end()) continue;
    const auto& g = mit->second;
    const bool overflow = con.maxX > g.x + g.w * (1.0 + kOverflowMargin) ||
                          con.minX < g.x - g.w * kOverflowMargin ||
                          con.maxY > g.y + g.h * (1.0 + kOverflowMargin) ||
                          con.minY < g.y - g.h * kOverflowMargin;
    wsOverflow[wsId] = overflow;
    wsMonId[wsId] = monId;
  }

  for (auto& [wsId, entries] : byWs) {
    const bool overflow = wsOverflow[wsId];
    for (auto& e : entries) {
      OverviewWindow& w = e.win;
      if (overflow) {
        const auto conIt = wsContent.find(wsId);
        if (conIt != wsContent.end() && conIt->second.has) {
          const auto& b = conIt->second;
          const double bw = b.maxX - b.minX;
          const double bh = b.maxY - b.minY;
          if (bw > 0.0 && bh > 0.0) {
            w.ww = std::clamp(w.w / bw, 0.0, 1.0);
            w.wh = std::clamp(w.h / bh, 0.0, 1.0);
            w.wx = std::clamp((w.x - b.minX) / bw, 0.0, 1.0);
            w.wy = std::clamp((w.y - b.minY) / bh, 0.0, 1.0);
          }
        }
      } else {
        if (auto mit = mon_cache_.find(e.monId); mit != mon_cache_.end()) {
          const auto& g = mit->second;
          w.ww = std::clamp(w.w / g.w, 0.0, 1.0);
          w.wh = std::clamp(w.h / g.h, 0.0, 1.0);
          w.wx = std::clamp((w.x - g.x) / g.w, 0.0, 1.0);
          w.wy = std::clamp((w.y - g.y) / g.h, 0.0, 1.0);
        }
      }
    }
  }

  for (auto& ws : wsJson) {
    if (!ws.is_object()) continue;
    const int id = ws.value("id", -1);
    if (id <= 0) continue;
    OverviewWorkspace w;
    w.id = id;
    std::string name = ws.value("name", std::string{});
    w.label = name.empty() ? std::to_string(id) : name;
    w.monitor = ws.value("monitor", std::string{});
    if (auto mit = mon_cache_.find(wsMonId[id]); mit != mon_cache_.end()) {
      w.monX = mit->second.x;
      w.monY = mit->second.y;
      w.monW = mit->second.w;
      w.monH = mit->second.h;
    }
    w.active = (id == activeId) || displayedWsIds.count(id) > 0 || ws.value("focused", false);
    w.occupied = ws.value("windows", 0) > 0;
    w.overflow = wsOverflow[id];
    auto it = byWs.find(id);
    if (it != byWs.end()) {
      w.windows.reserve(it->second.size());
      for (auto& e : it->second) w.windows.push_back(std::move(e.win));
      w.occupied = w.occupied || !w.windows.empty();
    }
    workspaces_.push_back(std::move(w));
  }

  // Apply the configured workspace count from the widget settings.
  // When max_slots > 0, show at least that many workspaces (padded with empty
  // placeholders for IDs the compositor doesn't have yet).  Real workspaces
  // beyond max_slots (e.g. one just created via the strip's add slot) are
  // always kept visible.
  const auto& sc = eh::config::shell_config_snapshot();
  const std::string wid = eh::config::workspaces_widget_instance_id(sc);
  const int maxSlots = eh::widgets::workspace_max_slots_from_settings(sc, wid);
  if (maxSlots > 0 && static_cast<int>(workspaces_.size()) != maxSlots) {
    std::sort(workspaces_.begin(), workspaces_.end(),
              [](const OverviewWorkspace& a, const OverviewWorkspace& b) { return a.id < b.id; });
    int lastId = maxSlots;
    for (const auto& ws : workspaces_) {
      if ((!ws.windows.empty() || ws.active) && ws.id > lastId) lastId = ws.id;
    }
    std::vector<OverviewWorkspace> result;
    result.reserve(static_cast<size_t>(lastId));
    size_t idx = 0;
    for (int id = 1; id <= lastId; ++id) {
      if (idx < workspaces_.size() && workspaces_[idx].id == id) {
        result.push_back(std::move(workspaces_[idx]));
        ++idx;
      } else {
        OverviewWorkspace ph{};
        ph.id = id;
        ph.label = std::to_string(id);
        result.push_back(std::move(ph));
      }
    }
    workspaces_ = std::move(result);
  }

  // In multi-monitor mode, build per-output workspace lists by filtering
  // workspaces to those assigned to each output's monitor name.
  for (auto& ep : extra_outputs_) {
    ep->workspaces.clear();
    for (const auto& ws : workspaces_) {
      if (ws.monitor == ep->name)
        ep->workspaces.push_back(ws);
    }
    // If no workspaces matched this output, show all (fallback for
    // workspaces that haven't been assigned a monitor yet).
    if (ep->workspaces.empty())
      ep->workspaces = workspaces_;
    // Restore scroll bounds for this output's workspace count.
    const double maxScroll = std::max(0.0,
        static_cast<double>(std::max(0, static_cast<int>(ep->workspaces.size()) - 1)) * layout_.pitch);
    ep->scroll_pos = std::clamp(ep->scroll_pos, 0.0, maxScroll);
    ep->scroll_target = std::clamp(ep->scroll_target, 0.0, maxScroll);
  }

  return !workspaces_.empty();
}

void Host::refresh_from_dock() {
  eh::widgets::workspace_strip_poll(dock_.workspaceStrip, dock_.workspaceStripLastPoll,
                                    dock_.compositorKind);

  int activeIdx = 0;
  for (size_t i = 0; i < dock_.workspaceStrip.size(); ++i) {
    if (dock_.workspaceStrip[i].active) {
      activeIdx = static_cast<int>(i);
      break;
    }
  }

  for (size_t i = 0; i < dock_.workspaceStrip.size(); ++i) {
    const auto& e = dock_.workspaceStrip[i];
    if (e.id <= 0) continue;
    OverviewWorkspace w;
    w.id = e.id;
    w.label = e.label.empty() ? std::to_string(e.id) : e.label;
    w.active = e.active;
    w.occupied = e.occupied;
    if (static_cast<int>(i) == activeIdx) {
      for (const auto& tl : dock_.toplevels.list()) {
        if (tl.closed) continue;
        OverviewWindow win;
        win.handle = tl.handle;
        win.appId = tl.appId;
        win.title = tl.title;
        win.focused = tl.activated;
        w.windows.push_back(std::move(win));
      }
      w.occupied = w.occupied || !w.windows.empty();
    }
    workspaces_.push_back(std::move(w));
  }
}

void Host::update_scroll(double dt) {
  if (scroll_pos_ == scroll_target_) return;
  const double factor = std::exp(-kScrollEaseRatePerSec * dt);
  scroll_pos_ = scroll_target_ + (scroll_pos_ - scroll_target_) * factor;
  if (std::abs(scroll_pos_ - scroll_target_) <= kScrollSettleEpsilonPx) scroll_pos_ = scroll_target_;
  selected_index_ = static_cast<int>(
      overview_selected_index(layout_, scroll_pos_, static_cast<int>(workspaces_.size())));
  if (input_->pointer_focus() && !input_->drag_active) input_->update_hover_from_pointer();
  if (input_->drag_active) {
    input_->drop_target_ws = pick_workspace_at(layout_, input_->ptr_x(), input_->ptr_y(), workspaces_, scroll_pos_);
    if (input_->drop_target_ws == input_->press_ws) input_->drop_target_ws = -1;
    input_->drop_target_win = -1;
    if (input_->drop_target_ws < 0 && input_->press_ws >= 0) {
      int flat = -1;
      if (pick_window_at(layout_, workspaces_, input_->ptr_x(), input_->ptr_y(), input_->press_ws, scroll_pos_, &flat) >= 0 &&
          flat >= 0 && flat != input_->press_win) {
        input_->drop_target_win = flat;
      }
    }
  }
}

bool Host::scroll_active() const noexcept {
  return std::abs(scroll_pos_ - scroll_target_) > kScrollSettleEpsilonPx;
}

void Host::snap_scroll() {
  const int sel = static_cast<int>(
      overview_selected_index(layout_, scroll_pos_, static_cast<int>(workspaces_.size())));
  scroll_target_ = static_cast<double>(sel) * layout_.pitch;
}

void Host::recompute_layout() {
  if (w_ <= 0 || h_ <= 0) {
    if (dock_.primaryOutputWidthPx > 0) w_ = dock_.primaryOutputWidthPx;
    if (dock_.primaryOutputHeightPx > 0) h_ = dock_.primaryOutputHeightPx;
  }
  if (w_ <= 0 || h_ <= 0) return;
  const double us = dock_ui_scale(dock_.settings);

  const auto& ov = eh::config::shell_config_snapshot().appearance;
  axis_ = (ov.overviewAxis == 0) ? OverviewAxis::Vertical : OverviewAxis::Horizontal;
  card_scale_ = static_cast<double>(std::clamp(ov.overviewCardScalePct, 20, 80)) / 100.0;
  card_gap_ = static_cast<double>(std::clamp(ov.overviewCardGapPx, 8, 80));
  scroll_event_delay_ms_ = std::clamp(ov.overviewScrollDelayMs, 50, 500);

  compute_overview_layout(layout_, static_cast<double>(w_), static_cast<double>(h_),
                          static_cast<int>(workspaces_.size()), us, axis_, card_scale_, card_gap_,
                          static_cast<double>(std::clamp(ov.overviewCloseBtnSizePx, 20, 60)),
                          static_cast<double>(std::clamp(ov.overviewSearchWidthPx, 200, 800)));
  if (show_apps_)
    compute_app_grid_layout(layout_.appGrid, static_cast<double>(w_), static_cast<double>(h_),
                            layout_.searchY, us, static_cast<int>(apps_.size()));
}

void Host::recompute_scroll_bounds() {
  const int n = static_cast<int>(workspaces_.size());
  const double maxScroll = layout_.pitch > 0.0 ? layout_.pitch * std::max(0, n - 1) : 0.0;
  scroll_pos_ = std::clamp(scroll_pos_, 0.0, maxScroll);
  scroll_target_ = std::clamp(scroll_target_, 0.0, maxScroll);
}

void Host::scroll_by(double delta, const wl_surface* surface) {
  // Find which output this surface belongs to.
  for (auto& ep : extra_outputs_) {
    if (ep->surface == surface || ep->backdrop_surface == surface) {
      ep->scroll_target += delta;
      const int n = static_cast<int>(ep->workspaces.size());
      const double maxScroll = layout_.pitch > 0.0 ? layout_.pitch * std::max(0, n - 1) : 0.0;
      ep->scroll_target = std::clamp(ep->scroll_target, 0.0, maxScroll);
      if (!frame_cb_) schedule_frame();
      return;
    }
  }
  // Primary output.
  scroll_target_ += delta;
  recompute_scroll_bounds();
}

bool Host::has_hover_animation_active() const {
  for (auto v : ws_hover_lifts_)
    if (v > 0.005f && v < 0.995f) return true;
  for (auto v : win_hover_lifts_)
    if (v > 0.005f && v < 0.995f) return true;
  if (input_->app_hover_lift > 0.005f && input_->app_hover_lift < 0.995f) return true;
  if (search_hover_lift_ > 0.005f && search_hover_lift_ < 0.995f) return true;
  return false;
}

bool Host::vulkan_enabled() const {
  return eh::config::shell_config_snapshot().renderer == eh::config::ShellRendererBackend::Vulkan;
}

bool Host::ensure_vk_context() {
  if (!vulkan_enabled()) return false;
  if (vk_unavailable_) return false;
  if (!vk_ctx_) vk_ctx_ = std::make_shared<eh::wayland::VulkanDisplayContext>();
  if (!vk_ctx_->valid() && !vk_ctx_->init(wl_->display())) {
    debug_log("overview", "own Vulkan context init failed; presenting via shm");
    vk_unavailable_ = true;
    vk_ctx_.reset();
    return false;
  }
  return true;
}

void Host::paint_backdrop() {
  if (!backdrop_dirty_) return;
  if (!open_ || !backdrop_surface_ || w_ <= 0 || h_ <= 0) return;
  if (backdrop_buf_.busy()) return;
  if (!wl_ || !wl_->shm()) return;
  int bufW = w_;
  int bufH = h_;
  if (backdrop_viewport_) {
    constexpr double kBufScale = 2.0;
    bufW = std::max(1, static_cast<int>(w_ / kBufScale));
    bufH = std::max(1, static_cast<int>(h_ / kBufScale));
  }
  if (!backdrop_buf_.ensure(wl_->shm(), kOverviewBackdropNamespace, bufW, bufH)) return;

  cairo_t* cr = backdrop_buf_.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  if (backdrop_viewport_) {
    cairo_scale(cr, static_cast<double>(bufW) / w_, static_cast<double>(bufH) / h_);
  }

  const double dw = static_cast<double>(w_);
  const double dh = static_cast<double>(h_);

  {
    const double dockH = static_cast<double>(dock_.dockHeight) + kDockSpacingPx;
    const double dockY = dh - dockH;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_rectangle(cr, 0, dockY, dw, dockH);
    cairo_fill(cr);
  }

  {
    const auto& tb = eh::config::shell_config_snapshot().taskbar;
    if (tb.enabled) {
      const double tbExtra = tb.widthMode == 0 ? static_cast<double>(tb.floatingAmount) : 0.0;
      const double tbH = static_cast<double>(tb.height) + tbExtra + kTaskbarSpacingPx;
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      if (tb.positionTop) {
        cairo_rectangle(cr, 0, 0, dw, tbH);
      } else {
        cairo_rectangle(cr, 0, dh - tbH, dw, tbH);
      }
      cairo_fill(cr);
    }
  }

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_restore(cr);

  cairo_surface_flush(backdrop_buf_.cairo_surface());
  if (backdrop_viewport_) wp_viewport_set_destination(backdrop_viewport_, w_, h_);
  wl_surface_attach(backdrop_surface_, backdrop_buf_.wl(), 0, 0);
  wl_surface_damage_buffer(backdrop_surface_, 0, 0, bufW, bufH);
  backdrop_buf_.mark_busy();

  if (wl_->compositor()) {
    wl_region* rgn = wl_compositor_create_region(wl_->compositor());
    if (rgn) {
      wl_region_add(rgn, 0, 0, w_, h_);
      const int dockIH = dock_.dockHeight + static_cast<int>(kDockSpacingPx);
      const int dockIY = h_ - dockIH;
      wl_region_subtract(rgn, 0, dockIY, w_, dockIH);
      const auto& tb2 = eh::config::shell_config_snapshot().taskbar;
      if (tb2.enabled) {
        const int tbExtraI = tb2.widthMode == 0 ? tb2.floatingAmount : 0;
        const int tbIH = tb2.height + tbExtraI + 12;
        if (tb2.positionTop) {
          wl_region_subtract(rgn, 0, 0, w_, tbIH);
        } else {
          wl_region_subtract(rgn, 0, h_ - tbIH, w_, tbIH);
        }
      }
      wl_surface_set_input_region(backdrop_surface_, rgn);
      wl_region_destroy(rgn);
    }
  }

  wl_surface_commit(backdrop_surface_);

  // Paint extra output backdrops (same dimmed overlay, no dock carving)
  for (auto& ep : extra_outputs_) {
    auto& e = *ep;
    if (!e.backdrop_surface || e.w <= 0 || e.h <= 0) continue;
    int ebufW = e.w;
    int ebufH = e.h;
    if (e.backdrop_viewport) {
      constexpr double kBufScale = 2.0;
      ebufW = std::max(1, static_cast<int>(e.w / kBufScale));
      ebufH = std::max(1, static_cast<int>(e.h / kBufScale));
    }
    if (!e.backdrop_buf.ensure(wl_->shm(), kOverviewBackdropNamespace, ebufW, ebufH)) continue;
    cairo_t* cr2 = e.backdrop_buf.cairo();
    cairo_save(cr2);
    cairo_set_operator(cr2, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr2);
    cairo_set_operator(cr2, CAIRO_OPERATOR_OVER);
    if (e.backdrop_viewport)
      cairo_scale(cr2, static_cast<double>(ebufW) / e.w, static_cast<double>(ebufH) / e.h);
    cairo_restore(cr2);
    cairo_surface_flush(e.backdrop_buf.cairo_surface());
    if (e.backdrop_viewport) wp_viewport_set_destination(e.backdrop_viewport, e.w, e.h);
    wl_surface_attach(e.backdrop_surface, e.backdrop_buf.wl(), 0, 0);
    wl_surface_damage_buffer(e.backdrop_surface, 0, 0, ebufW, ebufH);
    e.backdrop_buf.mark_busy();
    wl_surface_commit(e.backdrop_surface);
  }

  backdrop_dirty_ = false;
}

void Host::paint_content() {
  if (!open_ || !surface_ || w_ <= 0 || h_ <= 0) return;
  if (!wl_ || !wl_->compositor()) return;

  // Presentation is independent of the dock: use the overview's own Vulkan
  // context when enabled, otherwise fall back to shm.  A Vulkan layer/surface
  // creation failure must never stop the overview from painting.
  const bool present_vk = ensure_vk_context();
  if (present_vk && !vk_layer_.valid())
    (void)vk_layer_.create(*vk_ctx_, wl_->display(), surface_, w_, h_);

  if (!cpu_buf_.ensure(w_, h_)) return;

  {
    const auto& sc = eh::config::shell_config_snapshot();
    const auto mc = eh::config::derived_chrome_colors(sc.appearance);
    colors_.bgR = mc.panelFillR; colors_.bgG = mc.panelFillG; colors_.bgB = mc.panelFillB;
    colors_.searchBgR = mc.drawerDimR; colors_.searchBgG = mc.drawerDimG; colors_.searchBgB = mc.drawerDimB;
    colors_.thumbBgR = mc.drawerDimR * 1.3; colors_.thumbBgG = mc.drawerDimG * 1.3; colors_.thumbBgB = mc.drawerDimB * 1.3;
    colors_.thumbActiveR = mc.dockFillR; colors_.thumbActiveG = mc.dockFillG; colors_.thumbActiveB = mc.dockFillB;
    colors_.thumbActiveBorderR = mc.accentR; colors_.thumbActiveBorderG = mc.accentG; colors_.thumbActiveBorderB = mc.accentB;
    colors_.wsBgR = mc.panelFillR * 1.15; colors_.wsBgG = mc.panelFillG * 1.15; colors_.wsBgB = mc.panelFillB * 1.15;
    colors_.glassBgR = mc.panelFillR * 0.35; colors_.glassBgG = mc.panelFillG * 0.35; colors_.glassBgB = mc.panelFillB * 0.35;
    colors_.winBgR = mc.drawerDimR; colors_.winBgG = mc.drawerDimG; colors_.winBgB = mc.drawerDimB;
    colors_.winTitleBgR = mc.panelFillR * 0.9; colors_.winTitleBgG = mc.panelFillG * 0.9; colors_.winTitleBgB = mc.panelFillB * 0.9;
    colors_.closeBtnBgR = mc.outlineR; colors_.closeBtnBgG = mc.outlineG; colors_.closeBtnBgB = mc.outlineB;
    colors_.dashBgR = mc.panelFillR * 1.1; colors_.dashBgG = mc.panelFillG * 1.1; colors_.dashBgB = mc.panelFillB * 1.1;
    colors_.accentR = mc.accentR; colors_.accentG = mc.accentG; colors_.accentB = mc.accentB;
    colors_.fgR = mc.textR; colors_.fgG = mc.textG; colors_.fgB = mc.textB;
    colors_.dimFgR = mc.outlineR; colors_.dimFgG = mc.outlineG; colors_.dimFgB = mc.outlineB;
  }

  {
    overview::StageGuard pg(overview::Stage::RecomputeLayout);
    recompute_layout();
  }

  constexpr float kHoverLerp = 0.18f;
  constexpr float kLiftSnap = 0.005f;

  {
    overview::StageGuard pg(overview::Stage::HoverUpdates);
  ws_hover_lifts_.resize(workspaces_.size(), 0.f);
  for (size_t i = 0; i < ws_hover_lifts_.size(); ++i) {
    float t = (static_cast<int>(i) == input_->hovered_ws) ? 1.f : 0.f;
    ws_hover_lifts_[i] += (t - ws_hover_lifts_[i]) * kHoverLerp;
    if (std::abs(ws_hover_lifts_[i] - t) < kLiftSnap) ws_hover_lifts_[i] = t;
  }

  win_hover_lifts_.resize(actions_->nav_windows().size(), 0.f);
  for (size_t i = 0; i < win_hover_lifts_.size(); ++i) {
    float t = (static_cast<int>(i) == input_->hovered_win) ? 1.f : 0.f;
    win_hover_lifts_[i] += (t - win_hover_lifts_[i]) * kHoverLerp;
    if (std::abs(win_hover_lifts_[i] - t) < kLiftSnap) win_hover_lifts_[i] = t;
  }

  {
    float t = input_->hovered_search ? 1.f : 0.f;
    search_hover_lift_ += (t - search_hover_lift_) * kHoverLerp;
    if (std::abs(search_hover_lift_ - t) < kLiftSnap) search_hover_lift_ = t;
  }
  } // HoverUpdates

  cairo_t* cr = cpu_buf_.cairo();
  {
    overview::StageGuard pg(overview::Stage::CairoClear);
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  }

  {
    overview::StageGuard pg(overview::Stage::PaintSearchBar);
  paint_search_bar(cr, colors_, layout_, search_hover_lift_, progress_, search_query_);
  }

  if (show_apps_) {
    float appT = (input_->hovered_app >= 0) ? 1.f : 0.f;
    input_->app_hover_lift += (appT - input_->app_hover_lift) * 0.18f;
    if (std::abs(input_->app_hover_lift - appT) < 0.005f) input_->app_hover_lift = appT;

    {
      overview::StageGuard pg(overview::Stage::PaintAppGrid);
    paint_app_grid(cr, dock_, colors_, layout_, apps_, input_->hovered_app,
                   input_->app_hover_lift, progress_, app_scroll_pos_);
    }
  } else {
    {
      overview::StageGuard pg(overview::Stage::PaintWorkspaceCards);
    paint_workspace_cards(cr, dock_, colors_, layout_, workspaces_,
                          scroll_pos_, selected_index_,
                          input_->hovered_ws, input_->hovered_win, input_->close_hovered,
                          ws_hover_lifts_, win_hover_lifts_,
                          input_->drag_active ? input_->press_ws : -1,
                          input_->drag_active ? input_->press_win : -1,
                          input_->drag_active ? input_->ptr_x() - input_->drag_grab_dx : 0.0,
                          input_->drag_active ? input_->ptr_y() - input_->drag_grab_dy : 0.0,
                          input_->drop_target_ws,
                          progress_,
                          consume_cards_invalidated());
    }

    {
      overview::StageGuard pg(overview::Stage::PaintStrip);
    paint_quick_select_strip(cr, dock_, colors_, layout_.qs, layout_, workspaces_,
                             selected_index_, input_->hovered_ws, input_->hovered_qs, progress_);
    }

    {
      int ch = 0, cm = 0, wh = 0, wm = 0, sh = 0, sm = 0;
      overview::get_cache_stats(ch, cm, wh, wm, sh, sm);
      overview::OverviewProfiler::instance().set_cache_stats(ch, cm, wh, wm, sh, sm);
    }
  }

  cairo_restore(cr);

  {
    overview::StageGuard pg(overview::Stage::CairoFlush);
  cairo_surface_flush(cpu_buf_.cairo_surface());
  }
  bool presented = false;
  {
    overview::StageGuard pg(overview::Stage::VkPresent);
    presented = present_content_pixels(present_vk ? &vk_layer_ : nullptr, vk_ctx_.get(),
                                       cpu_present_buf_, wl_->shm(), surface_, w_, h_,
                                       cpu_buf_.data(), cpu_buf_.stride());
  }
  if (presented) {
    overview::StageGuard pg(overview::Stage::RegionCommit);
    wl_surface_damage_buffer(surface_, 0, 0, w_, h_);

    if (wl_->compositor()) {
      wl_region* rgn = wl_compositor_create_region(wl_->compositor());
      if (rgn) {
        wl_region_add(rgn, 0, 0, w_, h_);
        const int dockIH = dock_.dockHeight + static_cast<int>(kDockSpacingPx);
        const int dockIY = h_ - dockIH;
        wl_region_subtract(rgn, 0, dockIY, w_, dockIH);
        const auto& tb = eh::config::shell_config_snapshot().taskbar;
        if (tb.enabled) {
          const int tbExtraI = tb.widthMode == 0 ? tb.floatingAmount : 0;
          const int tbIH = tb.height + tbExtraI + static_cast<int>(kTaskbarSpacingPx);
          if (tb.positionTop) {
            wl_region_subtract(rgn, 0, 0, w_, tbIH);
          } else {
            wl_region_subtract(rgn, 0, h_ - tbIH, w_, tbIH);
          }
        }
        wl_surface_set_input_region(surface_, rgn);
        wl_region_destroy(rgn);
      }
    }

    wl_surface_commit(surface_);
  }

  // Present per-output content to all extra output surfaces.
  for (auto& ep : extra_outputs_) {
    paint_extra_content(*ep, present_vk);
  }
}

void Host::paint_extra_content(OverviewOutput& e, bool use_vk) {
  if (!open_ || !e.surface || e.w <= 0 || e.h <= 0) return;
  if (!wl_ || !wl_->compositor()) return;

  if (use_vk && !e.vk_layer.valid()) {
    if (!e.vk_layer.create(*vk_ctx_, wl_->display(), e.surface, e.w, e.h)) return;
  }

  // Fast path: if this output has the same dimensions as the primary and the
  // same workspace list, just present the already-rendered primary pixels.
  const bool sameDims = (e.w == w_ && e.h == h_);
  const bool sameWs = (e.workspaces.size() == workspaces_.size());
  if (sameDims && sameWs && cpu_buf_.data()) {
    if (present_content_pixels(use_vk ? &e.vk_layer : nullptr, vk_ctx_.get(), e.cpu_buf,
                               wl_->shm(), e.surface, e.w, e.h,
                               cpu_buf_.data(), cpu_buf_.stride()))
      wl_surface_commit(e.surface);
    return;
  }

  // Slow path: different dimensions or workspace list — re-render.
  if (!cpu_buf_.ensure(e.w, e.h)) return;

  const int savedW = w_;
  const int savedH = h_;
  auto savedWs = std::move(workspaces_);
  const double savedScroll = scroll_pos_;
  const double savedScrollTarget = scroll_target_;
  const int savedSelected = selected_index_;

  w_ = e.w;
  h_ = e.h;
  workspaces_ = e.workspaces;
  scroll_pos_ = e.scroll_pos;
  scroll_target_ = e.scroll_target;
  selected_index_ = e.selected_index;
  recompute_layout();

  {
    cairo_t* cr = cpu_buf_.cairo();
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    std::vector<float> tmpWsLifts(workspaces_.size(), 0.f);
    std::vector<float> tmpWinLifts;
    paint_workspace_cards(cr, dock_, colors_, layout_, workspaces_,
                          scroll_pos_, selected_index_,
                          -1, -1, false,
                          tmpWsLifts, tmpWinLifts,
                          -1, -1, 0.0, 0.0, -1, progress_,
                          false);
    paint_quick_select_strip(cr, dock_, colors_, layout_.qs, layout_, workspaces_,
                             selected_index_, -1, -1, 0.0f);

    cairo_restore(cr);
    cairo_surface_flush(cpu_buf_.cairo_surface());
  }

  e.scroll_pos = scroll_pos_;
  e.scroll_target = scroll_target_;
  e.selected_index = selected_index_;

  // Present with the extra's dimensions, not the restored primary's.
  if (present_content_pixels(use_vk ? &e.vk_layer : nullptr, vk_ctx_.get(), e.cpu_buf,
                             wl_->shm(), e.surface, e.w, e.h,
                             cpu_buf_.data(), cpu_buf_.stride()))
    wl_surface_commit(e.surface);

  // Restore primary state.
  w_ = savedW;
  h_ = savedH;
  workspaces_ = std::move(savedWs);
  scroll_pos_ = savedScroll;
  scroll_target_ = savedScrollTarget;
  selected_index_ = savedSelected;
  recompute_layout();
}

int Host::count_painted_windows() const {
  int n = 0;
  for (const auto& ws : workspaces_)
    for (const auto& w : ws.windows)
      if (!w.special) ++n;
  return n;
}

void Host::repaint_all() {
  paint_backdrop();
  paint_content();
}

void Host::prewarm_caches() {
  if (w_ <= 0 || h_ <= 0) return;

  {
    const auto& sc = eh::config::shell_config_snapshot();
    const auto mc = eh::config::derived_chrome_colors(sc.appearance);
    colors_.bgR = mc.panelFillR; colors_.bgG = mc.panelFillG; colors_.bgB = mc.panelFillB;
    colors_.searchBgR = mc.drawerDimR; colors_.searchBgG = mc.drawerDimG; colors_.searchBgB = mc.drawerDimB;
    colors_.thumbBgR = mc.drawerDimR * 1.3; colors_.thumbBgG = mc.drawerDimG * 1.3; colors_.thumbBgB = mc.drawerDimB * 1.3;
    colors_.thumbActiveR = mc.dockFillR; colors_.thumbActiveG = mc.dockFillG; colors_.thumbActiveB = mc.dockFillB;
    colors_.thumbActiveBorderR = mc.accentR; colors_.thumbActiveBorderG = mc.accentG; colors_.thumbActiveBorderB = mc.accentB;
    colors_.wsBgR = mc.panelFillR * 1.15; colors_.wsBgG = mc.panelFillG * 1.15; colors_.wsBgB = mc.panelFillB * 1.15;
    colors_.glassBgR = mc.panelFillR * 0.35; colors_.glassBgG = mc.panelFillG * 0.35; colors_.glassBgB = mc.panelFillB * 0.35;
    colors_.winBgR = mc.drawerDimR; colors_.winBgG = mc.drawerDimG; colors_.winBgB = mc.drawerDimB;
    colors_.winTitleBgR = mc.panelFillR * 0.9; colors_.winTitleBgG = mc.panelFillG * 0.9; colors_.winTitleBgB = mc.panelFillB * 0.9;
    colors_.closeBtnBgR = mc.outlineR; colors_.closeBtnBgG = mc.outlineG; colors_.closeBtnBgB = mc.outlineB;
    colors_.dashBgR = mc.panelFillR * 1.1; colors_.dashBgG = mc.panelFillG * 1.1; colors_.dashBgB = mc.panelFillB * 1.1;
    colors_.accentR = mc.accentR; colors_.accentG = mc.accentG; colors_.accentB = mc.accentB;
    colors_.fgR = mc.textR; colors_.fgG = mc.textG; colors_.fgB = mc.textB;
    colors_.dimFgR = mc.outlineR; colors_.dimFgG = mc.outlineG; colors_.dimFgB = mc.outlineB;
  }

  recompute_layout();

  overview::prune_stale_card_disk_cache(workspaces_, colors_);

  cairo_surface_t* tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w_, h_);
  if (cairo_surface_status(tmp) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(tmp); return; }
  cairo_t* cr = cairo_create(tmp);

  ws_hover_lifts_.assign(workspaces_.size(), 0.f);
  win_hover_lifts_.assign(actions_->nav_windows().size(), 0.f);
  search_hover_lift_ = 0.f;

  paint_workspace_cards(cr, dock_, colors_, layout_, workspaces_,
                        scroll_pos_, selected_index_,
                        -1, -1, false,
                        ws_hover_lifts_, win_hover_lifts_,
                        -1, -1, 0.0, 0.0, -1, 0.0f);

  paint_quick_select_strip(cr, dock_, colors_, layout_.qs, layout_, workspaces_,
                           selected_index_, -1, -1, 0.0f);

  cairo_destroy(cr);
  cairo_surface_destroy(tmp);
}

void Host::schedule_frame() {
  if (!surface_ || frame_cb_) return;
  if (!wl_) return;
  frame_cb_ = wl_surface_frame(surface_);
  wl_callback_add_listener(frame_cb_, &kFrameListener, this);
  wl_surface_commit(surface_);
}

bool Host::create_layer() {
  if (surface_) return true;
  if (!wl_ || !wl_->compositor() || !wl_->layer_shell()) return false;

  // Read the config toggle for multi-monitor mode.
  {
    const auto& ov = eh::config::shell_config_snapshot().appearance;
    multi_monitor_ = ov.overviewMultiMonitor;
  }

  if (wl_->seat() && !input_->pointer_focus()) {
    wl_seat* seat = wl_->seat();
    wl_seat_add_listener(seat, &kSeatListener, this);
    input_->setup_seat(seat);
  }

  auto* const primaryOut = pick_primary_output(wl_.get());
  if (!primaryOut) return false;

  // Backdrop surface on the primary output.
  eh::wayland::LayerSurfaceConfig bcfg{};
  bcfg.nameSpace     = kOverviewBackdropNamespace;
  bcfg.layer         = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  bcfg.anchor        = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  bcfg.width         = 0;
  bcfg.height        = 0;
  bcfg.exclusiveZone = -1;
  bcfg.keyboard      = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* bsurf = nullptr;
  zwlr_layer_surface_v1* blayer = nullptr;
  if (!eh::wayland::create_layer_surface(wl_->compositor(), wl_->layer_shell(), primaryOut, bcfg,
                                          &kLayerListener, this, &bsurf, &blayer))
    return false;
  backdrop_surface_ = bsurf;
  backdrop_layer_   = blayer;
  if (wl_->viewporter())
    backdrop_viewport_ = wp_viewporter_get_viewport(wl_->viewporter(), backdrop_surface_);
  wl_surface_commit(backdrop_surface_);

  // Content surface on the primary output.
  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace     = kOverviewNamespace;
  cfg.layer         = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor        = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width         = 0;
  cfg.height        = 0;
  cfg.exclusiveZone = -1;
  cfg.keyboard      = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;

  wl_surface* surf = nullptr;
  zwlr_layer_surface_v1* layer = nullptr;
  if (!eh::wayland::create_layer_surface(wl_->compositor(), wl_->layer_shell(), primaryOut, cfg,
                                          &kLayerListener, this, &surf, &layer)) {
    destroy_layer();
    return false;
  }
  surface_ = surf;
  layer_   = layer;

  wl_surface_commit(surface_);
  if (wl_->display()) wl_display_roundtrip(wl_->display());

  // Extra outputs in multi-monitor mode.
  if (multi_monitor_) {
    const auto allOutputs = wl_->logical_output_bounds();
    for (const auto& ob : allOutputs) {
      if (ob.output == primaryOut) continue;
      auto eo = std::make_unique<OverviewOutput>();
      eo->output = ob.output;
      eo->name = ob.name;

      // Backdrop
      wl_surface* ebsurf = nullptr;
      zwlr_layer_surface_v1* eblayer = nullptr;
      if (eh::wayland::create_layer_surface(wl_->compositor(), wl_->layer_shell(), ob.output, bcfg,
                                             &kLayerListener, this, &ebsurf, &eblayer)) {
        eo->backdrop_surface = ebsurf;
        eo->backdrop_layer   = eblayer;
        if (wl_->viewporter())
          eo->backdrop_viewport = wp_viewporter_get_viewport(wl_->viewporter(), eo->backdrop_surface);
        wl_surface_commit(eo->backdrop_surface);
      }

      // Content
      wl_surface* esurf = nullptr;
      zwlr_layer_surface_v1* elayer = nullptr;
      if (eh::wayland::create_layer_surface(wl_->compositor(), wl_->layer_shell(), ob.output, cfg,
                                             &kLayerListener, this, &esurf, &elayer)) {
        eo->surface = esurf;
        eo->layer   = elayer;
        wl_surface_commit(eo->surface);
      }

      if (eo->surface || eo->backdrop_surface)
        extra_outputs_.push_back(std::move(eo));
    }
    if (wl_->display()) wl_display_roundtrip(wl_->display());
  }

  return true;
}

void Host::destroy_layer() {
  disconnect_hyprland_events();
  anim_.cancel_all();
  ws_hover_lifts_.clear();
  win_hover_lifts_.clear();
  input_->hovered_search = false;
  search_hover_lift_ = 0.f;
  input_->hovered_ws = -1;
  input_->hovered_win = -1;
  input_->hovered_qs = -1;
  input_->close_hovered = false;
  input_->button_pressed = false;
  input_->drag_active = false;
  input_->drag_auto_scroll_dir = 0;
  input_->press_ws = -1;
  input_->press_win = -1;
  input_->press_app_idx = -1;
  input_->drop_target_ws = -1;
  input_->drop_target_win = -1;
  input_->drop_target_add = false;
  input_->trackpad_settle_until_ms_ = 0;
  backdrop_dirty_ = false;
  clear_extra_outputs();
  if (frame_cb_) { wl_callback_destroy(frame_cb_); frame_cb_ = nullptr; }
  vk_layer_.destroy();
  cpu_buf_.destroy();
  if (layer_) { zwlr_layer_surface_v1_destroy(layer_); layer_ = nullptr; }
  if (surface_) { wl_surface_destroy(surface_); surface_ = nullptr; }
  backdrop_buf_.destroy();
  cpu_present_buf_.destroy();
  if (backdrop_viewport_) { wp_viewport_destroy(backdrop_viewport_); backdrop_viewport_ = nullptr; }
  if (backdrop_layer_) { zwlr_layer_surface_v1_destroy(backdrop_layer_); backdrop_layer_ = nullptr; }
  if (backdrop_surface_) { wl_surface_destroy(backdrop_surface_); backdrop_surface_ = nullptr; }
  w_ = h_ = 0;
}

void Host::clear_extra_outputs() {
  for (auto& ep : extra_outputs_) {
    auto& e = *ep;
    if (e.vk_layer.valid()) e.vk_layer.destroy();
    if (e.layer) { zwlr_layer_surface_v1_destroy(e.layer); e.layer = nullptr; }
    if (e.surface) { wl_surface_destroy(e.surface); e.surface = nullptr; }
    if (e.backdrop_viewport) { wp_viewport_destroy(e.backdrop_viewport); e.backdrop_viewport = nullptr; }
    if (e.backdrop_layer) { zwlr_layer_surface_v1_destroy(e.backdrop_layer); e.backdrop_layer = nullptr; }
    if (e.backdrop_surface) { wl_surface_destroy(e.backdrop_surface); e.backdrop_surface = nullptr; }
    e.backdrop_buf.destroy();
    e.cpu_buf.destroy();
  }
  extra_outputs_.clear();
}

void Host::detach_vk() noexcept {
  vk_layer_.detach();
  for (auto& ep : extra_outputs_)
    ep->vk_layer.detach();
}

void Host::refresh_app_list() {
  const auto t0 = std::chrono::steady_clock::now();
  apps_.clear();
  eh_app_drawer_menu_query(search_query_, &apps_);
  app_scroll_pos_ = 0.0;
  app_scroll_target_ = 0.0;
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  debug_log("overview", "app query \"%s\": %zu hits in %.2f ms",
            search_query_.c_str(), apps_.size(), ms);
}

void Host::scroll_app_grid(double delta) {
  const auto& g = layout_.appGrid;
  if (!show_apps_ || g.scrollMax <= 0.0) return;
  const double next = std::clamp(app_scroll_target_ + delta, 0.0, g.scrollMax);
  if (next == app_scroll_target_) return;
  app_scroll_target_ = next;
  if (!frame_cb_) schedule_frame();
}

void Host::make_app_visible(int idx) {
  const auto& g = layout_.appGrid;
  if (!show_apps_ || idx < 0 || g.cols <= 0 || g.rows <= 0 || g.scrollMax <= 0.0) return;
  const double viewH = static_cast<double>(g.rows) * g.cellH;
  const double top = static_cast<double>(idx / g.cols) * g.cellH;
  const double bottom = top + g.cellH;
  if (top < app_scroll_target_)
    app_scroll_target_ = top;
  else if (bottom > app_scroll_target_ + viewH)
    app_scroll_target_ = bottom - viewH;
  app_scroll_target_ = std::clamp(app_scroll_target_, 0.0, g.scrollMax);
  if (!frame_cb_) schedule_frame();
}

void Host::toggle_apps_mode() {
  if (!open_) return;
  const auto t0 = std::chrono::steady_clock::now();
  show_apps_ = !show_apps_;
  if (show_apps_) {
    search_query_.clear();
    refresh_app_list();
    input_->hovered_app = -1;
    input_->app_hover_lift = 0.f;
    apps_open_req_ms_ = now_mono_ms();
    apps_first_paint_logged_ = false;
  } else {
    input_->hovered_app = -1;
  }
  recompute_layout();
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  debug_log("overview", "toggle_apps_mode(%d): %.2f ms total, apps=%zu",
            show_apps_ ? 1 : 0, ms, apps_.size());
  if (!frame_cb_) schedule_frame();
}

void Host::close() {
  if (!open_) return;
  open_ = false;
  closing_ = true;
  reveal_->reset();

  if (capture_->live_stream_) capture_->live_stream_->stop();

  anim_.cancel_all();
  anim_.animate(
      1.f, 0.f, kCloseDurationMs, kCloseEasing,
      [this](float v) { progress_ = v; },
      [this]() {
        progress_ = 0.f;
        closing_ = false;
        destroy_layer();
        if (wl_) wl_display_flush(wl_->display());
        actions_->run_pending_activation();
      });
  schedule_frame();
}

void Host::toggle() {
  if (open_) { close(); return; }

  if (closing_) {
    // Reopened before the previous close animation finished. That animation
    // still has a pending destroy_layer() queued as its completion callback;
    // cancel_all() drops that callback without running it, so finish the
    // teardown here, synchronously, before touching surface_/layer_ again.
    // Without this, destroy_layer() fires later (on the old timer) and
    // destroys the surface the reopened overview is presenting on, killing
    // the frame loop (schedule_frame() bails once surface_ is null) with no
    // way to recover short of restarting the shell.
    anim_.cancel_all();
    destroy_layer();
    closing_ = false;
  }

  actions_->pending_activation_.reset();
  reveal_->reset();

  refresh_workspace_data();
  capture_->capture_workspaces();

  // Kick off per-window capture BEFORE creating the layer surface so that
  // snapshot frames have time to arrive via Wayland while we set up the
  // surface.  The roundtrip pumps pending events so that any frames the
  // compositor already queued are dispatched into handle_live_frame() before
  // we paint the first frame.
  prefetch_start_ms_ = now_mono_ms();
  const auto& ovEarly = eh::config::shell_config_snapshot().appearance;
  if (ovEarly.overviewLiveUpdates) {
    capture_->sync_live_streams();
  } else {
    capture_->sync_snapshot_streams();
  }
  if (wl_ && wl_->display()) wl_display_roundtrip(wl_->display());
  const uint64_t post_roundtrip_ms = now_mono_ms();

  // Count how many per-window snapshot frames arrived during the roundtrip.
  int snapshot_ready = 0, snapshot_total = 0;
  for (const auto& ws : workspaces_) {
    for (const auto& w : ws.windows) {
      if (w.special || w.addr.empty()) continue;
      ++snapshot_total;
      if (w.liveValid) ++snapshot_ready;
    }
  }
  debug_log("overview", "prefetch: roundtrip took %llu ms, %d/%d snapshot frames arrived",
            static_cast<unsigned long long>(post_roundtrip_ms - prefetch_start_ms_),
            snapshot_ready, snapshot_total);

  recompute_layout();

  int activeIdx = 0;
  for (int i = 0; i < static_cast<int>(workspaces_.size()); ++i) {
    if (workspaces_[static_cast<size_t>(i)].active) {
      activeIdx = i;
      break;
    }
  }
  scroll_pos_ = static_cast<double>(activeIdx) * layout_.pitch;
  scroll_target_ = scroll_pos_;
  selected_index_ = activeIdx;
  last_frame_ms_ = 0;

  if (!create_layer()) {
    debug_log("overview", "create_layer_surface failed");
    return;
  }

  open_ = true;
  progress_ = 0.f;
  backdrop_dirty_ = true;
  data_dirty_ = true;
  last_data_refresh_ms_ = 0;

  // Connect to the compositor event socket for event-driven state updates.
  connect_hyprland_events();

  anim_.animate(
      0.f, 1.f, kOpenDurationMs, kOpenEasing,
      [this](float v) { progress_ = v; },
      [this]() { progress_ = 1.f; });

  // Paint the first frame immediately so the Wayland surface already has
  // capture content committed before the compositor displays it.  Without
  // this the layer surface is committed empty and the user sees one frame
  // of transparent overlay before the snapshots appear.  We use a small
  // progress (~16ms into the 250ms animation) so the zoom and alpha match
  // what the first frame callback tick will produce.
  constexpr float kFirstFrameProgress = 16.f / kOpenDurationMs;
  progress_ = kFirstFrameProgress;
  prewarm_caches();
  (void)consume_cards_invalidated();
  repaint_all();
  progress_ = 0.f;

  // Final prefetch summary — how many windows had captures at first paint.
  int live_ready = 0, live_total = 0;
  for (const auto& ws : workspaces_) {
    for (const auto& w : ws.windows) {
      if (w.special || w.addr.empty()) continue;
      ++live_total;
      if (w.liveValid) ++live_ready;
    }
  }
  debug_log("overview", "prefetch: first paint at +%llu ms, %d/%d windows have snapshots",
            static_cast<unsigned long long>(now_mono_ms() - prefetch_start_ms_),
            live_ready, live_total);

  schedule_frame();
}

bool Host::connect_hyprland_events() {
  if (hypr_ev_fd_ >= 0) return true;
  auto* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (!sig || !sig[0]) {
    debug_log("overview", "hyprland events: HYPRLAND_INSTANCE_SIGNATURE not set");
    return false;
  }
  std::string dir;
  auto* rtDir = std::getenv("XDG_RUNTIME_DIR");
  if (rtDir && rtDir[0]) dir = std::string(rtDir) + "/hypr/" + sig;
  if (dir.empty()) dir = std::string("/tmp/hypr/") + sig;
  const std::string path = dir + "/.socket2.sock";

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    debug_log("overview", "hyprland events: socket() failed: %s", strerror(errno));
    return false;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); return false; }
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    debug_log("overview", "hyprland events: connect(%s) failed: %s", path.c_str(), strerror(errno));
    ::close(fd);
    return false;
  }
  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  hypr_ev_fd_ = fd;
  debug_log("overview", "hyprland events: connected to %s (fd=%d)", path.c_str(), fd);
  return true;
}

void Host::disconnect_hyprland_events() {
  if (hypr_ev_fd_ >= 0) { ::close(hypr_ev_fd_); hypr_ev_fd_ = -1; }
  hypr_ev_buf_.clear();
}

void Host::drain_hyprland_events() {
  if (hypr_ev_fd_ < 0) return;
  debug_log("overview", "hyprland events: drain called (fd=%d)", hypr_ev_fd_);
  std::array<char, 4096> buf;
  while (true) {
    auto n = ::recv(hypr_ev_fd_, buf.data(), buf.size(), MSG_DONTWAIT);
    if (n > 0) {
      debug_log("overview", "hyprland events: recv %zd bytes", (ssize_t)n);
      hypr_ev_buf_.insert(hypr_ev_buf_.end(), buf.data(), buf.data() + n);
      continue;
    }
    if (n == 0) {
      debug_log("overview", "hyprland events: socket closed by peer");
      disconnect_hyprland_events();
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    if (errno == EINTR) continue;
    debug_log("overview", "hyprland events: recv error: %s", strerror(errno));
    disconnect_hyprland_events();
    return;
  }
  // Parse completed lines: "event>>data\n"
  while (true) {
    auto nl = hypr_ev_buf_.find('\n');
    if (nl == std::string::npos) break;
    std::string line(hypr_ev_buf_.begin(), hypr_ev_buf_.begin() + static_cast<ptrdiff_t>(nl));
    hypr_ev_buf_.erase(hypr_ev_buf_.begin(), hypr_ev_buf_.begin() + static_cast<ptrdiff_t>(nl) + 1);
    if (line.empty()) continue;
    auto split = line.find(">>");
    if (split == std::string::npos) continue;
    on_hyprland_event(std::string_view(line).substr(0, split),
                      std::string_view(line).substr(split + 2));
  }
}

void Host::on_hyprland_event(std::string_view event, std::string_view /*data*/) {
  debug_log("overview", "hyprland event recv: %.*s (open=%d)", (int)event.size(), event.data(), open_);
  if (!open_) return;

  // Workspace lifecycle events
  if (event == "workspacev2" || event == "createworkspacev2" ||
      event == "destroyworkspacev2" || event == "renameworkspace" ||
      event == "moveworkspacev2") {
    debug_log("overview", "hyprland event: %.*s -> data_dirty", (int)event.size(), event.data());
    data_dirty_ = true;
  }
  // Window lifecycle events
  else if (event == "openwindow" || event == "closewindow" ||
           event == "movewindowv2" || event == "urgent") {
    debug_log("overview", "hyprland event: %.*s -> data_dirty", (int)event.size(), event.data());
    data_dirty_ = true;
  }
  // Focus change events — no-op when showing on all monitors.
  else if (event == "focusedmonv2") {
    // In multi-monitor mode the overview is already on all outputs, so a
    // monitor focus change only needs a data refresh.  In single-monitor
    // mode we ignore this event entirely (the overview stays on the primary).
    data_dirty_ = true;
  }
  // Config reload: force immediate refresh
  else if (event == "configreloaded") {
    debug_log("overview", "hyprland event: configreloaded -> full refresh");
    refresh_workspace_data();
    last_data_refresh_ms_ = now_mono_ms();
    data_dirty_ = false;
    event_pending_ = false;
    capture_->capture_workspaces();
    return;
  }
  else {
    return;  // Unknown event, ignore
  }

  // Coalesce rapid-fire events: set a deadline so maybe_refresh_data()
  // fires once after a short delay, batching multiple events together.
  if (!event_pending_) {
    event_pending_ = true;
    event_coalesce_deadline_ms_ = now_mono_ms() + kEventCoalesceMs;
  }
  // Schedule a frame so maybe_refresh_data() runs on the next tick
  if (!frame_cb_) schedule_frame();
}

} // namespace eh::shell::overview
