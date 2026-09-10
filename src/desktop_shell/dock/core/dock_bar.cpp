#include "desktop_shell/dock/core/dock_bar.h"

#define DOCK_DIAG(fmt, ...) do {} while(0)

#include "bootstrap/loop/poll_mux.hpp"
#include "wl/core/gpu_page_trim.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/shared/toplevel/toplevel_hooks.hpp"
#include "desktop_shell/common/os_logo/os_logo.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/bench/startup_trace.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/bench/bench_trace.hpp"
#include "desktop_shell/common/bench/bench_file.hpp"
#include "desktop_shell/common/fs/trash_state.hpp"
#include "desktop_shell/common/tray/tray_session_defer.hpp"
#include "services/tray/manager/tray_manager.hpp"
#include "desktop_shell/common/monitor/output_assign.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"
#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"
#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"
#include "desktop_shell/power_confirm/power_confirm.hpp"
#include "desktop_shell/shared/popup/dispatch/popup_dispatch.hpp"
#include "desktop_shell/dock/input/dock_position.hpp"

#include <cairo/cairo.h>
#if EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif
#include <wayland-client.h>

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <map>
#include <string>
#include <unordered_map>
#include <mutex>
#include <tuple>
#include <vector>
#include <filesystem>

#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <poll.h>
#include <cerrno>

#include <xkbcommon/xkbcommon.h>

#include <sys/timerfd.h>
#include <sys/eventfd.h>

#include <pango/pangocairo.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/dock_slot_hooks.hpp"
#include "desktop_shell/spotlight/search/spotlight_search.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "bootstrap/loop/main_loop.hpp"
#include "wl/core/protocols.hpp"
#include "wl/surface/vulkan_destruction_queue.hpp"
#include "desktop_shell/dock/paint/dock_raster_backend.hpp"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/unified/unified_wayland_registry.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/palette/color_hsv.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "desktop_shell/common/geom/rect_overlap.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/controlcenter/layout/control_center_anim.hpp"
#include "desktop_shell/dock/paint/dock_strip_geometry.hpp"
#include "desktop_shell/shared/widgets/workspace_strip.hpp"
#include "desktop_shell/spotlight/search/spotlight_query.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/shared/popup/buffer/buffer.hpp"
#include "desktop_shell/launchpad/host/launchpad_host.hpp"
#include "desktop_shell/shared/popup/caret/caret.hpp"
#include "desktop_shell/shared/popup/paint/finish.hpp"
#include "desktop_shell/shared/popup/paint/paint.hpp"
#include "desktop_shell/controlcenter/layout/control_center_panel_geometry.hpp"
#include "desktop_shell/controlcenter/input/control_center_hit.hpp"
#include "desktop_shell/dock/layout/dock_layout_shared.hpp"
#include "desktop_shell/dock/input/dock_pick.hpp"
#include "desktop_shell/dock/paint/dock_paint_widget_bar.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"
#include "services/tray/manager/tray.hpp"
#include "desktop_shell/dock/tooltip/dock_tooltip.hpp"
#include "desktop_shell/shared/popup/chrome/chrome.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/dock/input/dock_input_region.hpp"
#include "desktop_shell/dock/paint/dock_anim.hpp"
#include "desktop_shell/widgets/start_menu/start_menu_zone.hpp"
#include "desktop_shell/shared/core/app_launch.hpp"
#include "desktop_shell/osd/audio/osd_audio.hpp"
#include "desktop_shell/osd/brightness/osd_brightness.hpp"
#include "services/tray/icon/tray_stream_icon.hpp"
#include "desktop_shell/dock/widgets/dock_widget_tokens.hpp"


using eh::shell::paths::normalize_desktop_app_id;
using eh::shell::str::file_exists;
using eh::shell::str::split_colon_list;
using eh::shell::str::trim;
using eh::shell::dock::kControlCenterPopupH;
using eh::shell::dock::kControlCenterPopupW;
using eh::shell::dock::kSpotlightMaxRows;
using eh::shell::dock::kSpotlightPopupW;
using eh::shell::dock::kSpotlightRowPx;
using eh::shell::dock::kSpotlightSearchOuterH;
using eh::shell::dock::spotlight_popup_total_height;
using eh::shell::color::hsv_to_rgb;
using eh::shell::dock::dock_active_canvas_dims;
using eh::shell::dock::dock_pill_geometry;
using eh::shell::dock::dock_pill_geometry_dims;
using eh::shell::dock::strip_layout_x_to_surface;
using eh::shell::dock::strip_surface_x_to_layout;
using eh::shell::dock::widget_strip_h_scale;
using eh::shell::geom::rects_overlap;
using eh::shell::str::utf8_pop_back;
using eh::shell::dock::spotlight_pick_row_index;
using eh::shell::dock::spotlight_truncate_to_width;
using eh::shell::dock::destroy_popup_buffer;
using eh::shell::dock::dock_popup_destroy_caret_frame;
using eh::shell::dock::dock_popup_queue_followup_frame;

static constexpr double kDockHoverLiftMax = 5.0;

static const eh::icons::IconEntry* get_cached_tray_icon(DockApp& app, const std::string& iconName);

void popup_draw_surface(DockApp& app);
static int compute_desired_surface_width(DockApp& app);
static const DockOutputSlot* dock_output_slot_for(const DockApp& app, const wl_output* out);

static bool eh_layer_debug() {
  static const bool val = eh::debug_profile::env_bool("EH_DEBUG_LAYERS");
  return val;
}

bool eh_dock_settings_debug() {
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_SETTINGS_DEBUG") ? 1 : 0;
  return cached != 0;
}

static bool eh_dock_toplevel_debug() { return dock_foreign_toplevel_debug_enabled(); }

static bool eh_dock_fast_start() {
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  const char* e = std::getenv("EH_DOCK_FAST_START");
  cached = (!e || e[0] == '\0') ? 1 : (e[0] != '0' ? 1 : 0);
  return cached != 0;
}

bool eh_dock_pin_drag_perf_enabled() {
  static int cached = -1;
  if (cached >= 0) return cached != 0;
  cached = eh::debug_profile::env_bool("EH_DOCK_PIN_DRAG_PERF") ? 1 : 0;
  return cached != 0;
}

int eh_dock_fps_log_mode() {
  static int cached = -999;
  if (cached != -999) return cached;
  cached = eh::debug_profile::env_int("EH_DOCK_FPS", 1);
  return cached;
}

static void dock_rebind_layer_outputs(DockApp& app);
bool dock_pointer_on_any_dock_layer(const DockApp& app);
static void dock_sync_legacy_from_primary(DockApp& app);
static int compute_desired_surface_width_for_output(DockApp& app, wl_output* out);

static bool timespec_equal(const timespec& a, const timespec& b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

void launch_settings_app(DockApp& app) {
  MANGOWM_FN();
  if (app.launch_settings_override) {
    app.launch_settings_override();
    return;
  }
  std::fprintf(stderr,
               "[dock] Settings open requires EventHorizon (embedded); running without unified "
               "entry did not configure them.\n");
}

static const DockOutputSlot* dock_output_slot_for(const DockApp& app, const wl_output* out);
static int dock_max_strip_width_px(const DockApp& app);

const DockApp::Toplevel* dock_toplevel_by_serial(const DockApp& app, std::uint64_t serial) {
  MANGOWM_FN();
  if (!serial) return nullptr;
  for (const auto& tl : app.toplevels) {
    if (tl.serial == serial && tl.handle && !tl.closed) return &tl;
  }
  return nullptr;
}

void dock_pinned_merge_running_slot(DockApp& app, const std::vector<eh::shell::shared::RunningGroup>& runningGroups,
                                           const std::string& normalizedPin, std::uint64_t& chosenSerial, bool& anyActivated,
                                           std::string* iconIdOut) {
  dock_pin_identity_cache_refresh(app);
  const std::vector<std::string>* expanded = nullptr;
  if (auto it = app.dockPinIdentityKeys.find(normalizedPin); it != app.dockPinIdentityKeys.end()) expanded = &it->second;

  bool any = false;
  std::uint64_t fallbackSerial = 0;
  std::string fallbackIcon;
  chosenSerial = 0;
  for (const auto& rg : runningGroups) {
    bool idMatch = (rg.pinMatchKey == normalizedPin);
    if (!idMatch && expanded) {
      for (const auto& k : *expanded) {
        if (k == rg.pinMatchKey) {
          idMatch = true;
          break;
        }
      }
    }
    if (!idMatch) idMatch = pin_identity_same_resolved_desktop(normalizedPin, rg.pinMatchKey);
    if (!idMatch) continue;
    any = any || rg.anyActivated;
    if (dock_toplevel_by_serial(app, rg.chosenSerial)) {
      chosenSerial = rg.chosenSerial;
      if (iconIdOut && !rg.iconId.empty()) *iconIdOut = rg.iconId;
      anyActivated = any;
      return;
    }
    if (!fallbackSerial) {
      fallbackSerial = rg.chosenSerial;
      fallbackIcon = rg.iconId;
    }
  }
  if (!chosenSerial) chosenSerial = fallbackSerial;
  if (iconIdOut && !fallbackIcon.empty()) *iconIdOut = std::move(fallbackIcon);
  anyActivated = any;
}

static std::string dock_first_clock_widget_id(const DockSettings& s) {
  for (const auto& w : s.leftWidgets) {
    if (eh::config::widget_implementation_type(w) == "clock") return w;
  }
  for (const auto& w : s.centerWidgets) {
    if (eh::config::widget_implementation_type(w) == "clock") return w;
  }
  for (const auto& w : s.rightWidgets) {
    if (eh::config::widget_implementation_type(w) == "clock") return w;
  }
  return {};
}

static std::string dock_first_world_clock_widget_id(const DockSettings& s) {
  for (const auto& w : s.leftWidgets) {
    if (eh::config::widget_implementation_type(w) == "world_clock") return w;
  }
  for (const auto& w : s.centerWidgets) {
    if (eh::config::widget_implementation_type(w) == "world_clock") return w;
  }
  for (const auto& w : s.rightWidgets) {
    if (eh::config::widget_implementation_type(w) == "world_clock") return w;
  }
  return {};
}

static std::string dock_first_media_widget_id(const DockSettings& s) {
  for (const auto& w : s.leftWidgets) {
    if (eh::config::widget_implementation_type(w) == "media") return w;
  }
  for (const auto& w : s.centerWidgets) {
    if (eh::config::widget_implementation_type(w) == "media") return w;
  }
  for (const auto& w : s.rightWidgets) {
    if (eh::config::widget_implementation_type(w) == "media") return w;
  }
  return {};
}

static void dock_apply_poll_timer_interval(DockApp& app) {
  MANGOWM_FN();
  if (app.pollTimerFd < 0) return;
  itimerspec its{};
  if (app.settingsInotifyFd < 0) {
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 500 * 1000 * 1000;
    its.it_value = its.it_interval;
    (void)timerfd_settime(app.pollTimerFd, 0, &its, nullptr);
    return;
  }
  if (!dock_first_media_widget_id(app.settings).empty() || !dock_first_control_center_widget_id(app.settings).empty()) {
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 400 * 1000 * 1000;
  } else {
    its.it_interval.tv_sec = 1;
    its.it_interval.tv_nsec = 0;
  }
  its.it_value = its.it_interval;
  (void)timerfd_settime(app.pollTimerFd, 0, &its, nullptr);
}

// Media progress animation timer.
// While a track plays (progress border visible) and no marquee scroll is
// running, the dock repaints the media pill only every tick instead of
// scheduling vsync frames. A few px of dash movement per tick is
// imperceptible; continuous 30fps repaints are not free.

static int eh_dock_media_progress_tick_ms() {
  static const int cached = [] {
    if (const char* v = std::getenv("EH_DOCK_MEDIA_PROGRESS_MS")) {
      const int n = std::atoi(v);
      if (n >= 50 && n <= 5000) return n;
    }
    return 400;
  }();
  return cached;
}

static void dock_disarm_media_anim_timer(DockApp& app) {
  if (app.mediaAnimTimerFd < 0) return;
  itimerspec its{};
  (void)timerfd_settime(app.mediaAnimTimerFd, 0, &its, nullptr);
}

static void dock_arm_media_anim_timer(DockApp& app, int delay_ms) {
  if (app.mediaAnimTimerFd < 0) return;
  itimerspec its{};
  its.it_value.tv_sec = static_cast<time_t>(delay_ms / 1000);
  its.it_value.tv_nsec = static_cast<long>(delay_ms % 1000) * 1000000L;
  (void)timerfd_settime(app.mediaAnimTimerFd, 0, &its, nullptr);
}

void dock_handle_media_anim_timer(DockApp& app) {
  uint64_t expirations = 0;
  (void)read(app.mediaAnimTimerFd, &expirations, sizeof(expirations));
  if (!app.configured || app.dockLayers.empty()) return;

  // Same safety conditions as the frame_done media-only classification:
  // anything else animating means a full frame is due anyway; let those
  // paths drive this redraw.
  const bool safe_partial =
      !app.sizeDirty && !app.deferDockRedraw && !app.pendingRedraw && !app.shellAnim.has_active() &&
      !app.wsStripAnim.active && !app.popupOpen && !app.pinDragging && !app.pinDragCandidate &&
      app.pinDragKey.empty();
  app.nextFrameScope = safe_partial ? DockApp::DockFrameScope::MediaMarqueeOnly : DockApp::DockFrameScope::Full;
  dock_draw(app);
}


void maybe_log_layout(DockApp& app, const char* reason) {
  MANGOWM_FN();
  if (!eh_dock_settings_debug()) return;

  if (app.pinDragging) return;

  const double dx = app.dockRectX;
  const double dy = app.dockRectY;
  const double dw = app.dockRectW;
  const double dh = app.dockRectH;

  const double px = static_cast<double>(app.popupConfiguredX);
  const double py = static_cast<double>(app.popupConfiguredY);
  const double pw = static_cast<double>(app.popupConfiguredW);
  const double ph = static_cast<double>(app.popupConfiguredH);

  const bool haveDock = (dw > 0.0 && dh > 0.0);
  const bool havePopup = (app.popupOpen && pw > 0.0 && ph > 0.0);
 
  const bool overlap = (haveDock && havePopup) ? rects_overlap(dx, dy, dw, dh, px, py, pw, ph) : false;

  std::string snap;
  snap.reserve(256);
  snap += "dock=(" + std::to_string(dx) + "," + std::to_string(dy) + " " + std::to_string(dw) + "x" + std::to_string(dh) + ")";
  snap += " popup=(" + std::to_string(px) + "," + std::to_string(py) + " " + std::to_string(pw) + "x" + std::to_string(ph) + ")";
  snap += " popup_open=" + std::to_string(app.popupOpen ? 1 : 0);
  snap += " overlap=" + std::to_string(overlap ? 1 : 0);

  if (snap != app.lastLayoutSnapshot) {
    app.lastLayoutSnapshot = snap;
    std::cout << "[layout] " << reason << " " << snap << "\n";
  }
}

static void log_dbg(const char*, ...) {}

void popup_draw_surface(DockApp& app) {
  MANGOWM_FN();
  using BenchClock = std::chrono::steady_clock;
  const BenchClock::time_point bench_t0 = BenchClock::now();
  if (!app.popupOpen) { log_dbg("[dock-popup] draw_surface skip: !popupOpen\n"); return; }
  if (!app.popupSurface) { log_dbg("[dock-popup] draw_surface skip: !popupSurface\n"); return; }
  if (!app.popupLayerSurface) { log_dbg("[dock-popup] draw_surface skip: !popupLayerSurface\n"); return; }
  if (app.popupConfiguredW <= 0 || app.popupConfiguredH <= 0) {
    log_dbg("[dock-popup] draw_surface defer: popup layer NOT YET CONFIGURED (cfgW=%d cfgH=%d kind=%d popupW=%d popupH=%d)\n",
            app.popupConfiguredW, app.popupConfiguredH, static_cast<int>(app.popupKind),
            app.popupW, app.popupH);
    return;
  }
  log_dbg("[dock-popup] draw_surface: PROCEED cfgW=%d cfgH=%d kind=%d popupW=%d popupH=%d\n",
          app.popupConfiguredW, app.popupConfiguredH, static_cast<int>(app.popupKind),
          app.popupW, app.popupH);

  cairo_t* cr = nullptr;
  const bool popupCairoMode = app.dockRendererBackend != eh::config::ShellRendererBackend::Vulkan;
  if (popupCairoMode) {
    if (!eh::dock::ensure_popup_cairo_raster(app, app.popupW, app.popupH)) {
      log_dbg("[dock-popup] wl_shm raster unavailable; cannot draw popup.\n");
      return;
    }
    cr = eh::dock::popup_cairo_ctx(app);
  } else {
    if (!eh::dock::ensure_popup_vk_raster(app, app.popupW, app.popupH)) {
      log_dbg("[dock-popup] Vulkan raster unavailable; cannot draw popup.\n");
      return;
    }
    cr = app.popupGlRaster.cairo();
  }
  if (!cr) return;
  app.popupLastDrawMs = eh::shell::monotonic_ms();
  const BenchClock::time_point bench_t1 = BenchClock::now();

  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  const eh::config::ShellConfig& scPopupOv = eh::config::shell_config_snapshot();

  if (app.popupKind == DockApp::PopupKind::Spotlight) {
    dock_popup_paint_spotlight(app, cr, scPopupOv);
    cairo_restore(cr);
    popup_finish_draw(app, true, true, false);
    return;
  }
  if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
    const int idx = app.appMenuPowerConfirmIdx;
    const bool open = app.appMenuPowerConfirmOpen;
    const uint64_t now = eh::shell::monotonic_ms();
    const uint64_t elapsed = (app.appMenuPowerConfirmStartMs != 0) ? (now - app.appMenuPowerConfirmStartMs) : 0;
    const int remainingSec = (elapsed >= 60000) ? 0 : static_cast<int>(60 - elapsed / 1000);
    if (open && idx >= 1 && idx <= 3) {
      if (remainingSec <= 0) {
        eh::shell::dock::app_drawer::app_drawer_power_exec(app.compositorKind, idx);
        popup_close(app);
        wl_display_flush(app.display);
        cairo_restore(cr);
        return;
      }
      eh::power_confirm::paint(cr,
        static_cast<double>(app.popupW), static_cast<double>(app.popupH),
        idx,
        app.pointerX, app.pointerY,
        remainingSec,
        scPopupOv);
    }
    cairo_restore(cr);
    popup_finish_draw(app, true, true, false);
    return;
  }
  if (dock_popup_kind_uses_app_drawer_ui(app.popupKind)) {
    const BenchClock::time_point bench_t2 = BenchClock::now();
    dock_popup_paint_app_menu(app, cr, scPopupOv, true);
    cairo_restore(cr);
    const BenchClock::time_point bench_t3 = BenchClock::now();
    popup_finish_draw(app, true, true, false);
    const BenchClock::time_point bench_t4 = BenchClock::now();

    if (eh_menu_bench()) {
      static unsigned s_drawN = 0;
      ++s_drawN;
      const auto us_total = std::chrono::duration_cast<std::chrono::microseconds>(bench_t4 - bench_t0).count();
      const auto us_backend = std::chrono::duration_cast<std::chrono::microseconds>(bench_t1 - bench_t0).count();
      const auto us_clear = std::chrono::duration_cast<std::chrono::microseconds>(bench_t2 - bench_t1).count();
      const auto us_body = std::chrono::duration_cast<std::chrono::microseconds>(bench_t3 - bench_t2).count();
      const auto us_commit = std::chrono::duration_cast<std::chrono::microseconds>(bench_t4 - bench_t3).count();
      const double fps_inst = us_total > 0 ? 1000000.0 / static_cast<double>(us_total) : 0.0;
      char line[1024];
      snprintf(line, sizeof(line),
        "[dock-menu-bench] draw#%u %dx%d slot=vk backend=%ldus clear=%ldus body=%ldus commit=%ldus total=%ldus fps_inst=%.1f\n",
        s_drawN, app.popupW, app.popupH,
        static_cast<long>(us_backend), static_cast<long>(us_clear),
        static_cast<long>(us_body), static_cast<long>(us_commit),
        static_cast<long>(us_total), fps_inst);
      std::cerr << line;
      static FILE* s_log = nullptr;
      if (!s_log) {
        s_log = fopen("/tmp/eh_menu_bench.log", "w");
        if (s_log) {
          time_t now_t = time(nullptr);
          fprintf(s_log, "# Event Horizon Dock-Menu Bench  %s", ctime(&now_t));
          fprintf(s_log, "# draw# WxH slot backend clear body commit total fps_inst\n");
        }
      }
      if (s_log) { fputs(line, s_log); fflush(s_log); }
    }
    return;
  }
  if (app.popupKind == DockApp::PopupKind::ControlCenter) {
    const std::string ccId = dock_first_control_center_widget_id(app.settings);
    const std::string wid = ccId.empty() ? std::string("control_center") : ccId;
    control_center_popup_paint(app.ccState, cr, app.popupW, app.popupH, scPopupOv, app.mpris.get(), wid, app.icons, app.settings.pinnedApps);
    cairo_restore(cr);
    popup_finish_draw(app, true, true, true);
    return;
  }
  if (eh::shell::popup::popup_dispatch_paint(app, cr)) return;
  dock_popup_paint_context_menu(app, cr, scPopupOv);
  cairo_restore(cr);
  popup_finish_draw(app, true, false, false);
}

int dock_effective_icon_px(const DockSettings& s) {
  const double sc = dock_ui_scale(s);
  const int base = std::clamp(s.dockIconSize, 0, 50);
  const int v = static_cast<int>(std::lround(static_cast<double>(base) * sc));
  return std::clamp(v, 1, 96);
}
int dock_effective_gap_px(const DockSettings& s) {
  const double sc = dock_ui_scale(s);
  const int raw0 = std::clamp(s.dockIconSpacing, -50, 50);
  const int raw = static_cast<int>(std::lround(static_cast<double>(raw0) * sc));

  if (raw < 0) return std::clamp(raw, -50, -1);

  if (raw <= 1) return raw;
  return std::clamp(static_cast<int>(std::lround(static_cast<double>(raw) * 0.6)), 0, 50);
}

int dock_effective_bar_height_px(const DockSettings& s) {
  const int iconPx = dock_effective_icon_px(s);
  const int autoH = dock_height_for_icon_px(iconPx);
  if (s.dockBarFollowsIcons) return autoH;
  const int minH = std::clamp(iconPx + 4, 32, 200);
  return std::clamp(s.dockManualBarHeightPx, minH, 200);
}
static int dock_effective_bottom_gap_px(const DockSettings& s) { return std::max(0, std::min(25, s.dockBottomGap)); }
static int dock_effective_radius_px(const DockSettings& s) {
  const double sc = dock_ui_scale(s);
  const int base = std::clamp(s.dockRadius, 0, 50);
  const int v = static_cast<int>(std::lround(static_cast<double>(base) * sc));
  return std::clamp(v, 0, 76);
}

static void sync_dock_chrome(DockApp& app) {
  MANGOWM_FN();
  if (eh_startup_trace_enabled()) {
    static int sync_chrome_logs = 40;
    if (sync_chrome_logs > 0) {
      --sync_chrome_logs;
      EH_ST_TRACE(std::cerr << "dock sync_dock_chrome: enter configured=" << (app.configured ? 1 : 0) << " surface=" << static_cast<void*>(app.surface));
    }
  }
  const int newBottom = dock_effective_bottom_gap_px(app.settings);
  if (app.bottomGap != newBottom) app.bottomGap = newBottom;

  const int newH = dock_effective_bar_height_px(app.settings);
  if (app.dockHeight != newH) {
    app.dockHeight = newH;
    if (app.autoHide) {
      app.animTargetPx = static_cast<double>(app.dockHeight - app.triggerHeight);
      if (!app.reveal) app.animOffsetPx = app.animTargetPx;
    }
  }

  const int mbCompositor = dock_compositor_margin_bottom_px(app);
  for (auto& up : app.dockLayers) {
    if (up && up->layer) zwlr_layer_surface_v1_set_margin(up->layer, 0, 0, mbCompositor, 0);
  }
  if (app.layerSurface && app.dockLayers.empty()) zwlr_layer_surface_v1_set_margin(app.layerSurface, 0, 0, mbCompositor, 0);
 
  app.sizeDirty = true;
  const bool dockHidden = !app.settings.dockShowDock;
  const int desiredH = app.dockHeight;
  auto apply_one = [&](DockOutputLayer& L) {
    if (!L.layer) return;
    const int dw = compute_desired_surface_width_for_output(app, L.wlOut);
    int exclusive = 0;
    if (!dockHidden)
      exclusive = app.autoHide ? -1 : desiredH + dock_exclusive_zone_gap_px(app.settings);
    if (eh_startup_trace_enabled()) {
      static int sync_chrome_size_logs = 40;
      if (sync_chrome_size_logs > 0) {
        --sync_chrome_size_logs;
        EH_ST_TRACE(std::cerr << "dock sync_dock_chrome: exclusive_zone=" << exclusive
                              << " size=" << L.configuredWidth << "x" << L.configuredHeight
                              << " desired=" << dw << "x" << desiredH);
      }
    }
    zwlr_layer_surface_v1_set_exclusive_zone(L.layer, exclusive);
    zwlr_layer_surface_v1_set_size(L.layer, static_cast<uint32_t>(dw), static_cast<uint32_t>(desiredH));
  };
  if (!app.dockLayers.empty()) {
    for (auto& up : app.dockLayers)
      if (up) apply_one(*up);
  } else if (app.layerSurface) {
    const int dw = compute_desired_surface_width(app);
    const int exclusive = dockHidden ? 0 : (app.autoHide ? -1 : desiredH + dock_exclusive_zone_gap_px(app.settings));
    zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, exclusive);
    zwlr_layer_surface_v1_set_size(app.layerSurface, static_cast<uint32_t>(dw), static_cast<uint32_t>(desiredH));
  }
}

int dock_compute_widget_strip_width(DockApp& app, const std::vector<std::string>& leftW,
                                    const std::vector<std::string>& centerW, const std::vector<std::string>& rightW) {
  if (!app.configured) return 120;
  DockPickResult scratch;
  const auto hitSnap = eh::shell::shared::build_running_snapshot(app.toplevels, app.appFirstSeenSerial, app.settings.dockGroupApps);
  dock_fill_pick_result_slots(app, hitSnap, leftW, centerW, rightW, scratch);
  const double totalPx = dock_pick_layout_total_width(app, scratch.all);
  const double scPad = dock_ui_scale(app.settings);
  const int outerPad = std::max(8, static_cast<int>(std::lround(24.0 * scPad)));
  constexpr int minW = 120;
  constexpr int maxW = 4800;
  int w = outerPad + static_cast<int>(std::ceil(totalPx)) + outerPad;
  w = std::max(minW, std::min(maxW, w));
  return w;
}

DockPinnedDragGeometry dock_pinned_drag_geometry(DockApp& app) {
  MANGOWM_FN();
  DockPinnedDragGeometry out;
  if (!app.configured) return out;
  eh::shell::dock::dock_ensure_workspace_strip(app);

  DockPickResult scratch;
  const auto hitSnap = eh::shell::shared::build_running_snapshot(app.toplevels, app.appFirstSeenSerial, app.settings.dockGroupApps);
  dock_fill_pick_result_slots(app, hitSnap, app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets,
                               scratch);

  const double icon = static_cast<double>(dock_effective_icon_px(app.settings));
  const double gap = static_cast<double>(dock_effective_gap_px(app.settings));
  const double total = dock_pick_layout_total_width(app, scratch.all);

  double px{}, py{}, boxW{}, boxH{};
  dock_pill_geometry(app, px, py, boxW, boxH);
  const double midX = px + boxW * 0.5;
  const double stripInner = 8.0 * dock_ui_scale(app.settings);
  const double hScale = widget_strip_h_scale(boxW, total, stripInner);
  const double startLayoutX = px + (boxW - total) * 0.5;

  const auto& scPick = eh::config::shell_config_snapshot();
  auto pick_slot_w = [&](const PickSlot& s) -> double {
    if (s.kind == PickSlot::Kind::Clock) {
      return eh::shell::dock_slot_hooks::dock_clock_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Weather) {
      return eh::shell::dock_slot_hooks::dock_weather_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Media) {
      const eh::mpris::PlayerSnapshot snap = app.mpris ? app.mpris->snapshot() : eh::mpris::PlayerSnapshot{};
      return eh::shell::dock_slot_hooks::dock_media_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight), snap);
    }
    if (s.kind == PickSlot::Kind::Workspaces) {
      return eh::shell::dock_slot_hooks::dock_workspaces_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight),
                                                      app.workspaceStrip);
    }
    if (s.kind == PickSlot::Kind::ControlCenter) {
      return eh::shell::dock_slot_hooks::dock_control_center_slot_width(scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    if (s.kind == PickSlot::Kind::Bluetooth) {
      return eh::shell::dock_slot_hooks::dock_bluetooth_slot_width(nullptr, scPick, s.key, icon, static_cast<double>(app.dockHeight));
    }
    return icon;
  };

  double curLayout = startLayoutX;
  bool foundFirst = false;
  for (size_t i = 0; i < scratch.all.size(); ++i) {
    const PickSlot& s = scratch.all[i];
    const double sw = pick_slot_w(s);
    if (s.kind == PickSlot::Kind::App && s.isPinned) {
      if (!foundFirst) {
        out.firstPinnedLeftSurf = strip_layout_x_to_surface(curLayout, midX, hScale);
        foundFirst = true;
      }
      ++out.pinnedCount;
    }
    curLayout += sw + (i + 1 < scratch.all.size() ? dock_gap_after_pick(app.settings, scratch.all, i, gap) : 0.0);
  }

  out.slotStrideSurf = (icon + gap) * hScale;
  out.iconSurf = icon * hScale;
  out.valid = foundFirst && out.pinnedCount > 0;
  return out;
}

static int compute_desired_surface_width(DockApp& app) {
  int w = dock_compute_widget_strip_width(app, app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets);
  const int cap = dock_max_strip_width_px(app);
  if (cap > 0 && w > cap) w = cap;
  return w;
}

static int compute_desired_surface_width_for_output(DockApp& app, wl_output* out) {
  int w = dock_compute_widget_strip_width(app, app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets);
  if (out) {
    for (const auto& u : app.outputSlots) {
      if (u && u->output == out && u->ready && u->logical_w > 0) {
        w = std::min(w, u->logical_w);
        break;
      }
    }
  }
  const int cap = dock_max_strip_width_px(app);
  if (cap > 0 && w > cap) w = cap;
  return w;
}

static bool maybe_update_one_output_layer(DockApp& app, DockOutputLayer& L, int desiredW, int desiredH, bool trayAck) {
  MANGOWM_FN();
  (void)app;
  if (!L.layer) return true;

  if (!L.configured) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_one_output_layer: NOT_CONFIGURED surface=" << static_cast<void*>(L.surface)
                << " wlOut=" << static_cast<void*>(L.wlOut) << " desired=" << desiredW << "x" << desiredH
                << " → set_size+nullptr_attach+commit\n";
    zwlr_layer_surface_v1_set_size(L.layer, static_cast<uint32_t>(desiredW), static_cast<uint32_t>(desiredH));
    wl_surface_attach(L.surface, nullptr, 0, 0);
    eh::dock::clear_layer_rasters(L);
    wl_surface_commit(L.surface);
    return false;
  }

  if (!trayAck && L.configuredWidth == desiredW && L.configuredHeight == desiredH) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_one_output_layer: SIZE_MATCH surface=" << static_cast<void*>(L.surface)
                << " " << L.configuredWidth << "x" << L.configuredHeight << " ← desired " << desiredW << "x" << desiredH
                << " → no commit\n";
    return true;
  }
  if (trayAck && L.configuredWidth == desiredW && L.configuredHeight == desiredH) {
    return true;
  }

  const bool want_wider = L.configuredWidth < desiredW;
  const bool want_taller = L.configuredHeight < desiredH;
  const bool want_narrower = L.configuredWidth > desiredW;
  const bool want_shorter = L.configuredHeight > desiredH;
  const bool degenerate_granted = L.configuredWidth <= 1 || L.configuredHeight <= 1;

  if ((want_wider || want_taller) && !(want_narrower || want_shorter)) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_one_output_layer: GROW surface=" << static_cast<void*>(L.surface)
                << " " << L.configuredWidth << "x" << L.configuredHeight << " → desired " << desiredW << "x" << desiredH
                << " → set_size+commit(KEEP BUFFER)\n";
    zwlr_layer_surface_v1_set_size(L.layer, static_cast<uint32_t>(desiredW), static_cast<uint32_t>(desiredH));
    wl_surface_commit(L.surface);
    if (app.display) wl_display_roundtrip(app.display);
    return true;
  }

  if ((want_narrower || want_shorter) && !(want_wider || want_taller) && !degenerate_granted) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_one_output_layer: SHRINK surface=" << static_cast<void*>(L.surface)
                << " " << L.configuredWidth << "x" << L.configuredHeight << " → desired " << desiredW << "x" << desiredH
                << " → set_size+commit(KEEP BUFFER)\n";
    zwlr_layer_surface_v1_set_size(L.layer, static_cast<uint32_t>(desiredW), static_cast<uint32_t>(desiredH));
    wl_surface_commit(L.surface);
    if (app.display) wl_display_roundtrip(app.display);
    return true;
  }

  if (eh_layer_debug())
    std::cerr << "[layer-debug] maybe_update_one_output_layer: BUFFERLESS surface=" << static_cast<void*>(L.surface)
              << " " << L.configuredWidth << "x" << L.configuredHeight << " → desired " << desiredW << "x" << desiredH
              << " degenerate=" << (degenerate_granted ? 1 : 0) << " → null_attach+clear+commit\n";
  zwlr_layer_surface_v1_set_size(L.layer, static_cast<uint32_t>(desiredW), static_cast<uint32_t>(desiredH));
  wl_surface_attach(L.surface, nullptr, 0, 0);
  eh::dock::clear_layer_rasters(L);
  L.configured = false;
  wl_surface_commit(L.surface);
  return false;
}

static bool maybe_update_layer_size(DockApp& app) {
  MANGOWM_FN();
  if (app.dockLayers.empty()) return true;
  const bool show_resize_pending = app.settings.dockShowDock && app.sizeDirty;
  if (!app.configured && !show_resize_pending) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_layer_size: SKIP (not configured, no resize pending) sizeDirty="
                << (app.sizeDirty ? 1 : 0) << " → proceed to buffer commit (DANGER if first configure not received)\n";
    return true;
  }
  if (!app.sizeDirty) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_layer_size: SKIP (!sizeDirty) configured=" << (app.configured ? 1 : 0) << "\n";
    return true;
  }

  if (eh_layer_debug())
    std::cerr << "[layer-debug] maybe_update_layer_size: ENTER configured=" << (app.configured ? 1 : 0)
              << " sizeDirty=" << (app.sizeDirty ? 1 : 0) << " show_resize_pending=" << (show_resize_pending ? 1 : 0) << "\n";
  app.sizeDirty = false;

  const bool trayAck = app.trayStripNeedsLayerAck;
  app.trayStripNeedsLayerAck = false;

  const int desiredH = app.dockHeight;

    bool all_ok = true;
  for (auto& up : app.dockLayers) {
    if (!up) continue;
    const int desiredW = compute_desired_surface_width_for_output(app, up->wlOut);
    if (!maybe_update_one_output_layer(app, *up, desiredW, desiredH, trayAck)) all_ok = false;
  }
  dock_sync_legacy_from_primary(app);
  if (!all_ok) {
    if (eh_layer_debug())
      std::cerr << "[layer-debug] maybe_update_layer_size: EXIT (all_ok=false) re-queue sizeDirty=1 configured="
                << (app.configured ? 1 : 0) << "\n";

    app.sizeDirty = true;
    return false;
  }
  if (eh_layer_debug())
    std::cerr << "[layer-debug] maybe_update_layer_size: EXIT OK configured=" << (app.configured ? 1 : 0)
              << " configuredW=" << app.configuredWidth << "\n";
  return true;
}

static const eh::icons::IconEntry* get_cached_icon(DockApp& app, const std::string& key) { return app.icons.app_icon(key); }

static const eh::icons::IconEntry* get_cached_tray_icon(DockApp& app, const std::string& iconName) {
  return app.icons.tray_icon(iconName);
}

const eh::icons::IconEntry* eh_app_drawer_resolve_catalog_icon(DockApp& app, const std::string& desktop_path,
                                                               const std::string& icon_key) {
  const std::string stem = eh_app_drawer_desktop_stem_from_path(desktop_path);
  if (!stem.empty()) {
    if (const eh::icons::IconEntry* ic = get_cached_icon(app, stem)) {
      if (ic->surface) return ic;
    }
  }
  if (const eh::icons::IconEntry* ic = get_cached_tray_icon(app, icon_key)) {
    if (ic->surface) return ic;
  }
  if (!icon_key.empty() && icon_key != stem) {
    if (const eh::icons::IconEntry* ic = get_cached_icon(app, icon_key)) {
      if (ic->surface) return ic;
    }
  }
  return nullptr;
}

static void log_app_snapshot_if_changed(DockApp& app) {
  auto normalize = [](const std::string& s) -> std::string {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (out.size() > 8 && out.ends_with(".desktop")) out.resize(out.size() - 8);
    return out.empty() ? std::string("unknown") : out;
  };

  std::vector<std::string> ids;
  ids.reserve(app.toplevels.size());
  for (const auto& tl : app.toplevels) {
    if (!tl.handle || tl.closed) continue;
    ids.push_back(normalize(tl.appId));
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());

  std::string snap;
  for (size_t i = 0; i < ids.size(); i++) {
    if (i) snap += ", ";
    snap += ids[i];
  }

  if (snap != app.lastLoggedAppSnapshot) {
    app.lastLoggedAppSnapshot = snap;
    if (eh_verbose_enabled()) std::cout << "[dock] apps_shown: [" << snap << "]\n";
    app.sizeDirty = true;
  }
}

void sync_input_region_layer(DockApp& app, DockOutputLayer& L) {
  MANGOWM_FN();
  const int cfgW = L.configuredWidth;
  const int cfgH = L.configuredHeight;
  if (!app.compositor || !L.surface || cfgW <= 0 || cfgH <= 0) return;
  wl_region* region = wl_compositor_create_region(app.compositor);
  if (!region) return;

  if (!app.settings.dockShowDock) {
    wl_region_add(region, 0, 0, 1, 1);
    wl_surface_set_input_region(L.surface, region);
    wl_region_destroy(region);
    return;
  }

  if (app.autoHide && !app.reveal) {
    wl_region_add(region, 0, cfgH - app.triggerHeight, cfgW, app.triggerHeight);
  } else {
    double x{};
    double y{};
    double boxW{};
    double boxH{};
    dock_pill_geometry_dims(app, cfgW, cfgH, x, y, boxW, boxH);
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iw = static_cast<int>(std::ceil(boxW));
    const int ih = static_cast<int>(std::ceil(boxH));
    wl_region_add(region, ix, iy, iw, ih);
  }

  wl_surface_set_input_region(L.surface, region);
  wl_region_destroy(region);
}

void sync_input_region(DockApp& app) {
  MANGOWM_FN();
  if (!app.dockLayers.empty()) {
    for (auto& up : app.dockLayers)
      if (up) sync_input_region_layer(app, *up);
    return;
  }
  if (!app.compositor || !app.surface || app.configuredWidth <= 0 || app.configuredHeight <= 0) return;
  DockOutputLayer tmp{};
  tmp.surface = app.surface;
  tmp.configuredWidth = app.configuredWidth;
  tmp.configuredHeight = app.configuredHeight;
  sync_input_region_layer(app, tmp);
}

static void apply_autohide_state(DockApp& app, bool autoHide) {
  MANGOWM_FN();
  const bool changed = (app.autoHide != autoHide);
  app.autoHide = autoHide;
  if (changed) {
    std::cout << "[dock] setting: dock_autohide=" << (app.autoHide ? 1 : 0) << "\n";
  }

  app.shellAnim.cancel(app.dockSlideAnimId);
  app.dockSlideAnimId = 0;
  app.shellAnim.cancel(app.dockHoverLiftAnimId);
    app.dockHoverLiftAnimId = 0;

  if (!app.autoHide) {
    app.reveal = true;
    app.animTargetPx = 0.0;
    app.animOffsetPx = 0.0;
    if (!app.dockLayers.empty()) {
      for (auto& up : app.dockLayers) {
        if (!up || !up->layer) continue;
        const int dw = compute_desired_surface_width_for_output(app, up->wlOut);
        const int dh = app.dockHeight;
        zwlr_layer_surface_v1_set_exclusive_zone(up->layer, dh + dock_exclusive_zone_gap_px(app.settings));
        zwlr_layer_surface_v1_set_size(up->layer, static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
      }
    } else if (app.layerSurface) {
      const int dw = compute_desired_surface_width(app);
      const int dh = app.dockHeight;
      zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, dh + dock_exclusive_zone_gap_px(app.settings));
      zwlr_layer_surface_v1_set_size(app.layerSurface, static_cast<uint32_t>(dw), static_cast<uint32_t>(dh));
    }
    dock_draw(app);
    return;
  }

  app.reveal = false;
  app.animTargetPx = static_cast<double>(app.dockHeight - app.triggerHeight);
  app.animOffsetPx = app.animTargetPx;
  if (!app.dockLayers.empty()) {
    for (auto& up : app.dockLayers) {
      if (!up || !up->layer) continue;
      const int dw = compute_desired_surface_width_for_output(app, up->wlOut);
      zwlr_layer_surface_v1_set_exclusive_zone(up->layer, -1);
      zwlr_layer_surface_v1_set_size(up->layer, static_cast<uint32_t>(dw), static_cast<uint32_t>(app.dockHeight));
    }
  } else if (app.layerSurface) {
    const int dw = compute_desired_surface_width(app);
    zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, -1);
    zwlr_layer_surface_v1_set_size(app.layerSurface, static_cast<uint32_t>(dw), static_cast<uint32_t>(app.dockHeight));
  }
  dock_draw(app);
}

static bool dock_widget_lists_contain(const DockSettings& st, const char* token) {
  auto has = [&](const std::vector<std::string>& v) {
    for (const auto& w : v)
      if (w == token) return true;
    return false;
  };
  return has(st.leftWidgets) || has(st.centerWidgets) || has(st.rightWidgets);
}

static bool dock_widget_lists_contain_impl(const DockSettings& st, const char* implType) {
  auto has = [&](const std::vector<std::string>& v) {
    for (const auto& w : v)
      if (eh::config::widget_implementation_type(w) == implType) return true;
    return false;
  };
  return has(st.leftWidgets) || has(st.centerWidgets) || has(st.rightWidgets);
}

static bool dock_widget_token_warrants_app_icon_heat(const std::string& w, const DockSettings& st) {
  if (dock_strip_widget_blocked(st, w)) return false;
  if (w == "pinned_apps" || w == "running_apps") return false;
  if (eh::config::widget_token_is_system_tray(w)) return false;
  if (w == "settings_button" || w == "distro_spotlight" || w == "app_menu" || w == "launchpad")
    return false;
  const std::string impl = eh::config::widget_implementation_type(w);
  if (impl == "clock" || impl == "weather" || impl == "media" || impl == "workspaces" || impl == "control_center" ||
      impl == "app_drawer" || impl == "battery")
    return false;
  return true;
}

static void dock_warm_startup_caches(DockApp& app) {
  MANGOWM_FN();
  auto heat = [&](const std::vector<std::string>& v) {
    for (const auto& w : v) {
      if (!dock_widget_token_warrants_app_icon_heat(w, app.settings)) continue;
      (void)app.icons.app_icon(w);
    }
  };
  heat(app.settings.leftWidgets);
  heat(app.settings.centerWidgets);
  heat(app.settings.rightWidgets);
  if (dock_widget_lists_contain(app.settings, "settings_button")) {
    if (!app.settingsLogo) app.settingsLogo = eh::shell::asset::load_brand_logo_surface();
  }
  if (dock_widget_lists_contain(app.settings, "distro_spotlight") ||
      dock_widget_lists_contain_impl(app.settings, "app_drawer")) {
    if (!app.distroSpotlightLogo) app.distroSpotlightLogo = eh_os_logo::load_distro_logo_cairo_surface();
  }
  if (dock_widget_lists_contain(app.settings, "launchpad")) {
    // logos loaded on demand by launchpad_host at 48 px
  }
  if (dock_widget_lists_contain(app.settings, "trash")) {
    if (!app.trashEmptyLogo) app.trashEmptyLogo = eh::shell::asset::load_trash_empty_surface();
    if (!app.trashFullLogo) app.trashFullLogo = eh::shell::asset::load_trash_full_surface();
    app.trashFull = eh::shell::fs::trash_has_files();
  }
  for (auto& pRaw : dock_pinned_apps_source_for_layout(app)) {
    const std::string p = normalize_desktop_app_id(pRaw);
    if (p == "unknown" || p == eh::shell::kSettingsAppId) continue;
    (void)app.icons.app_icon(pRaw);
  }
  for (const auto& tl : app.toplevels) {
    if (tl.closed || tl.appId.empty()) continue;
    (void)app.icons.app_icon(tl.appId);
  }
  std::vector<DockApp::TrayItem> traySnap;
  {
    std::lock_guard<std::mutex> lock(app.trayMutex);
    traySnap = app.trayItems;
  }
  for (const auto& ti : traySnap) {
    if (!ti.iconName.empty()) (void)app.icons.tray_icon(ti.iconName);
  }
}

void dock_try_start_deferred_tray(DockApp& app) {
  MANGOWM_FN();
  if (!app.trayWatcherDeferPending) return;
  if (std::chrono::steady_clock::now() < app.trayWatcherDeferUntil) return;
  app.trayWatcherDeferPending = false;
  eh::tray::TrayManager::instance().start();
  dock_tray_init(app);
  dock_warm_startup_caches(app);
  if (eh_dock_bench() && shell_bench_have_init_t0()) {
    std::cerr << "[dock-bench] after tray_start_watcher (StatusNotifier D-Bus, deferred) cumulative=" << shell_bench_ms_since(shell_bench_init_t0())
              << "ms\n";
  }
}

void dock_maybe_reload_settings(DockApp& app, const char* source) {
  MANGOWM_FN();
  MANGOWM_DEBUG("dock_maybe_reload_settings source=%s", source ? source : "null");
  if (eh_startup_trace_enabled()) {
    const bool poll = source && std::strcmp(source, "poll") == 0;
    static int poll_reload_logs = 8;
    if (!poll || poll_reload_logs > 0) {
      if (poll) --poll_reload_logs;
      EH_ST_TRACE(std::cerr << "dock maybe_reload_settings: source=" << (source ? source : "") << " configured=" << (app.configured ? 1 : 0));
    }
  }
  const std::string path = eh::config::state_event_horizon_dir();
    auto mt = eh::config::aggregate_config_source_mtime();
  if (!mt) {
    static bool missingFileLogged = false;
    if (!missingFileLogged || eh_dock_settings_debug()) {
      missingFileLogged = true;
      const char* xdg = std::getenv("XDG_STATE_HOME");
      std::cerr << "[dock-settings] no config sources path=\"" << path << "\" source=" << source
                << " XDG_STATE_HOME=\"" << (xdg ? xdg : "") << "\"\n";
    }
    if (app.settingsMtime.has_value()) {
      app.settingsMtime.reset();
      apply_autohide_state(app, false);
    } else if (source && std::strcmp(source, "startup") == 0) {
      sync_dock_chrome(app);
      dock_draw(app);
      if (app.surface) (void)wl_surface_commit(app.surface);
    }
    return;
  }

  if (app.settingsMtime.has_value() && timespec_equal(*mt, *app.settingsMtime)) {
    return;
  }

  if (std::strcmp(source, "startup") != 0) eh::config::shell_config_invalidate_light();
  const eh::config::ShellConfig loaded = eh::config::shell_config_snapshot_skip_matugen();
  DockSettings next = loaded.dock;
  const eh::config::ShellRendererBackend prevRenderer = app.dockRendererBackend;
  if (dock_settings_equal(next, app.settings) && loaded.renderer == app.dockRendererBackend) {
    // Self-heal: if settings were adopted somewhere without notifying the
    // icon cache, reconcile it here so theme switches can never get stuck.
    if (app.icons.icon_theme() != next.iconTheme) {
      std::cerr << "[dock] icon_theme reconcile \"" << app.icons.icon_theme() << "\" → \""
                << next.iconTheme << "\" source=" << source << "\n";
      app.icons.set_icon_theme(next.iconTheme);
      for (auto& up : app.dockLayers)
        if (up) eh::dock::clear_layer_rasters(*up);
      eh::dock::clear_popup_gl_raster(app);
      app.sizeDirty = true;
    }
    app.settingsMtime = mt;
    if (eh_dock_settings_debug() && std::strcmp(source, "poll") != 0) {
      std::cout << "[dock-settings] dock.ini unchanged source=" << source << " path=\"" << path
                << "\" (repaint for TOML-only deltas e.g. [widget].enabled)\n";
    }

    if (source && std::strcmp(source, "startup") == 0 && app.layerSurface && app.layerSurfaceCreateDepth == 0 &&
        !app.pendingDockOutputRebind) {
      sync_dock_chrome(app);
      dock_draw(app);
      if (app.surface) (void)wl_surface_commit(app.surface);
    } else if (std::strcmp(source, "startup") != 0) {

      sync_dock_chrome(app);
      dock_draw(app);
      if (app.surface) (void)wl_surface_commit(app.surface);
    }
    return;
  }

  if (eh_dock_settings_debug()) {
    std::cout << "[dock-settings] diff: old r=" << app.settings.dockRadius << " icon=" << app.settings.dockIconSize
              << " sp=" << app.settings.dockIconSpacing << " bg=" << app.settings.dockBottomGap << " ah="
              << (app.settings.dockAutoHide ? 1 : 0) << " → new r=" << next.dockRadius << " icon=" << next.dockIconSize
              << " sp=" << next.dockIconSpacing << " bg=" << next.dockBottomGap << " ah=" << (next.dockAutoHide ? 1 : 0)
              << " source=" << source << "\n";
  }

  app.settingsMtime = mt;
  const std::string oldDockOut = trim(app.settings.outputName);
  const bool prev_show_dock = app.settings.dockShowDock;
  app.settings = std::move(next);
  app.dockRendererBackend = loaded.renderer;
  if (prev_show_dock != app.settings.dockShowDock) {
    eh::bench::bench_write("dock", app.settings.dockShowDock ? "turn_on" : "turn_off");
  }
  if (prev_show_dock && !app.settings.dockShowDock &&
      eh::dock::use_vulkan_backend(app)) {
    for (auto& up : app.dockLayers) {
      if (up) eh::dock::clear_layer_rasters(*up);
    }
    eh::dock::clear_popup_gl_raster(app);
  }
  if (prevRenderer != app.dockRendererBackend) {
    for (auto& up : app.dockLayers) {
      if (up) eh::dock::clear_layer_rasters(*up);
    }
    eh::dock::clear_popup_gl_raster(app);
    app.dockVk.reset();
    app.dockVkFailed = false;
  }
  std::cerr << "[dock] icon_theme=\"" << app.settings.iconTheme << "\" source=" << source << "\n";
  app.icons.set_icon_theme(app.settings.iconTheme);
  if (std::strcmp(source, "startup") != 0 && app.settings.dockShowDock) {
    dock_warm_startup_caches(app);
  }
  apply_autohide_state(app, app.settings.dockAutoHide);
  if (prev_show_dock != app.settings.dockShowDock) {
    // Toggle: skip sync_dock_chrome — only exclusive zone changes, size stays the same.
    const auto t0 = ShellBenchClock::now();
    const int exclusive = app.settings.dockShowDock
                              ? (app.autoHide ? -1 : app.dockHeight + dock_exclusive_zone_gap_px(app.settings))
                              : 0;
    for (auto& up : app.dockLayers)
      if (up && up->layer) zwlr_layer_surface_v1_set_exclusive_zone(up->layer, exclusive);
    if (app.layerSurface && app.dockLayers.empty())
      zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, exclusive);
    dock_apply_poll_timer_interval(app);
    if (app.settings.dockShowDock) {
      // Show background instantly, defer widget paint to next frame
      app.dockDeferWidgetsOnNextDraw = true;
    }
    dock_draw(app);
    std::cerr << "[dock-toggle] total=" << shell_bench_ms_since(t0) << "ms\n";
  } else if (trim(oldDockOut) != trim(app.settings.outputName)) {
    DOCK_DIAG("outputName changed: \"%s\" → \"%s\"", oldDockOut.c_str(), app.settings.outputName.c_str());
    sync_dock_chrome(app);
    dock_apply_poll_timer_interval(app);
    if (app.layerSurfaceCreateDepth > 0) app.pendingDockOutputRebind = true;
    else dock_rebind_layer_outputs(app);
  } else {
    const auto t_toggle0 = ShellBenchClock::now();
    sync_dock_chrome(app);
    const auto t_sync = ShellBenchClock::now();
    std::cout << "[dock-settings] APPLY source=" << source << " path=\"" << path << "\" radius=" << app.settings.dockRadius
              << " icon=" << app.settings.dockIconSize << " gap=" << app.settings.dockIconSpacing
              << " bottom_gap=" << app.settings.dockBottomGap << " border=" << (app.settings.dockBorderEnabled ? 1 : 0)
              << " border_px=" << app.settings.dockBorderSize << " border_h=" << app.settings.dockBorderHue
              << " autohide=" << (app.settings.dockAutoHide ? 1 : 0) << "\n";
    dock_apply_poll_timer_interval(app);
    dock_draw(app);
    (void)t_toggle0;
    (void)t_sync;
  }
}

static void schedule_frame(DockApp& app);

// Kill switch for Weston-style partial damage: enabled by default,
// EH_DOCK_PARTIAL_DAMAGE=0 forces full repaints everywhere.
static bool eh_dock_partial_damage_enabled() {
  static const bool enabled = []() noexcept {
    const char* e = std::getenv("EH_DOCK_PARTIAL_DAMAGE");
    return !(e && e[0] == '0' && e[1] == '\0');
  }();
  return enabled;
}

// Safety net: while partial damage is active, force a full repaint at least
// this often (ms) so any missed dirty signal self-heals; <=0 disables it.
static int64_t eh_dock_damage_full_period_ms() {
  static const int64_t v = eh::debug_profile::env_int("EH_DOCK_DAMAGE_FULL_PERIOD_MS", 5000);
  return v;
}

static std::vector<DockWidgetHit>& dock_paint_hits_scratch() {
  static std::vector<DockWidgetHit> hits;
  return hits;
}

void dock_draw(DockApp& app, bool* committed) {
  MANGOWM_FN();
  if (committed) *committed = false;
  // Consume the scope set by frame_done (direct draws always run Full).
  const DockApp::DockFrameScope frameScope = app.nextFrameScope;
  app.nextFrameScope = DockApp::DockFrameScope::Full;
  const bool partial_damage_on =
      eh_dock_partial_damage_enabled() && frameScope == DockApp::DockFrameScope::MediaMarqueeOnly;
  ShellBenchClock::time_point t_d0{}, t_d1{}, t_d2{}, t_d3{}, t_d4{};
  const bool draw_split = shell_bench_should_log_first_draw_detail();
  if (draw_split) t_d0 = ShellBenchClock::now();
  if (app.dockLayers.empty()) return;
  if (!app.pinDragging) app.pinDragLayerOnlyNextDraw = false;
  if (!app.surface || !app.layerSurface) return;
  if (eh_startup_trace_enabled()) {
    static int dock_draw_enter_left = 64;
    if (dock_draw_enter_left > 0) {
      --dock_draw_enter_left;
      EH_ST_TRACE(std::cerr << "dock dock_draw: enter configured=" << (app.configured ? 1 : 0) << " cfg=" << app.configuredWidth << "x"
                                                       << app.configuredHeight << " surface=" << static_cast<void*>(app.surface));
    }
  }
  const bool dockHidden = !app.settings.dockShowDock;
  const bool show_resize_pending = !dockHidden && app.sizeDirty;
  if ((!app.configured || app.configuredWidth <= 0 || app.configuredHeight <= 0) && !show_resize_pending) {

    if (!app.dockLayers.empty() && app.settings.dockShowDock) app.deferDockRedraw = true;
    return;
  }
  static int s_toggle_draw_log = 20;
  const bool log_toggle = s_toggle_draw_log > 0 && !app.settings.dockShowDock;
    auto t_draw_before_resize = ShellBenchClock::now();
  if (!maybe_update_layer_size(app)) {
    static int bufferless_log_left = 48;
    if (bufferless_log_left > 0) {
      --bufferless_log_left;
      eh::shell_log::dock("draw skipped: bufferless layer negotiate cfg=", app.configuredWidth, "x", app.configuredHeight,
                          " (no attach/commit this frame; budget=", bufferless_log_left, " more like this)");
    }
    EH_ST_TRACE(std::cerr << "dock dock_draw: exit early (bufferless resize)");

    app.deferDockRedraw = true;
    return;
  }
  if (log_toggle) {
    --s_toggle_draw_log;
    std::cerr << "[dock-toggle] draw: resize=" << shell_bench_ms_since(t_draw_before_resize) << "ms\n";
  }
  if (draw_split) t_d1 = ShellBenchClock::now();

  const bool pinDragLayerFast = app.pinDragging && app.pinDragLayerOnlyNextDraw && app.dockLayers.size() > 1 &&
                                 app.pointerDockLayerIdx < app.dockLayers.size();
  const bool pin_perf_drag = eh_dock_pin_drag_perf_enabled() && app.pinDragging;
  const bool was_pin_drag_fast = pinDragLayerFast;
  ShellBenchClock::time_point pin_perf_t0{};
  size_t pin_perf_layers_committed = 0;
  if (pin_perf_drag) pin_perf_t0 = ShellBenchClock::now();

  for (size_t liBusy = 0; liBusy < app.dockLayers.size(); ++liBusy) {
    auto& up = app.dockLayers[liBusy];
    if (!up) continue;
    if (pinDragLayerFast && liBusy != app.pointerDockLayerIdx) continue;

  }

  {
    int rcw{}, rch{};
    dock_active_canvas_dims(app, rcw, rch);
    double rx{}, ry{}, rboxW{}, rboxH{};
    dock_pill_geometry_dims(app, rcw, rch, rx, ry, rboxW, rboxH);
    app.dockRectX = rx;
    app.dockRectY = ry;
    app.dockRectW = rboxW;
    app.dockRectH = rboxH;
  }

  log_app_snapshot_if_changed(app);

  const bool want_fast_start = eh_dock_fast_start() && !app.dockFastStartDidFullPaint;
  const bool defer_widgets = app.dockDeferWidgetsOnNextDraw;
  app.dockDeferWidgetsOnNextDraw = false;
  const bool placeholder_only = (want_fast_start && !app.dockFastStartDidPlaceholder) || defer_widgets;

  const double radius = static_cast<double>(dock_effective_radius_px(app.settings));
  const double bgAlpha = static_cast<double>(std::clamp(app.settings.dockOpacity, 0, 100)) / 100.0;
  const eh::config::ChromePaintColors ehChrome =
      eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  static uint64_t s_dock_chrome_prev = 0;
  {
    const uint64_t key = static_cast<uint64_t>(std::round(ehChrome.accentR * 255.0)) << 16 |
                         static_cast<uint64_t>(std::round(ehChrome.accentG * 255.0)) << 8 |
                         static_cast<uint64_t>(std::round(ehChrome.accentB * 255.0));
    if (key != s_dock_chrome_prev) {
      s_dock_chrome_prev = key;
      const auto& snap = eh::config::shell_config_snapshot().appearance;
      debug_log("dock", "paint_chrome hc_native=%d hc_ok=%d matugen=%d accent=%02x%02x%02x fill=%02x%02x%02x",
          snap.horizonColorsNative ? 1 : 0, snap.horizonColorsPaletteOk ? 1 : 0,
          snap.matugenThemingEnabled ? 1 : 0,
          static_cast<unsigned>(std::round(ehChrome.accentR * 255.0)),
          static_cast<unsigned>(std::round(ehChrome.accentG * 255.0)),
          static_cast<unsigned>(std::round(ehChrome.accentB * 255.0)),
          static_cast<unsigned>(std::round(ehChrome.dockFillR * 255.0)),
          static_cast<unsigned>(std::round(ehChrome.dockFillG * 255.0)),
          static_cast<unsigned>(std::round(ehChrome.dockFillB * 255.0)));
    }
  }
  bool any_commit = false;
  bool media_mq = false;
  bool media_progress_tick = false;
  double sum_ensure_ms = 0;
  double sum_cairo_ms = 0;
  double sum_input_ms = 0;
  double sum_wl_ms = 0;
  const auto t_render_start = ShellBenchClock::now();

  auto rounded_rect = [&](cairo_t* cr, double rx, double ry, double rw, double rh, double r) {
    const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0, M_PI_2);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  if (draw_split) t_d2 = ShellBenchClock::now();
  for (size_t li = 0; li < app.dockLayers.size(); ++li) {
    auto& up = app.dockLayers[li];
    if (!up || !up->surface || !up->layer) continue;
    if (pinDragLayerFast && li != app.pointerDockLayerIdx) continue;
    if (!up->configured || up->configuredWidth <= 0 || up->configuredHeight <= 0) continue;
    const int logW = up->configuredWidth;
    const int logH = up->configuredHeight;
    const double bufScale = up->surfExt.preferred_scale();
    const int bufW = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logW) * bufScale)));
    const int bufH = std::max(1, static_cast<int>(std::ceil(static_cast<double>(logH) * bufScale)));
    const int cw = logW;
    const int ch = logH;
    ShellBenchClock::time_point te{};
    if (draw_split) te = ShellBenchClock::now();
    eh::dock::IDockRasterBackend* const backend = eh::dock::try_pick_raster_backend(app, *up, bufW, bufH);
    if (!backend) return;
    cairo_t* const cr = backend->cairo_ctx(*up);
    if (draw_split) sum_ensure_ms += shell_bench_ms_between(te, ShellBenchClock::now());

    // Weston-style partial-damage plan (per layer).
    // pendingDamage holds buffer regions not yet presented; empty means the
    // CPU buffer matches what the compositor shows, which is the precondition
    // for repainting only a clipped sub-region.
    if (up->lastPresentW != bufW || up->lastPresentH != bufH || up->lastBackend != static_cast<const void*>(backend)) {
      up->pendingDamage.mark_full();
      up->lastMediaRects.clear();
    }
    up->lastBackend = static_cast<const void*>(backend);
    if (!up->damageSeeded) up->pendingDamage.mark_full();

    bool layer_partial = partial_damage_on && !placeholder_only && app.settings.dockShowDock &&
                         up->pendingDamage.empty() && !up->lastMediaRects.empty();
    std::vector<eh::wayland::DamageRect> clip_spans;
    std::vector<eh::wayland::DamageRect> cur_media_rects;
    if (layer_partial) {
      const int32_t pad = static_cast<int32_t>(std::ceil(8.0 * bufScale)) + 1;
      clip_spans.reserve(up->lastMediaRects.size());
      for (const auto& r : up->lastMediaRects) {
        const int32_t bx =
            std::clamp(static_cast<int32_t>(std::floor(r.x * bufScale)) - pad, static_cast<int32_t>(0), bufW);
        const int32_t by =
            std::clamp(static_cast<int32_t>(std::floor(r.y * bufScale)) - pad, static_cast<int32_t>(0), bufH);
        const int32_t bs = std::min(bufW - bx, static_cast<int32_t>(std::ceil(r.w * bufScale)) + 2 * pad);
        const int32_t bh = std::min(bufH - by, static_cast<int32_t>(std::ceil(r.h * bufScale)) + 2 * pad);
        if (bs > 0 && bh > 0) clip_spans.push_back({bx, by, bs, bh});
      }
      if (clip_spans.empty()) layer_partial = false;
    }

    if (draw_split) te = ShellBenchClock::now();
    cairo_save(cr);
    if (layer_partial) {
      for (const auto& s : clip_spans) cairo_rectangle(cr, s.x, s.y, s.w, s.h);
      cairo_clip(cr);
    }
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (bufScale != 1.0) cairo_scale(cr, bufScale, bufScale);

    const double w = static_cast<double>(cw);
    const double h = static_cast<double>(ch);
    double x{};
    double y{};
    double boxW{};
    double boxH{};
    dock_pill_geometry_dims(app, cw, ch, x, y, boxW, boxH);

    const bool dockHidden = !app.settings.dockShowDock;
    if (!dockHidden) {
      rounded_rect(cr, x, y, boxW, boxH, radius);
      cairo_set_source_rgba(cr, ehChrome.dockFillR * 0.35, ehChrome.dockFillG * 0.35,
                            ehChrome.dockFillB * 0.35, bgAlpha * 0.78);
      cairo_fill_preserve(cr);

      // Liquid glass perimeter rim.
      if (app.settings.dockLiquidGlass) {
        const double rimW = std::max(1.0, 2.0 * std::min(1.0, boxH / 70.0));
        const bool tinted = app.settings.dockColoredGlass;
        const double tintR = tinted ? ehChrome.accentR : 1.0;
        const double tintG = tinted ? ehChrome.accentG : 1.0;
        const double tintB = tinted ? ehChrome.accentB : 1.0;

        // Layer 0 — frosted glass body (uniform translucent base wrapping all sides/corners)
        {
          cairo_set_source_rgba(cr, tintR, tintG, tintB, 0.10);
          cairo_set_line_width(cr, rimW);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
        }

        // Layer 1 — broad top-to-bottom glass shading (light from above)
        {
          cairo_pattern_t* glassShade = cairo_pattern_create_linear(0, y, 0, y + boxH);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.00, tintR, tintG, tintB, 0.20);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.08, tintR, tintG, tintB, 0.12);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.25, tintR, tintG, tintB, 0.02);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.50, tintR, tintG, tintB, 0.00);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.75, tintR, tintG, tintB, 0.02);
          cairo_pattern_add_color_stop_rgba(glassShade, 0.92, tintR, tintG, tintB, 0.12);
          cairo_pattern_add_color_stop_rgba(glassShade, 1.00, tintR, tintG, tintB, 0.18);
          cairo_set_source(cr, glassShade);
          cairo_set_line_width(cr, rimW);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
          cairo_pattern_destroy(glassShade);
        }

        // Layer 2 — sharp specular highlight on the very top edge (always white)
        {
          cairo_pattern_t* specTop = cairo_pattern_create_linear(0, y, 0, y + rimW * 2.5);
          cairo_pattern_add_color_stop_rgba(specTop, 0.00, 1.0, 1.0, 1.0, 0.55);
          cairo_pattern_add_color_stop_rgba(specTop, 0.30, 1.0, 1.0, 1.0, 0.12);
          cairo_pattern_add_color_stop_rgba(specTop, 1.00, 1.0, 1.0, 1.0, 0.00);
          cairo_set_source(cr, specTop);
          cairo_set_line_width(cr, rimW * 0.6);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
          cairo_pattern_destroy(specTop);
        }

        // Layer 3 — thin inner bevel shadow just below the top highlight
        {
          cairo_pattern_t* bevelShadow = cairo_pattern_create_linear(0, y + rimW, 0, y + rimW * 3.0);
          cairo_pattern_add_color_stop_rgba(bevelShadow, 0.0, 0.0, 0.0, 0.0, 0.18);
          cairo_pattern_add_color_stop_rgba(bevelShadow, 1.0, 0.0, 0.0, 0.0, 0.00);
          cairo_set_source(cr, bevelShadow);
          cairo_set_line_width(cr, rimW * 0.35);
          rounded_rect(cr, x + rimW * 0.3, y + rimW * 0.3,
                       std::max(1.0, boxW - rimW * 0.6), std::max(1.0, boxH - rimW * 0.6),
                       std::max(0.0, radius - rimW * 0.3));
          cairo_stroke(cr);
          cairo_pattern_destroy(bevelShadow);
        }

        // Layer 4 — side edge highlights (left/right catch ambient light)
        {
          cairo_pattern_t* sideHL = cairo_pattern_create_linear(x, 0, x + boxW, 0);
          cairo_pattern_add_color_stop_rgba(sideHL, 0.00, tintR, tintG, tintB, 0.22);
          cairo_pattern_add_color_stop_rgba(sideHL, 0.04, tintR, tintG, tintB, 0.06);
          cairo_pattern_add_color_stop_rgba(sideHL, 0.30, tintR, tintG, tintB, 0.00);
          cairo_pattern_add_color_stop_rgba(sideHL, 0.70, tintR, tintG, tintB, 0.00);
          cairo_pattern_add_color_stop_rgba(sideHL, 0.96, tintR, tintG, tintB, 0.06);
          cairo_pattern_add_color_stop_rgba(sideHL, 1.00, tintR, tintG, tintB, 0.18);
          cairo_set_source(cr, sideHL);
          cairo_set_line_width(cr, rimW);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
          cairo_pattern_destroy(sideHL);
        }

        // Layer 5 — bright bottom lip (tinted)
        {
          cairo_pattern_t* botLip = cairo_pattern_create_linear(0, y + boxH - rimW * 2, 0, y + boxH);
          cairo_pattern_add_color_stop_rgba(botLip, 0.00, tintR, tintG, tintB, 0.00);
          cairo_pattern_add_color_stop_rgba(botLip, 0.50, tintR, tintG, tintB, 0.08);
          cairo_pattern_add_color_stop_rgba(botLip, 1.00, tintR, tintG, tintB, 0.30);
          cairo_set_source(cr, botLip);
          cairo_set_line_width(cr, rimW * 0.7);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
          cairo_pattern_destroy(botLip);
        }

        // Layer 6 — faint outer glow (wider, more transparent, adds glass halo)
        {
          cairo_pattern_t* glow = cairo_pattern_create_linear(0, y, 0, y + boxH);
          cairo_pattern_add_color_stop_rgba(glow, 0.00, tintR, tintG, tintB, 0.08);
          cairo_pattern_add_color_stop_rgba(glow, 0.10, tintR, tintG, tintB, 0.03);
          cairo_pattern_add_color_stop_rgba(glow, 0.50, tintR, tintG, tintB, 0.00);
          cairo_pattern_add_color_stop_rgba(glow, 0.90, tintR, tintG, tintB, 0.03);
          cairo_pattern_add_color_stop_rgba(glow, 1.00, tintR, tintG, tintB, 0.06);
          cairo_set_source(cr, glow);
          cairo_set_line_width(cr, rimW * 3.0);
          rounded_rect(cr, x, y, boxW, boxH, radius);
          cairo_stroke(cr);
          cairo_pattern_destroy(glow);
        }
      }

      if (app.settings.dockBorderEnabled && app.settings.dockBorderSize > 0) {
        const double bw = static_cast<double>(
            std::clamp(static_cast<int>(std::lround(static_cast<double>(std::clamp(app.settings.dockBorderSize, 1, 12)) *
                                                   dock_ui_scale(app.settings))),
                       1, 18));
        const double inset = bw * 0.5;
        const double rr = std::max(0.0, radius - inset);
        rounded_rect(cr, x + inset, y + inset, std::max(1.0, boxW - bw), std::max(1.0, boxH - bw), rr);
        double sr = 1.0, sg = 1.0, sb = 1.0;
        double borderA = static_cast<double>(std::clamp(app.settings.dockBorderOpacity, 0, 100)) / 100.0;
        const auto& apMat = eh::config::shell_config_snapshot().appearance;

        if (apMat.anyPaletteActive()) {
          sr = ehChrome.accentR;
          sg = ehChrome.accentG;
          sb = ehChrome.accentB;
        } else {
          hsv_to_rgb(static_cast<double>(std::clamp(app.settings.dockBorderHue, 0, 359)), 0.55, 0.92, sr, sg, sb);
        }
        cairo_set_source_rgba(cr, sr, sg, sb, borderA);
        cairo_set_line_width(cr, bw);
        cairo_stroke(cr);
      } else {
        cairo_new_path(cr);
      }
    } else {
      cairo_new_path(cr);
    }

    bool layer_media = false;
    bool layer_media_progress = false;
    if (!placeholder_only && !dockHidden) {
      auto& paint_hits = dock_paint_hits_scratch();
      paint_hits.clear();
      dock_paint_widget_bar(app, cr, x, y, boxW, boxH, app.settings.leftWidgets, app.settings.centerWidgets,
                            app.settings.rightWidgets, false, 0, &layer_media, li, &paint_hits,
                            -1, -1, 0.0, &layer_media_progress);
      if (layer_media) media_mq = true;
      if (layer_media_progress) media_progress_tick = true;

      cur_media_rects.reserve(paint_hits.size());
      for (const auto& h : paint_hits) {
        if (eh::config::widget_implementation_type(h.widgetId) == "media") {
          cur_media_rects.push_back({static_cast<int32_t>(std::lround(h.x)), static_cast<int32_t>(std::lround(h.y)),
                                     static_cast<int32_t>(std::lround(h.w)), static_cast<int32_t>(std::lround(h.h))});
        }
      }

      if (app.autoHide && !app.reveal) {
        cairo_rectangle(cr, 0, h - static_cast<double>(app.triggerHeight), w, static_cast<double>(app.triggerHeight));
        cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
        cairo_fill(cr);
      }
    }

    // The clip was derived from last frame's media geometry; verify it still
    // covers the current one. If not, heal with a full repaint next frame.
    if (layer_partial && cur_media_rects.empty()) layer_partial = false;
    if (layer_partial) {
      const int32_t tol = static_cast<int32_t>(std::ceil(1.0 * bufScale)) + 1;
      for (const auto& c : cur_media_rects) {
        const int32_t cx = static_cast<int32_t>(std::floor(static_cast<double>(c.x) * bufScale));
        const int32_t cy = static_cast<int32_t>(std::floor(static_cast<double>(c.y) * bufScale));
        const int32_t cwd = static_cast<int32_t>(std::ceil(static_cast<double>(c.w) * bufScale));
        const int32_t chd = static_cast<int32_t>(std::ceil(static_cast<double>(c.h) * bufScale));
        bool covered = false;
        for (const auto& s : clip_spans) {
          if (s.x <= cx + tol && s.y <= cy + tol && s.x + s.w >= cx + cwd + tol && s.y + s.h >= cy + chd + tol) {
            covered = true;
            break;
          }
        }
        if (!covered) {
          layer_partial = false;
          up->pendingDamage.mark_full();
          break;
        }
      }
    }

    if (layer_partial) {
      for (const auto& s : clip_spans) up->pendingDamage.add_rect(s.x, s.y, s.w, s.h);
      up->pendingDamage.clip_to(bufW, bufH);
    } else {
      up->pendingDamage.mark_full();
    }

    cairo_restore(cr);
    cairo_surface_flush(backend->cairo_surface(*up));
    if (draw_split) sum_cairo_ms += shell_bench_ms_between(te, ShellBenchClock::now());

    if (draw_split) te = ShellBenchClock::now();
    sync_input_region_layer(app, *up);
    if (draw_split) sum_input_ms += shell_bench_ms_between(te, ShellBenchClock::now());

    if (eh_startup_trace_enabled()) {
      static int dock_commit_log_left = 48;
      if (dock_commit_log_left > 0) {
        --dock_commit_log_left;
        EH_ST_TRACE(std::cerr << "dock dock_draw: ATTACH+COMMIT buffer sz=" << bufW << "x" << bufH << " logical=" << cw << "x" << ch
                                                             << " (first commits log only)");
      }
    }
    if (draw_split) te = ShellBenchClock::now();
    if (up->surfExt.viewport && up->surface) {
      wl_surface_set_buffer_scale(up->surface, 1);
      wp_viewport_set_destination(up->surfExt.viewport, cw, ch);
    }
    if (up->bgEffect && !dockHidden) {
      wl_region* rgn = wl_compositor_create_region(app.compositor);
      wl_region_add(rgn, static_cast<int32_t>(x), static_cast<int32_t>(y),
                    static_cast<int32_t>(boxW), static_cast<int32_t>(boxH));
      ext_background_effect_surface_v1_set_blur_region(up->bgEffect, rgn);
      wl_region_destroy(rgn);
    }
    if (!backend->present(app, *up, bufW, bufH, up->pendingDamage)) {
      if (eh_dock_pin_drag_perf_enabled() && app.pinDragging) {
        std::cerr << "[dock-pin-perf] dock_draw present_failed layer_idx=" << li << "\n";
      }
      app.deferDockRedraw = true;
      return;
    }
    up->lastPresentW = bufW;
    up->lastPresentH = bufH;
    up->damageSeeded = true;
    up->lastMediaRects = std::move(cur_media_rects);
    up->pendingDamage.clear();
    if (layer_partial) {
      ++app.partialDamageFrames;
    } else {
      ++app.fullDamageFrames;
      clock_gettime(CLOCK_MONOTONIC, &app.lastFullPaintMono);
    }
    if (draw_split) sum_wl_ms += shell_bench_ms_between(te, ShellBenchClock::now());
    any_commit = true;
    if (pin_perf_drag) ++pin_perf_layers_committed;
  }

  if (log_toggle) {
    std::cerr << "[dock-toggle] draw: render_loop=" << shell_bench_ms_since(t_render_start) << "ms\n";
  }

  if (pinDragLayerFast) app.pinDragLayerOnlyNextDraw = false;

  if (pin_perf_drag && any_commit) {

    const double draw_ms = shell_bench_ms_since(pin_perf_t0);
    static uint32_t s_pin_drag_draw_log_i = 0;
    static std::chrono::steady_clock::time_point s_last_spike_emit{};
    static bool s_have_spike_emit_ts = false;
    ++s_pin_drag_draw_log_i;
    const bool sample = (s_pin_drag_draw_log_i % 24u) == 0u;
    const bool spike_candidate = draw_ms >= 18.5;
    bool spike_emit = false;
    if (spike_candidate) {
      const auto nowp = std::chrono::steady_clock::now();
      double since_ms = 1e9;
      if (s_have_spike_emit_ts) since_ms = std::chrono::duration<double, std::milli>(nowp - s_last_spike_emit).count();
      if (!s_have_spike_emit_ts || since_ms >= 350.0) {
        spike_emit = true;
        s_last_spike_emit = nowp;
        s_have_spike_emit_ts = true;
      }
    }
    if (sample || spike_emit) {
      const char* tag = spike_emit && sample ? " (spike+sampled)" : spike_emit ? " (spike)" : " (sampled)";
      std::cerr << "[dock-pin-perf] dock_draw ms=" << draw_ms << " layers_committed=" << pin_perf_layers_committed
                << " dock_heads=" << app.dockLayers.size() << " fast_layer_only=" << (was_pin_drag_fast ? 1 : 0)
                << " gpu_backend=" << 1 << tag << "\n";
    }
  }

  app.mediaMarqueeWantsFrame = media_mq;

  // Drive the media progress border from a slow timer instead of the vsync
  // frame chain. Only arm when no marquee scroll is active (scrolling keeps
  // the continuous chain, which repaints the dash as part of each frame).
  if (any_commit && media_progress_tick && !media_mq && !dockHidden) {
    dock_arm_media_anim_timer(app, eh_dock_media_progress_tick_ms());
  } else {
    dock_disarm_media_anim_timer(app);
  }

  if (draw_split) t_d3 = t_d4 = ShellBenchClock::now();

  if (committed && any_commit) *committed = true;
  if (any_commit && draw_split) {
    shell_bench_mark_first_draw_detail_logged();
    std::cerr << "[dock-bench] detail dock_draw (1st commit): entry_to_maybe_update_done="
              << shell_bench_ms_between(t_d0, t_d1) << "ms maybe_done_to_paint_loop=" << shell_bench_ms_between(t_d1, t_d2)
               << "ms ensure_vk=" << sum_ensure_ms << "ms cairo_clear_paint_widgets_flush=" << sum_cairo_ms
              << "ms sync_input_region=" << sum_input_ms << "ms wl_attach_damage_commit=" << sum_wl_ms
              << "ms post_layers_geometry=" << shell_bench_ms_between(t_d3, t_d4) << "ms draw_total=" << shell_bench_ms_between(t_d0, t_d4)
              << "ms\n";
  }
  if (any_commit && eh_dock_bench() && shell_bench_have_init_t0() && !app.dockBenchLoggedFirstCommit) {
    app.dockBenchLoggedFirstCommit = true;
    std::cerr << "[dock-bench] first_layer_commit since_dock_init=" << shell_bench_ms_since(shell_bench_init_t0()) << "ms\n";
  }
  maybe_log_layout(app, "draw");
  if (any_commit && placeholder_only) {
    if (!defer_widgets) {
      app.dockFastStartDidPlaceholder = true;
      std::cerr << "[dock] EH_DOCK_FAST_START: placeholder frame committed; full widget paint deferred\n";
    }
    app.deferDockRedraw = true;
    schedule_frame(app);
  } else if (any_commit && want_fast_start && app.dockFastStartDidPlaceholder && !placeholder_only) {
    app.dockFastStartDidFullPaint = true;
  }
  // NOTE: no unconditional re-arm here. Marquee/animation continuation is
  // handled once, after draw, by the throttled scheduler in frame_done
  // (media-marquee limited to ~30fps); an early wl_surface_frame here would
  // win the race and pin the loop at full vsync rate even when idle.

  if (any_commit && app.configured && !app.dockLayers.empty() && !placeholder_only) {
    eh::shell::dock::dock_ensure_workspace_strip(app);
    app.dockLastIntrinsicStripWidth =
        dock_compute_widget_strip_width(app, app.settings.leftWidgets, app.settings.centerWidgets, app.settings.rightWidgets);
  }
}

void dock_anim_start_slide(DockApp& app) {
  MANGOWM_FN();
  app.shellAnim.cancel(app.dockSlideAnimId);
  app.dockSlideAnimId = app.shellAnim.animate(
      static_cast<float>(app.animOffsetPx), static_cast<float>(app.animTargetPx), 360.f,
      eh::shell::Easing::EaseOutCubic,
      [&app](float v) { app.animOffsetPx = static_cast<double>(v); },
      [&app] {
        app.dockSlideAnimId = 0;
        app.animOffsetPx = app.animTargetPx;
      });
  schedule_frame(app);
}

void dock_anim_start_hover_lift(DockApp& app) {
  MANGOWM_FN();
  app.shellAnim.cancel(app.dockHoverLiftAnimId);
  app.dockHoverLiftAnimId = app.shellAnim.animate(
      static_cast<float>(app.dockHoverLiftPx), static_cast<float>(app.dockHoverLiftTarget), 165.f,
      eh::shell::Easing::EaseOutCubic, [&app](float v) { app.dockHoverLiftPx = static_cast<double>(v); },
      [&app] { app.dockHoverLiftAnimId = 0; });
  schedule_frame(app);
}

static double dock_timespec_diff_ms(const timespec& later, const timespec& earlier) {
  const auto ds = static_cast<int64_t>(later.tv_sec) - static_cast<int64_t>(earlier.tv_sec);
  const auto dns = static_cast<int64_t>(later.tv_nsec) - static_cast<int64_t>(earlier.tv_nsec);
  return static_cast<double>(ds) * 1000.0 + static_cast<double>(dns) / 1e6;
}

static void dock_fps_log_on_vsync(uint32_t compositor_time_ms, bool pin_dragging) {
  const int mode = eh_dock_fps_log_mode();
  if (mode == 0) return;
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);

  static bool have_baseline = false;
  static timespec window_start{};
  static timespec last_frame{};
  static uint32_t frames_in_window = 0;
  static double gap_min_ms = 1e9;
  static double gap_max_ms = 0.0;
 
  if (!have_baseline) {
    window_start = last_frame = now;
    have_baseline = true;
    return;
  }

  const double gap_ms = dock_timespec_diff_ms(now, last_frame);
  last_frame = now;

  if (mode == 2) {
    std::cerr << std::fixed << std::setprecision(2) << "[dock-fps] gap_ms=" << gap_ms << " compositor_ms=" << compositor_time_ms
              << " pin_drag=" << (pin_dragging ? 1 : 0) << "\n";
    return;
  }

  frames_in_window++;
  gap_min_ms = std::min(gap_min_ms, gap_ms);
  gap_max_ms = std::max(gap_max_ms, gap_ms);

  const double window_ms = dock_timespec_diff_ms(now, window_start);
  if (window_ms < 1000.0) return;

  const double window_s = window_ms / 1000.0;
  const double hz = static_cast<double>(frames_in_window) / window_s;
  const double avg_gap_ms = window_ms / static_cast<double>(frames_in_window);
  std::cerr << std::fixed << std::setprecision(1) << "[dock-fps] hz=" << hz << " frames=" << frames_in_window
            << " avg_gap_ms=" << avg_gap_ms << " gap_min_ms=" << gap_min_ms << " gap_max_ms=" << gap_max_ms
            << " pin_drag=" << (pin_dragging ? 1 : 0) << "\n";

  window_start = now;
  frames_in_window = 0;
  gap_min_ms = 1e9;
  gap_max_ms = 0.0;
}

static void frame_done(void* data, wl_callback* cb, uint32_t compositor_time_ms) {
  MANGOWM_FN();
  auto& app = *static_cast<DockApp*>(data);
  wl_callback_destroy(cb);
  app.frameCallback = nullptr;
  eh::gpu::touch_frame_activity();

  dock_fps_log_on_vsync(compositor_time_ms, app.pinDragging);

#ifndef NDEBUG
  {
    using eh::wayland::VulkanLayerSurface;
    auto ds = VulkanLayerSurface::debug_snapshot_and_reset();
    if (ds.present_count > 0 || ds.bytes_uploaded > 0) {
      std::cerr << "[draw-stats] presents=" << ds.present_count << " upload_kb=" << (ds.bytes_uploaded / 1024)
                << " avg_kb_per_present=" << (ds.present_count > 0 ? ds.bytes_uploaded / ds.present_count / 1024 : 0)
                << "\n";
    }
  }
#endif

  app.shellAnim.tick();

  // Classify the upcoming frame BEFORE drawing. A frame whose only reason to
  // exist is the media marquee may repaint just the marquee region; anything
  // else (shell animations, workspace strip, input, popups, resize, external
  // schedule requests) falls back to a full repaint.
  app.nextFrameScope = DockApp::DockFrameScope::Full;
  if (eh_dock_partial_damage_enabled() && app.mediaMarqueeWantsFrame && !app.externalFrameRequest &&
      !app.shellAnim.has_active() && !app.wsStripAnim.active && !app.pendingRedraw && !app.sizeDirty &&
      !app.deferDockRedraw && !app.pinDragging && !app.pinDragCandidate && app.pinDragKey.empty() && !app.popupOpen) {
    const int64_t period = eh_dock_damage_full_period_ms();
    bool due_full = period <= 0;
    if (!due_full) {
      timespec now{};
      clock_gettime(CLOCK_MONOTONIC, &now);
      due_full = app.lastFullPaintMono.tv_sec == 0 && app.lastFullPaintMono.tv_nsec == 0;
      if (!due_full) due_full = dock_timespec_diff_ms(now, app.lastFullPaintMono) >= static_cast<double>(period);
    }
    if (!due_full) app.nextFrameScope = DockApp::DockFrameScope::MediaMarqueeOnly;
  }
  app.externalFrameRequest = false;

  dock_draw(app);

  static const bool dmg_stats = eh::debug_profile::env_int("EH_DOCK_DAMAGE_STATS", 0) != 0;
  if (dmg_stats) {
    const int64_t total = app.partialDamageFrames + app.fullDamageFrames;
    if (total > 0 && total % 120 == 0) {
      std::cerr << "[dock-damage] frames=" << total << " partial=" << app.partialDamageFrames
                << " full=" << app.fullDamageFrames << " (EH_DOCK_DAMAGE_STATS)\n";
    }
  }

  if (app.shellAnim.has_active() || app.mediaMarqueeWantsFrame || app.wsStripAnim.active || app.pendingRedraw) {
    bool doSchedule = true;
    if (app.mediaMarqueeWantsFrame && !app.shellAnim.has_active() && !app.wsStripAnim.active) {
      static uint64_t lastMarqueeMs = 0;
      timespec ts{};
      clock_gettime(CLOCK_MONOTONIC, &ts);
      const uint64_t nowMs = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
      if (lastMarqueeMs != 0 && nowMs - lastMarqueeMs < 33) doSchedule = false;
      else lastMarqueeMs = nowMs;
    }
    if (doSchedule) schedule_frame(app);
    app.pendingRedraw = false;
  }

}

static const wl_callback_listener g_frame_listener = {
  .done = frame_done,
};

static wl_surface* dock_pick_frame_surface(DockApp& app) {
  MANGOWM_FN();
  if (app.dockLayers.empty()) return app.surface;
  if (app.pointerSurface) {
    for (const auto& up : app.dockLayers) {
      if (up && up->surface == app.pointerSurface) return up->surface;
    }
  }
  if (app.pointerDockLayerIdx < app.dockLayers.size() && app.dockLayers[app.pointerDockLayerIdx] &&
      app.dockLayers[app.pointerDockLayerIdx]->surface)
    return app.dockLayers[app.pointerDockLayerIdx]->surface;
  return app.dockLayers[0] ? app.dockLayers[0]->surface : nullptr;
}

static bool dock_pick_frame_surface_ready(DockApp& app, wl_surface* surf) {
  if (!surf) return false;
  if (app.dockLayers.empty()) return app.configured;
  for (const auto& up : app.dockLayers) {
    if (up && up->surface == surf)
      return up->configured && up->configuredWidth > 0 && up->configuredHeight > 0;
  }
  return app.configured;
}

static void schedule_frame(DockApp& app) {
  MANGOWM_FN();
  wl_surface* surf = dock_pick_frame_surface(app);
  if (!surf || !dock_pick_frame_surface_ready(app, surf) || app.frameCallback) return;
  app.frameCallback = wl_surface_frame(surf);
  wl_callback_add_listener(app.frameCallback, &g_frame_listener, &app);
  wl_surface_commit(surf);
}

void dock_schedule_frame(DockApp& app) {
  MANGOWM_FN();
  // External callers (toplevel hooks etc.) request state changes that only a
  // full repaint can safely express; tag it so frame_done never classifies
  // the resulting frame as marquee-only.
  app.externalFrameRequest = true;
  schedule_frame(app);
}

void dock_sync_settings_from_drag_preview(DockApp& app) {
  MANGOWM_FN();
  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  DockSettings merged = sc.dock;
  // Adopting settings here bypasses dock_maybe_reload_settings' diff path,
  // which owns icon-theme notification — so handle the delta ourselves or a
  // later "no changes" reload silently skips set_icon_theme forever.
  const bool themeChanged = merged.iconTheme != app.settings.iconTheme;
  if (!dock_settings_equal(app.settings, merged)) app.sizeDirty = true;
  app.settings = std::move(merged);
  if (themeChanged) {
    app.icons.set_icon_theme(app.settings.iconTheme);
    for (auto& up : app.dockLayers)
      if (up) eh::dock::clear_layer_rasters(*up);
    eh::dock::clear_popup_gl_raster(app);
  }
}

static void dock_output_layer_configure(void* data, zwlr_layer_surface_v1* surf, uint32_t serial, uint32_t width,
                                        uint32_t height) {
  auto* L = static_cast<DockOutputLayer*>(data);
  if (!L->dock) return;
  DockApp& app = *L->dock;
  std::string outName;
  for (const auto& u : app.outputSlots)
    if (u && u->output == L->wlOut) { outName = u->output_name; break; }
  DOCK_DIAG("dock_output_layer_configure: serial=%u compositor_granted=%ux%u surf=%p layer=%p wlOut=%p output=\"%s\"",
            serial, width, height, (void*)surf, (void*)L->layer, (void*)L->wlOut, outName.c_str());
  EH_ST_TRACE(std::cerr << "dock layer_surface_configure: serial=" << serial << " compositor " << width << "x" << height
                       << " surf=" << static_cast<void*>(surf)
           << " layer=" << static_cast<void*>(L->layer));

  if (!L->layer || surf != L->layer) {
    EH_ST_TRACE(std::cerr << "dock layer_surface_configure: stale event from old surface, ignoring");
    return;
  }

  zwlr_layer_surface_v1_ack_configure(surf, serial);

  if (width > 0) L->configuredWidth = static_cast<int>(width);
  if (height > 0) L->configuredHeight = static_cast<int>(height);
  L->configured = true;
  dock_sync_legacy_from_primary(app);

  dock_maybe_reload_settings(app, "configure");

  if (!L->layer || surf != L->layer) {
    EH_ST_TRACE(std::cerr << "dock layer_surface_configure: not a tracked dock layer (return)");
    return;
  }

  const int desiredW = compute_desired_surface_width_for_output(app, L->wlOut);
  const int desiredH = app.dockHeight;

  if (L->configuredWidth <= 0) L->configuredWidth = desiredW;
  if (L->configuredHeight <= 0) L->configuredHeight = desiredH;

  if (L->configuredWidth != desiredW || L->configuredHeight != desiredH) {
    EH_ST_TRACE(std::cerr << "dock layer_surface_configure: mismatch desired " << desiredW << "x" << desiredH << " granted "
                                                       << L->configuredWidth << "x" << L->configuredHeight);
    zwlr_layer_surface_v1_set_size(L->layer, static_cast<uint32_t>(desiredW), static_cast<uint32_t>(desiredH));
    const bool degenerate_granted = L->configuredWidth <= 1 || L->configuredHeight <= 1;
    const bool show_grow_from_degenerate =
        app.settings.dockShowDock && degenerate_granted &&
        (desiredW > L->configuredWidth || desiredH > L->configuredHeight);
    if (degenerate_granted && show_grow_from_degenerate) {

      EH_ST_TRACE(std::cerr << "dock layer_surface_configure: degenerate granted → grow (keep configured) toward "
                                                       << desiredW << "x" << desiredH);
      wl_surface_commit(L->surface);
      dock_sync_legacy_from_primary(app);
      app.deferDockRedraw = true;
      return;
    }
    if (degenerate_granted) {
      EH_ST_TRACE(std::cerr << "dock layer_surface_configure: degenerate granted → bufferless grow");
      wl_surface_attach(L->surface, nullptr, 0, 0);
      eh::dock::clear_layer_rasters(*L);
      L->configured = false;
      wl_surface_commit(L->surface);
      dock_sync_legacy_from_primary(app);

      app.deferDockRedraw = true;
      return;
    }
    const bool need_smaller_buffer = L->configuredWidth > desiredW || L->configuredHeight > desiredH;
    if (need_smaller_buffer) {
      EH_ST_TRACE(std::cerr << "dock layer_surface_configure: shrink → bufferless handoff");
      wl_surface_attach(L->surface, nullptr, 0, 0);
      eh::dock::clear_layer_rasters(*L);
      L->configured = false;
      wl_surface_commit(L->surface);
      dock_sync_legacy_from_primary(app);

      app.deferDockRedraw = true;
      return;
    }
    EH_ST_TRACE(std::cerr << "dock layer_surface_configure: compositor capped / negotiate; draw at granted size");
  }

  bool drew = false;
  EH_ST_TRACE(std::cerr << "dock layer_surface_configure: calling dock_draw");
  dock_draw(app, &drew);
  dock_sync_legacy_from_primary(app);
  if (!app.configured) return;
  if (!drew) {
    EH_ST_TRACE(std::cerr << "dock layer_surface_configure: dock_draw did not commit; wl_surface_commit(surface)");
    if (L->surface) wl_surface_commit(L->surface);
  }
}

static void dock_output_layer_closed(void* data, zwlr_layer_surface_v1*  ) {
  MANGOWM_FN();
  auto* L = static_cast<DockOutputLayer*>(data);
  if (L && L->dock) {
    std::cerr << "[dock] zwlr_layer_surface_v1 closed (compositor removed dock layer) → recreating\n";
    dock_rebind_layer_outputs(*L->dock);
  }
}

static const zwlr_layer_surface_v1_listener g_dock_output_layer_listener = {
  .configure = dock_output_layer_configure,
  .closed = dock_output_layer_closed,
};

static void dock_sync_legacy_from_primary(DockApp& app) {
  if (!app.dockLayers.empty() && app.dockLayers[0]) {
    const DockOutputLayer& L = *app.dockLayers[0];
    app.surface = L.surface;
    app.layerSurface = L.layer;
    app.configuredWidth = L.configuredWidth;
    app.configuredHeight = L.configuredHeight;
    app.configured = L.configured;
  } else {
    app.surface = nullptr;
    app.layerSurface = nullptr;
    app.configuredWidth = 0;
    app.configuredHeight = 0;
    app.configured = false;
  }
}

bool dock_pointer_on_any_dock_layer(const DockApp& app) {
  MANGOWM_FN();
  if (!app.pointerSurface) return false;
  if (dock_layer_from_surface(app, app.pointerSurface)) return true;
  return app.dockLayers.empty() && app.pointerSurface == app.surface;
}

zwlr_layer_surface_v1* dock_popup_parent_layer_surface(DockApp& app) {
  MANGOWM_FN();
  if (DockOutputLayer* L = dock_layer_from_surface(app, app.pointerSurface))
    if (L->layer) return L->layer;
  if (app.pointerDockLayerIdx < app.dockLayers.size() && app.dockLayers[app.pointerDockLayerIdx])
    return app.dockLayers[app.pointerDockLayerIdx]->layer;
  if (!app.dockLayers.empty() && app.dockLayers[0]) return app.dockLayers[0]->layer;
  return app.layerSurface;
}

wl_surface* dock_popup_parent_wl_surface(DockApp& app) {
  MANGOWM_FN();
  if (DockOutputLayer* L = dock_layer_from_surface(app, app.pointerSurface))
    if (L->surface) return L->surface;
  if (app.pointerDockLayerIdx < app.dockLayers.size() && app.dockLayers[app.pointerDockLayerIdx])
    return app.dockLayers[app.pointerDockLayerIdx]->surface;
  if (!app.dockLayers.empty() && app.dockLayers[0]) return app.dockLayers[0]->surface;
  return app.surface;
}
 
void dock_main_layers_set_keyboard_interactivity(DockApp& app, uint32_t mode) {
  MANGOWM_FN();
  for (auto& up : app.dockLayers) {
    if (up && up->layer) zwlr_layer_surface_v1_set_keyboard_interactivity(up->layer, mode);
  }
  if (app.dockLayers.empty() && app.layerSurface)
    zwlr_layer_surface_v1_set_keyboard_interactivity(app.layerSurface, mode);
}

static void dock_clear_dock_layers(DockApp& app) {
  MANGOWM_FN();

  const bool needs_strict_teardown = (app.compositorKind == CompositorKind::Mango ||
                                      app.compositorKind == CompositorKind::Unknown);

  eh::dock::drain_deferred_vk_drop(app);
 
  if (needs_strict_teardown) {
    eh::vk::VulkanDestructionQueue::instance().drain();

    if (app.display) wl_display_roundtrip(app.display);
  }

  for (auto& up : app.dockLayers) {
    if (!up) continue;

    if (up->vkLayer) {
      up->vkLayer->destroy();
      up->vkLayer.reset();
    }

    up->surfExt.destroy();

    // Destroy role objects before the wl_surface to avoid protocol errors.
    if (up->bgEffect) {
      ext_background_effect_surface_v1_destroy(up->bgEffect);
      up->bgEffect = nullptr;
    }
    if (up->layer) {
      zwlr_layer_surface_v1_destroy(up->layer);
      up->layer = nullptr;
    }
    if (up->surface) wl_surface_destroy(up->surface);
    up->surface = nullptr;
 
    if (needs_strict_teardown && app.display) wl_display_roundtrip(app.display);
    up->wlOut = nullptr;
    up->configured = false;
  }
  app.dockLayers.clear();
  app.surface = nullptr;
  app.layerSurface = nullptr;
  app.configured = false;
  app.configuredWidth = 0;
  app.configuredHeight = 0;
}

static bool dock_create_main_layer_surfaces(DockApp& app) {
  MANGOWM_FN();
  struct DockLayerCreateGuard {
    DockApp& a;
    explicit DockLayerCreateGuard(DockApp& app_) : a(app_) { ++a.layerSurfaceCreateDepth; }
    ~DockLayerCreateGuard() {
      --a.layerSurfaceCreateDepth;
      if (a.layerSurfaceCreateDepth != 0) return;
      while (a.pendingDockOutputRebind) {
        a.pendingDockOutputRebind = false;
        dock_rebind_layer_outputs(a);
      }
    }
  } guard(app);

  EH_ST_TRACE(std::cerr << "dock dock_create_main_layer_surfaces: begin");
  ShellBenchClock::time_point t_lc0{}, t_lc1{}, t_lc2{};
  const bool lc_split = shell_bench_should_log_layer_create_detail();
  if (lc_split) t_lc0 = ShellBenchClock::now();
  dock_clear_dock_layers(app);

  std::vector<wl_output*> targets = dock_collect_layer_target_outputs(app);
  if (targets.empty()) {
    if (wl_output* one = dock_pick_layer_output(app)) {
      targets.push_back(one);
    }
  }
  if (targets.empty()) {
    std::cerr << "Failed to create dock: no outputs available.\n";
    return false;
  }
  app.dockLayerOutput = targets[0];
  EH_ST_TRACE(std::cerr << "dock dock_create_main_layer_surfaces: primary output → " << static_cast<void*>(app.dockLayerOutput));


  std::vector<std::unique_ptr<DockOutputLayer>> created;
  for (size_t ti = 0; ti < targets.size(); ++ti) {
    wl_output* wlo = targets[ti];
    std::string outName;
    for (const auto& u : app.outputSlots)
      if (u && u->output == wlo) { outName = u->output_name; break; }

    auto L = std::make_unique<DockOutputLayer>();
    L->dock = &app;
    L->wlOut = wlo;
    eh::wayland::LayerSurfaceConfig cfg{};
    cfg.nameSpace = eh::shell::kDockNamespace;
    cfg.layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    cfg.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    cfg.width = 1;
    cfg.height = 1;
    cfg.exclusiveZone = app.settings.dockShowDock ? (app.autoHide ? -1 : app.dockHeight + dock_exclusive_zone_gap_px(app.settings)) : 0;
    cfg.marginTop = 0;
    cfg.marginRight = 0;
    cfg.marginBottom = dock_compositor_margin_bottom_px(app);
    cfg.marginLeft = 0;
    if (!eh::wayland::create_layer_surface(app.compositor, app.layerShell, wlo, cfg, &g_dock_output_layer_listener, L.get(),
                                           &L->surface, &L->layer)) {
      std::cerr << "Failed to create dock layer surface.\n";
      for (auto& up : created) {
        if (up->layer) zwlr_layer_surface_v1_destroy(up->layer);
        if (up->surface) wl_surface_destroy(up->surface);
      }
      return false;
    }
    dock_attach_output_layer_fractional_scale(app, *L);

    if (app.bgEffectMgr && L->surface) {
      L->bgEffect = ext_background_effect_manager_v1_get_background_effect(app.bgEffectMgr, L->surface);
    }

    EH_ST_TRACE(std::cerr << "dock: create layer surface=" << static_cast<void*>(L->surface) << " layer=" << static_cast<void*>(L->layer)
                                                       << " output=" << static_cast<void*>(wlo) << " name=\"" << outName << "\"");
    created.push_back(std::move(L));
  }
  for (auto& up : created) {
    if (up->surface) wl_surface_commit(up->surface);
  }
  if (app.display) wl_display_roundtrip(app.display);
  app.dockLayers = std::move(created);
  if (eh_layer_debug()) {
    std::cerr << "[layer-debug] dock_create_main_layer_surfaces: n_layers=" << app.dockLayers.size()
              << " configured=" << (app.configured ? 1 : 0)
              << " first_surface=" << (app.dockLayers.empty() ? 0 : (app.dockLayers[0] && app.dockLayers[0]->surface ? 1 : 0))
              << " configuredW=" << app.configuredWidth << "\n";
  }
  if (lc_split) t_lc1 = ShellBenchClock::now();
  dock_sync_legacy_from_primary(app);
  if (lc_split) t_lc2 = ShellBenchClock::now();

  EH_ST_TRACE(std::cerr << "dock: initial commit sent for all layers after creation configured="
                        << (app.configured ? 1 : 0));

  if (lc_split) {
    const auto t_lc3 = ShellBenchClock::now();
    shell_bench_mark_layer_create_detail_logged();
    std::cerr << "[dock-bench] detail layer_create: clear+create_layer_surface_loop=" << shell_bench_ms_between(t_lc0, t_lc1)
              << "ms sync_legacy_from_primary=" << shell_bench_ms_between(t_lc1, t_lc2)
              << "ms commit_all+wl_display_roundtrip=" << shell_bench_ms_between(t_lc2, t_lc3)
              << "ms (configure/dock_draw run inside roundtrip) subtotal=" << shell_bench_ms_between(t_lc0, t_lc3) << "ms\n";
  }

  EH_ST_TRACE(std::cerr << "dock: tray/app-menu/spotlight/CC/context menus use dedicated overlay layer surfaces\n");
  EH_ST_TRACE(std::cerr << "dock dock_create_main_layer_surfaces: done");
  return true;
}

static void dock_rebind_layer_outputs(DockApp& app) {
  MANGOWM_FN();
  popup_close(app);
  dock_destroy_app_menu_host_surfaces(app);
  if (app.display) wl_display_roundtrip(app.display);
  dock_clear_dock_layers(app);

  if (!dock_create_main_layer_surfaces(app)) {
    std::cerr << "[dock] rebind layer outputs failed\n";
    app.running = false;
    return;
  }


  if (app.desktopForOutputRebind) {
    eh::shell::desktop::desktop_create_layers(*app.desktopForOutputRebind);
  }

  app.sizeDirty = true;
  dock_draw(app);
}

static const DockOutputSlot* dock_output_slot_for(const DockApp& app, const wl_output* out) {
  if (!out) return nullptr;
  for (const auto& u : app.outputSlots) {
    if (u && u->output == out) return u.get();
  }
  return nullptr;
}

static int dock_max_strip_width_px(const DockApp& app) {
  constexpr int kFallbackMax = 4800;
  const std::string raw = eh::shell::trim_output_assign(app.settings.outputName);
  if (eh::shell::output_assign_is_all_displays(raw)) {
    int m = 0;
    for (const auto& u : app.outputSlots) {
      if (u && u->ready && u->logical_w > 0) m = std::max(m, u->logical_w);
    }
    return m > 0 ? m : kFallbackMax;
  }
  const DockOutputSlot* mon = dock_output_slot_for(app, app.dockLayerOutput);
  if (mon && mon->ready && mon->logical_w > 0) return mon->logical_w;
  if (app.primaryOutputWidthPx > 0) return app.primaryOutputWidthPx;
  return kFallbackMax;
}

bool dock_init_on_display(DockApp& app, wl_display* display) {
  MANGOWM_FN();
  MANGOWM_INFO("dock_init_on_display app=%p display=%p", (void*)&app, (void*)display);
  EH_ST_TRACE(std::cerr << "dock_init_on_display: begin");
  ShellBenchClock::time_point bench_t0{};
  if (eh_dock_bench()) {
    bench_t0 = ShellBenchClock::now();
    shell_bench_set_init_t0(bench_t0);
    app.dockBenchLoggedFirstCommit = false;
    std::cerr << "[dock-bench] ─── dock_init_on_display begin ───\n";
  }
  app.display = display;
  app.registry = wl_display_get_registry(app.display);
  if (!app.registry) return false;
  wl_registry_add_listener(app.registry, &g_registry_listener, &app);
  wl_display_flush(app.display);
  EH_ST_TRACE(std::cerr << "dock_init: wait for globals async (compositor+shm+layerShell)");
  // Non-blocking poll loop: dispatch events as they arrive, checking for essential globals.
  // Other init work can be done between poll slices (handled by caller).
  while (!app.compositor || !app.shm || !app.layerShell) {
    if (wl_display_prepare_read(app.display) == 0) {
      struct pollfd pfd = {wl_display_get_fd(app.display), POLLIN, 0};
      int r = poll(&pfd, 1, -1);
      if (r < 0) return false;
      wl_display_read_events(app.display);
    }
    wl_display_dispatch_pending(app.display);
  }
  if (eh_dock_bench()) std::cerr << "[dock-bench] after globals_ready cumulative=" << shell_bench_ms_since(bench_t0) << "ms\n";

  app.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

  // Bind all deferred globals now — compositor only processed 4 binds during roundtrip.
  dock_bind_deferred_globals(app);
  dock_bind_xdg_all_slots(app);
  dock_foreign_toplevel_bind_manager_and_hooks(app);
  dock_ext_toplevels_set_changed_cb(app);

  // Flush+dispatch to receive events from deferred binds.
  wl_display_flush(app.display);
  wl_display_dispatch(app.display);
  if (eh_dock_bench()) std::cerr << "[dock-bench] after flush_dispatch cumulative=" << shell_bench_ms_since(bench_t0) << "ms\n";

  app.compositorKind = detect_compositor_kind();
  EH_ST_TRACE(std::cerr << "dock_init: compositorKind=" << compositor_kind_cstr(app.compositorKind));
  if (eh_dock_toplevel_debug()) {
    std::cout << "[dock] compositor=" << compositor_kind_cstr(app.compositorKind) << "\n";
  }

  if (!app.compositor || !app.shm || !app.layerShell) {
    std::cerr << "Missing required Wayland globals. Need wl_compositor, wl_shm, zwlr_layer_shell_v1.\n";
    return false;
  }

  if (!dock_create_main_layer_surfaces(app)) return false;
  if (eh_dock_bench()) std::cerr << "[dock-bench] after dock_create_main_layer_surfaces cumulative=" << shell_bench_ms_since(bench_t0) << "ms\n";

  if (eh_verbose_enabled()) {
    std::cout << "Dock running. Hover bottom edge to reveal.\n";
    std::cout << "[dock-settings] EH_SETTINGS_DEBUG=1 → extra inotify/skip/wl_buffer logs\n";
  }
  EH_VERBOSE_LOG(std::cerr << "[dock] MPRIS/media + tray D-Bus + bufferless-draw hints on stderr (grep: [mpris][dbus] [dock][mpris] [dock] [dbus][tray]).\n");

  EH_ST_TRACE(std::cerr << "dock_init: maybe_reload_settings(startup)");
  dock_maybe_reload_settings(app, "startup");
  // Pre-warm icon theme search dirs now so they're ready by the time
  // dock_warm_startup_caches() runs on the first idle tick.
  app.icons.prewarm_search_dirs();
  if (eh_dock_bench()) {
    std::cerr << "[dock-bench] after maybe_reload_settings(startup) cumulative=" << shell_bench_ms_since(bench_t0)
              << "ms (config parse, pinned apps, sync_dock_chrome, widget-bar layout + dock_draw)\n";
  }
  {
    const std::chrono::milliseconds d = eh::shell::tray_session_defer_delay();
    if (d.count() > 0) {
      app.trayWatcherDeferPending = true;
      app.trayWatcherDeferUntil = std::chrono::steady_clock::now() + d;
      EH_ST_TRACE(std::cerr << "dock_init: tray session services deferred");
    } else {
      eh::tray::TrayManager::instance().start();
      dock_tray_init(app);
      if (eh_dock_bench() && shell_bench_have_init_t0()) {
        std::cerr << "[dock-bench] after TrayManager start (StatusNotifier D-Bus) cumulative=" << shell_bench_ms_since(shell_bench_init_t0())
                  << "ms\n";
      }
    }
  }

  EH_ST_TRACE(std::cerr << "dock_init_on_display: end configured=" << (app.configured ? 1 : 0) << " sizeDirty=" << (app.sizeDirty ? 1 : 0));
  return true;
}

// Called by the weather async engine when a forecast update completes.
static void dock_cc_weather_redraw(void* ctx) {
  auto& app = *static_cast<DockApp*>(ctx);
  if (app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter && app.popupSurface) {
    std::cerr << "[dock-popup] weather_redraw hook → popup_draw_surface\n";
    popup_draw_surface(app);
  }
}

void dock_init_deferred_startup(DockApp& app) {
  MANGOWM_FN();
  ShellBenchClock::time_point bench_t0{};
  if (eh_dock_bench()) {
    bench_t0 = ShellBenchClock::now();
    std::cerr << "[dock-bench] ─── dock_init_deferred_startup begin ───\n";
  }
  try {
    app.mpris = std::make_unique<eh::mpris::DockMpris>();
  } catch (const std::exception& e) {
    eh::shell_log::dock_mpris("DockMpris init failed: ", e.what());
    app.mpris.reset();
  } catch (...) {
    eh::shell_log::dock_mpris("DockMpris init failed: unknown exception");
    app.mpris.reset();
  }
  if (app.mpris) {
    EH_ST_TRACE(std::cerr << "dock_deferred: mpris poll_refresh");
    (void)app.mpris->poll_refresh();
    app.sizeDirty = true;
  }
  if (eh_dock_bench()) {
    std::cerr << "[dock-bench] deferred after mpris cumulative=" << shell_bench_ms_since(bench_t0) << "ms\n";
  }
  if (!eh::shell::osd::osd_env_disabled()) {
    app.osdHost = std::make_unique<eh::shell::osd::OsdHost>();
    app.osdHost->init(app);
    eh::shell::osd::osd_audio_bind(app);
    eh::shell::osd::osd_audio_apply_saved_defaults(app);
    eh::shell::osd::osd_audio_poll_pending(app);
  }
  eh::shell::dock_slot_hooks::control_center_weather_startup_dock(&app, dock_cc_weather_redraw);
  eh::shell::dock_slot_hooks::battery_widget_init();
  eh::shell::dock_slot_hooks::bluetooth_widget_init();
  dock_warm_startup_caches(app);
  if (eh_dock_bench()) {
    std::cerr << "[dock-bench] deferred startup total=" << shell_bench_ms_since(bench_t0) << "ms\n";
  }
}

void dock_install_loop_fds(DockApp& app) {
  MANGOWM_FN();
  if (app.settingsInotifyFd < 0) {
    app.settingsInotifyFd = dock_open_settings_inotify();
  }

  if (app.pollTimerFd < 0) {

    app.pollTimerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (app.pollTimerFd >= 0) {
      if (app.settingsInotifyFd < 0) {
        std::cerr << "[dock-settings] using 500ms poll fallback (no inotify fd)\n";
      }
      dock_apply_poll_timer_interval(app);
    }
  }

  if (app.mediaAnimTimerFd < 0) {
    app.mediaAnimTimerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  }
}

void dock_handle_timer(DockApp& app) {
  MANGOWM_FN();
  if (app.pollTimerFd < 0) return;
  eh::shell::osd::osd_brightness_poll(app);
  eh::shell::osd::osd_audio_poll_pending(app);
  dock_try_start_deferred_tray(app);
  uint64_t expirations = 0;
  (void)read(app.pollTimerFd, &expirations, sizeof(expirations));
  if (app.deferDockRedraw && !app.frameCallback) schedule_frame(app);
  bool drew = false;
  bool drewDock = false;

  static std::chrono::steady_clock::time_point s_next_poll_settings_check{};
  const auto poll_now = std::chrono::steady_clock::now();
  if (poll_now >= s_next_poll_settings_check) {
    s_next_poll_settings_check = poll_now + std::chrono::milliseconds(400);
    dock_maybe_reload_settings(app, "poll");
    // System theme switches never touch our settings file: poll the cache's
    // auto-follow so dock icons track dark/light toggles like everything else.
    if (app.settings.iconTheme.empty() && app.icons.refresh_auto_theme_if_needed()) {
      app.sizeDirty = true;
      dock_draw(app);
    }
  }
  const std::string clockId = dock_first_clock_widget_id(app.settings);
  if (!clockId.empty()) {
    const auto& sc = eh::config::shell_config_snapshot();
    if (eh::shell::dock_slot_hooks::clock_tick_signature_changed(app.dockClockTickSignature, sc, clockId)) {
      drewDock = true;
      drew = true;
    }
  }
  const std::string worldClockId = dock_first_world_clock_widget_id(app.settings);
  if (!worldClockId.empty()) {
    const auto& sc = eh::config::shell_config_snapshot();
    if (eh::shell::dock_slot_hooks::world_clock_tick_signature_changed(app.dockWorldClockTickSignature, sc, worldClockId)) {
      drewDock = true;
      drew = true;
    }
  }
  if (!dock_first_media_widget_id(app.settings).empty() && app.mpris) {
    bool mpris_changed = false;
    try {
      mpris_changed = app.mpris->poll_refresh();
    } catch (const std::exception& e) {
      eh::shell_log::dock_mpris("poll_refresh escaped (should not): ", e.what());
    } catch (...) {
      eh::shell_log::dock_mpris("poll_refresh escaped (should not): unknown");
    }
    if (mpris_changed) {
      eh::shell_log::dock_mpris("state changed → redraw configured=", app.configured ? 1 : 0, " sizeDirty=", app.sizeDirty ? 1 : 0,
                                " cfg=", app.configuredWidth, "x", app.configuredHeight);
      if (app.configured) {
        eh::shell::dock::dock_ensure_workspace_strip(app);
        const int intrinsic = dock_compute_widget_strip_width(app, app.settings.leftWidgets, app.settings.centerWidgets,
                                                              app.settings.rightWidgets);
        if (intrinsic != app.dockLastIntrinsicStripWidth) app.sizeDirty = true;
        eh::shell_log::dock_mpris("strip intrinsic_w=", intrinsic, " last=", app.dockLastIntrinsicStripWidth,
                                  " sizeDirty=", app.sizeDirty ? 1 : 0);
      }
      drewDock = true;
      drew = true;
    }
  }
  eh::shell::dock_slot_hooks::battery_widget_poll();
  if (eh::shell::dock_slot_hooks::bluetooth_widget_poll()) {
    drewDock = true;
    drew = true;
  }
  const std::string ccId = dock_first_control_center_widget_id(app.settings);
  if (!ccId.empty()) {
    const auto& sc = eh::config::shell_config_snapshot();
    if (eh::shell::dock_slot_hooks::control_center_tick_signature_changed(app.dockControlCenterTickSignature, sc, ccId)) {
      drewDock = true;
      drew = true;
    }
  }
  dock_tooltip_tick(app);

  if (drewDock) dock_draw(app);
  if (drew && app.popupOpen && app.popupSurface) {
    popup_draw_surface(app);
    wl_display_flush(app.display);
  }
}

void dock_handle_inotify(DockApp& app) {
  MANGOWM_FN();
  if (app.settingsInotifyFd < 0) return;
  dock_drain_settings_inotify(app, app.settingsInotifyFd);
  if (app.settings.iconTheme.empty() && app.icons.refresh_auto_theme_if_needed()) {
    app.sizeDirty = true;
    dock_draw(app);
  }
}

void dock_handle_tray(DockApp& app) {
  MANGOWM_FN();
  EH_ST_TRACE(std::cerr << "dock_handle_tray: enter configured=" << (app.configured ? 1 : 0) << " cfg=" << app.configuredWidth << "x"
                                                    << app.configuredHeight);
  uint64_t v = 0;
  while (read(app.trayEventFd, &v, sizeof(v)) > 0) {}
  dock_tray_sync_items(app);
  app.sizeDirty = true;
  app.trayStripNeedsLayerAck = true;
  dock_draw(app);
  EH_ST_TRACE(std::cerr << "dock_handle_tray: after dock_draw configured=" << (app.configured ? 1 : 0));
}

void dock_after_display_dispatch(DockApp& app) {
  MANGOWM_FN();

  eh::dock::drain_deferred_vk_drop(app);

  // Check for immediate Bluetooth redraw (catches device discovery between timer ticks)
  if (eh::shell::dock_slot_hooks::bluetooth_widget_needs_immediate_draw()) {
    app.deferDockRedraw = true;
  }

  if (app.deferDockRedraw) {
    if (!app.settings.dockShowDock && !app.popupOpen) {
      app.deferDockRedraw = false;
      return;
    }
    // Rate-limit redraws: cap at ~30fps (33ms) to avoid GPU contention
    // during event bursts (e.g., RS3/Proton toplevel floods on high-refresh displays)
    auto now = std::chrono::steady_clock::now();
    constexpr auto kMinFrameGap = std::chrono::milliseconds(33);
    if (now - app.lastDockDrawTime < kMinFrameGap) {
      // Still rate-limited — sleep briefly to avoid busy-spinning.
      // The display fd is always readable during Proton floods, so ppoll
      // returns immediately; a voluntary sleep caps loop iterations.
      usleep(5000); // 5ms
      return;
    }
    app.deferDockRedraw = false;
    app.lastDockDrawTime = now;
    EH_ST_TRACE(std::cerr << "dock_after_display_dispatch: deferDockRedraw → schedule_frame");
    dock_schedule_frame(app);
  }
}

void dock_cleanup(DockApp& app, bool disconnect_display) {
  MANGOWM_FN();
  MANGOWM_INFO("dock_cleanup app=%p disconnect=%d", (void*)&app, (int)disconnect_display);

  // If display is in a fatal error state, any Wayland proxy operation will
  // segfault. Skip all Wayland destroy calls — the OS will free resources.
  if (app.display && wl_display_get_error(app.display)) {
    app.display = nullptr;
    // Still clean up non-Wayland resources (threads, timers, etc.).
    eh::shell::osd::osd_audio_shutdown();
    dock_tooltip_cleanup(app);
    app.trayWatcherDeferPending = false;
    dock_tray_shutdown(app);
    app.dockFastStartDidPlaceholder = false;
    app.dockFastStartDidFullPaint = false;
    eh::shell::dock_slot_hooks::control_center_weather_service_stop();
    eh::shell::dock_slot_hooks::battery_widget_shutdown();
    eh::shell::dock_slot_hooks::bluetooth_widget_shutdown();
    app.mpris.reset();
    if (app.pollTimerFd >= 0) close(app.pollTimerFd);
    if (app.mediaAnimTimerFd >= 0) close(app.mediaAnimTimerFd);
    app.mediaAnimTimerFd = -1;
    if (app.settingsInotifyFd >= 0) close(app.settingsInotifyFd);
    popup_close(app);
    dock_destroy_app_menu_host_surfaces(app);
    eh::dock::abort_vk_backend(app);
    // Destroy deferred Vulkan resources synchronously (no Wayland roundtrip
    // needed — display is already broken).
    app.deferredVkLayers.clear();
    app.deferredVkDrop.reset();
    app.popupVkLayer.reset();
    app.dockLayers.clear();
    app.outputSlots.clear();
    if (app.xkbState) xkb_state_unref(app.xkbState);
    if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
    if (app.xkbCtx) xkb_context_unref(app.xkbCtx);
    return;
  }

  eh::shell::osd::osd_audio_shutdown();
  if (app.osdHost) {
    app.osdHost->shutdown();
    app.osdHost.reset();
  }
  dock_tooltip_cleanup(app);
  app.trayWatcherDeferPending = false;
  dock_tray_shutdown(app);
  app.dockFastStartDidPlaceholder = false;
  app.dockFastStartDidFullPaint = false;
  eh::shell::dock_slot_hooks::control_center_weather_service_stop();
  eh::shell::dock_slot_hooks::battery_widget_shutdown();
  eh::shell::dock_slot_hooks::bluetooth_widget_shutdown();
  app.mpris.reset();
  if (app.pollTimerFd >= 0) close(app.pollTimerFd);
  app.pollTimerFd = -1;
  if (app.mediaAnimTimerFd >= 0) close(app.mediaAnimTimerFd);
  app.mediaAnimTimerFd = -1;
  if (app.settingsInotifyFd >= 0) close(app.settingsInotifyFd);
  app.settingsInotifyFd = -1;
  popup_close(app);
  dock_destroy_app_menu_host_surfaces(app);

  for (auto& u : app.outputSlots) {
    if (u && u->xdg) {
  zxdg_output_v1_destroy(u->xdg);
      u->xdg = nullptr;
    }
  }
  app.outputSlots.clear();
  app.dockLayerOutput = nullptr;
  if (app.xdgOutputManager) {
    zxdg_output_manager_v1_destroy(app.xdgOutputManager);
    app.xdgOutputManager = nullptr;
  }

  dock_clear_dock_layers(app);
  // Destroy deferred/popup Vulkan resources synchronously — they would
  // otherwise be freed during ~DockApp / ~WaylandState *after* the Wayland
  // display is disconnected, causing the NVIDIA driver to crash when it
  // internally makes Wayland protocol calls during swapchain teardown.
  app.deferredVkLayers.clear();
  app.deferredVkDrop.reset();
  app.popupVkLayer.reset();
  app.toplevels.shutdown();
  app.toplevelManager = nullptr;
  if (app.keyboard) wl_keyboard_destroy(app.keyboard);
  if (app.xkbState) xkb_state_unref(app.xkbState);
  if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
  if (app.xkbCtx) xkb_context_unref(app.xkbCtx);
  if (app.pointer) wl_pointer_destroy(app.pointer);
  if (app.seat) wl_seat_destroy(app.seat);
  if (app.layerShell) zwlr_layer_shell_v1_destroy(app.layerShell);
  if (app.fractionalScaleMgr) {
    wp_fractional_scale_manager_v1_destroy(app.fractionalScaleMgr);
    app.fractionalScaleMgr = nullptr;
  }
  if (app.viewporter) {
    wp_viewporter_destroy(app.viewporter);
    app.viewporter = nullptr;
  }
  if (app.shm) wl_shm_destroy(app.shm);
  if (app.compositor) wl_compositor_destroy(app.compositor);
  if (app.registry) wl_registry_destroy(app.registry);
  app.registry = nullptr;
  if (disconnect_display && app.display) wl_display_disconnect(app.display);
  app.display = nullptr;
}
