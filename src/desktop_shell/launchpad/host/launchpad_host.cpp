#include "desktop_shell/launchpad/host/launchpad_host.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/shared/popup/chrome/chrome.hpp"
#include "desktop_shell/shared/popup/geometry/margins.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/widgets/app_drawer/overlay/app_drawer_overlay.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_exec.hpp"
#include "desktop_shell/widgets/app_drawer/power/app_drawer_power_modal.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "desktop_shell/common/fs/string_util.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/common/fs/shell_paths.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/common/bench/shell_bench.hpp"
#include "wl/surface/layer_surface.hpp"
#include "configuration/shell_config.hpp"

#include "m3/core/primitives/box.hpp"

#include "viewporter-client-protocol.h"

#include <cairo/cairo.h>

#include <wayland-client.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

static void lp_log(std::string_view msg) {
  static FILE* f = nullptr;
  if (!f) f = fopen("/tmp/eh-launchpad.log", "w");
  if (f) { fprintf(f, "%s\n", msg.data()); fflush(f); }
}
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <sys/inotify.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/animation/animations.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/shared/pins/pin_identity.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <toml++/toml.hpp>

namespace eh::shell::launchpad {

// Asset path helpers.

namespace {

struct BenchScope {
  const char* name;
  uint64_t t0;
  int level;
  BenchScope(const char* n, int lv = 3) : name(n), t0(eh::shell::now_mono_ms()), level(lv) {}
  ~BenchScope() {
    if (eh_app_drawer_debug_level() < level) return;
    const auto dt = eh::shell::now_mono_ms() - t0;
    if (dt >= 1)
      eh::shell::dock::app_drawer::trace_line(level, "launchpad-bench",
          std::string(name) + " " + std::to_string(dt) + "ms");
  }
  void checkpoint(const char* label) {
    if (eh_app_drawer_debug_level() < level) return;
    const auto dt = eh::shell::now_mono_ms() - t0;
    if (dt >= 1)
      eh::shell::dock::app_drawer::trace_line(level, "launchpad-bench",
          std::string(name) + " [" + label + "] " + std::to_string(dt) + "ms");
  }
};

// Returns the directory that holds Dark_Launchpad.png / Light_Launchpad.png.
// Follows the same search order as load_asset_png() in asset_loader.cpp.
std::filesystem::path launchpad_assets_dir() {
  // Prefer an explicit env-var override (handy during development).
  if (const char* env = ::getenv("EH_ASSETS_DIR"))
    return std::filesystem::path(env);

  // Check CWD-relative paths (most common during development).
  for (const auto& rel : {"assets", "src/assets"}) {
    std::error_code ec;
    if (std::filesystem::is_directory(rel, ec))
      return std::filesystem::absolute(rel, ec);
  }

  // Walk up from the binary location.  In an installed layout the assets
  // live under <prefix>/share/event-horizon/assets/.
  std::error_code ec;
  auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    // Try relative to the executable.
    for (const auto& rel : {"../assets", "../src/assets"}) {
      auto cand = exe.parent_path() / rel;
      if (std::filesystem::is_directory(cand, ec))
        return cand;
    }
    // Installed layout:  <prefix>/bin/EventHorizon
    //                    <prefix>/share/event-horizon/assets/
    auto inst = exe.parent_path().parent_path() / "share" / "event-horizon" / "assets";
    if (std::filesystem::is_directory(inst, ec))
      return inst;
  }

  // Final hard-coded fallback.
  return std::filesystem::path("/usr/local/share/event-horizon/assets");
}

// Dock-button icon cache: process-local, shared between the launchpad Host
// and the supervisor taskbar via paint_launchpad_dock_button.

struct DockButtonIcons {
  cairo_surface_t* dark = nullptr;
  cairo_surface_t* light = nullptr;
  bool loaded = false;
  ~DockButtonIcons() {
    if (dark) cairo_surface_destroy(dark);
    if (light) cairo_surface_destroy(light);
  }
};

DockButtonIcons g_dock_button_icons;

void ensure_dock_button_icons() {
  if (g_dock_button_icons.loaded) return;
  g_dock_button_icons.loaded = true;  // Set first — even if loading fails we don't retry on every frame.

  const auto dir = launchpad_assets_dir();

  auto try_load = [&](const char* filename) -> cairo_surface_t* {
    const auto path = (dir / filename).string();
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
      if (s) cairo_surface_destroy(s);
      std::cerr << "[launchpad] could not load dock icon: " << path << "\n";
      return nullptr;
    }
    {
      const int iw = cairo_image_surface_get_width(s);
      const int ih = cairo_image_surface_get_height(s);
      constexpr int kMaxDim = 48;
      if (iw > kMaxDim || ih > kMaxDim) {
        const double sc = static_cast<double>(kMaxDim) / std::max(iw, ih);
        const int dw = std::max(1, static_cast<int>(std::lround(iw * sc)));
        const int dh = std::max(1, static_cast<int>(std::lround(ih * sc)));
        cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
        cairo_t* cr2 = cairo_create(scaled);
        cairo_scale(cr2, sc, sc);
        cairo_set_source_surface(cr2, s, 0.0, 0.0);
        cairo_pattern_set_filter(cairo_get_source(cr2), CAIRO_FILTER_GOOD);
        cairo_paint(cr2);
        cairo_destroy(cr2);
        cairo_surface_destroy(s);
        s = scaled;
      }
    }
    return s;
  };

  g_dock_button_icons.dark = try_load("Dark_Launchpad.png");
  g_dock_button_icons.light = try_load("Light_Launchpad.png");
}

// Animation / layout helpers (unchanged from the original).

[[nodiscard]] float launchpad_page_slide_duration_ms(const DockApp& dock) {
  constexpr float kRefHz = 60.f;
  constexpr float kRefMs = 320.f;
  const DockOutputSlot* pick = nullptr;
  {
    auto it = std::find_if(dock.outputSlots.begin(), dock.outputSlots.end(),
                           [&dock](const auto& u) {
                             return u && u->output && dock.dockLayerOutput
                                 && u->output == dock.dockLayerOutput;
                           });
    if (it != dock.outputSlots.end()) pick = it->get();
  }
  if (!pick) {
    auto it = std::find_if(dock.outputSlots.begin(), dock.outputSlots.end(),
                           [](const auto& u) {
                             return u && u->current_mode_refresh_mHz > 0;
                           });
    if (it != dock.outputSlots.end()) pick = it->get();
  }
  float hz = 60.f;
  if (pick && pick->current_mode_refresh_mHz > 0)
    hz = static_cast<float>(pick->current_mode_refresh_mHz) / 1000.f;
  hz = std::clamp(hz, 30.f, 360.f);
  return kRefMs * (kRefHz / hz);
}

void launchpad_resolve_configure_size(const DockApp& dock, int w, int h, int* out_w, int* out_h) {
  int nw = w > 0 ? w : 0;
  int nh = h > 0 ? h : 0;
  if ((nw <= 0 || nh <= 0) && dock.dockLayerOutput) {
    for (const auto& u : dock.outputSlots) {
      if (u && u->output == dock.dockLayerOutput && u->ready && u->logical_w > 0 && u->logical_h > 0) {
        if (nw <= 0) nw = u->logical_w;
        if (nh <= 0) nh = u->logical_h;
        break;
      }
    }
  }
  if (nw <= 0 && dock.primaryOutputWidthPx > 0) nw = dock.primaryOutputWidthPx;
  if (nh <= 0 && dock.primaryOutputHeightPx > 0) nh = dock.primaryOutputHeightPx;
  *out_w = nw;
  *out_h = nh;
}

constexpr uint32_t kPointerVersion = 5u;

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

} // anonymous namespace

// File-backed log for crash diagnosis.


// Dock button paint / hit-test.

// Shared paint-only dock button (see launchpad_host.hpp). Pure paint — safe for
// the supervisor's taskbar now that the launchpad Host lives in horizon-dock.
void paint_launchpad_dock_button(cairo_t* cr, double x, double y, double size, bool hovered) {
  ensure_dock_button_icons();

  // Choose dark or light variant based on the dock background luminance.
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  const auto mc = eh::config::derived_chrome_colors(ap);
  const double luma = 0.299 * mc.dockFillR + 0.587 * mc.dockFillG + 0.114 * mc.dockFillB;
  const bool darkBg = luma <= 0.5;
  cairo_surface_t* icon = darkBg ? g_dock_button_icons.dark : g_dock_button_icons.light;

  // Draw a subtle hover highlight behind the icon - glassy
  if (hovered) {
    cairo_save(cr);
    const double pad = size * 0.08;
    const double r   = (size - pad * 2.0) * 0.22;
    {
      m3::Box hl;
      hl.setColor(static_cast<float>(mc.accentR), static_cast<float>(mc.accentG),
                  static_cast<float>(mc.accentB), 0.18f);
      hl.setRadius(static_cast<float>(r));
      hl.setGeometry(x + pad, y + pad, size - pad * 2.0, size - pad * 2.0);
      hl.setGlassy(true);
      hl.paint(cr);
    }
    cairo_restore(cr);
  }

  if (!icon) {
    // Fallback: draw a simple grid of dots so there is always something visible.
    cairo_save(cr);
    const double pr = size * 0.065;
    const double margin = size * 0.22;
    const double gap = (size - margin * 2.0 - pr * 6.0) / 2.0;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col) {
        const double cx = x + margin + col * (pr * 2.0 + gap) + pr;
        const double cy = y + margin + row * (pr * 2.0 + gap) + pr;
        cairo_arc(cr, cx, cy, pr, 0, 2.0 * M_PI);
      }
    }
    cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, hovered ? 1.0 : 0.82);
    cairo_fill(cr);
    cairo_restore(cr);
    return;
  }

  // Scale the PNG to fit the requested size, centred.
  cairo_save(cr);
  const double iw = static_cast<double>(cairo_image_surface_get_width(icon));
  const double ih = static_cast<double>(cairo_image_surface_get_height(icon));
  const double scale = size / std::max(1.0, std::max(iw, ih));
  const double ox = x + (size - iw * scale) * 0.5;
  const double oy = y + (size - ih * scale) * 0.5;
  cairo_translate(cr, ox, oy);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, icon, 0.0, 0.0);
  cairo_paint_with_alpha(cr, hovered ? 1.0 : 0.88);
  cairo_restore(cr);
}

void Host::paint_dock_button(cairo_t* cr, double x, double y, double size, bool hovered) const {
  paint_launchpad_dock_button(cr, x, y, size, hovered);
}

bool Host::hit_test_dock_button(double px, double py,
                                double btn_x, double btn_y,
                                double btn_size) const noexcept {
  return px >= btn_x && px < btn_x + btn_size &&
         py >= btn_y && py < btn_y + btn_size;
}

// Layer-surface listeners.

void Host::layer_configure(void* data, zwlr_layer_surface_v1* surface, uint32_t serial, uint32_t w, uint32_t h) {
   
  auto& self = *static_cast<Host*>(data);
  if (surface != self.layer_ && surface != self.backdrop_layer_) return;
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  int nw = w > 0 ? static_cast<int>(w) : self.w_;
  int nh = h > 0 ? static_cast<int>(h) : self.h_;
  launchpad_resolve_configure_size(self.dock_, nw, nh, &nw, &nh);
  if (nw > 0) self.w_ = nw;
  if (nh > 0) self.h_ = nh;
  if (surface == self.backdrop_layer_)
    self.paint_backdrop();
  else if (surface == self.layer_)
    self.paint_content();
  if (self.wl_) wl_display_flush(self.wl_->display());
}

void Host::layer_closed(void*, zwlr_layer_surface_v1*) {}

void launchpad_host_page_frame_done(void* data, wl_callback* cb, uint32_t /*compositor_time_ms*/) {
  auto* self = static_cast<Host*>(data);
  wl_callback_destroy(cb);
  self->page_frame_cb_ = nullptr;
  if (!self->open_ || !self->surface_) return;
  self->page_anim_.tick();
  self->hover_anim_.tick();
  self->repaint_all();
  // Keep a frame callback pending while the launchpad is open. This ensures
  // that changes to launcher grid settings (Columns for "items in the top row",
  // Rows for visible rows) are picked up quickly and the grid (including
  // folder cards) is re-laid out with the new density. Folders count exactly
  // like regular icons in the grid slots.
  if (self->open_) self->schedule_page_frame();
}

const wl_callback_listener kLaunchpadPageFrameListener = {
  .done = launchpad_host_page_frame_done,
};

// Seat listeners.

void Host::seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
  auto& self = *static_cast<Host*>(data);
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !self.pointer_) {
    self.pointer_ = wl_seat_get_pointer(seat);
    if (self.pointer_) {
      wl_pointer_set_user_data(self.pointer_, &self);
      wl_pointer_add_listener(self.pointer_, &kPointerListener, &self);
    }
  } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && self.pointer_) {
    wl_pointer_release(self.pointer_);
    self.pointer_ = nullptr;
    self.pointer_focus_ = false;
  }
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !self.keyboard_) {
    self.keyboard_ = wl_seat_get_keyboard(seat);
    if (self.keyboard_) {
      wl_keyboard_set_user_data(self.keyboard_, &self);
      wl_keyboard_add_listener(self.keyboard_, &kKeyboardListener, &self);
    }
  } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && self.keyboard_) {
    if (self.xkbState_)  { xkb_state_unref(self.xkbState_);  self.xkbState_  = nullptr; }
    if (self.xkbKeymap_) { xkb_keymap_unref(self.xkbKeymap_); self.xkbKeymap_ = nullptr; }
    wl_keyboard_release(self.keyboard_);
    self.keyboard_ = nullptr;
  }
}

void Host::seat_name(void*, wl_seat*, const char*) {}

// Pointer listeners.

void Host::pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                         wl_fixed_t surface_x, wl_fixed_t surface_y) {
  auto& self = *static_cast<Host*>(data);
  self.handle_pointer_enter(surface, wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
}

void Host::pointer_leave(void* data, wl_pointer*, uint32_t, wl_surface*) {
  auto& self = *static_cast<Host*>(data);
  self.handle_pointer_leave();
}

void Host::pointer_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t surface_x, wl_fixed_t surface_y) {
  auto& self = *static_cast<Host*>(data);
  self.handle_pointer_motion(wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y));
}

void Host::pointer_button(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
  auto& self = *static_cast<Host*>(data);
  self.handle_pointer_button(button, state);
}

void Host::pointer_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
  auto& self = *static_cast<Host*>(data);
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
    self.handle_pointer_axis(wl_fixed_to_double(value));
}

void Host::pointer_frame(void*, wl_pointer*) {}
void Host::pointer_axis_source(void*, wl_pointer*, uint32_t) {}
void Host::pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
void Host::pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}

// Keyboard listeners.

void Host::keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto& self = *static_cast<Host*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { ::close(fd); return; }
  char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map_str == MAP_FAILED) { ::close(fd); return; }
  auto* km = xkb_keymap_new_from_string(self.xkbCtx_, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
                                         XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  ::close(fd);
  if (!km) return;
  if (self.xkbState_)  xkb_state_unref(self.xkbState_);
  if (self.xkbKeymap_) xkb_keymap_unref(self.xkbKeymap_);
  self.xkbKeymap_ = km;
  self.xkbState_  = xkb_state_new(km);
}

void Host::keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void Host::keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

void Host::keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
  auto& self = *static_cast<Host*>(data);
  self.handle_key(key, state);
}

void Host::keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t mods_depressed,
                              uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  auto& self = *static_cast<Host*>(data);
  if (self.xkbState_)
    xkb_state_update_mask(self.xkbState_, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

void Host::keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

// Internal input handlers.

void Host::handle_pointer_enter(wl_surface* surface, double sx, double sy) {
   
  pointer_focus_ = owns_surface(surface);
  if (!pointer_focus_) return;
  ptr_x_ = sx;
  ptr_y_ = sy;
  if (folder_open_) return;
  const LaunchpadPaintModel m = paint_model();
  const int idx = eh_launchpad_pick_catalog_index(m, surface_, surface, sx, sy);
  if (idx != hover_idx_ && !power_confirm_open_) {
    hover_idx_ = idx;
    if (!page_frame_cb_) schedule_page_frame();
  }
}

void Host::handle_pointer_leave() {
   
  pointer_focus_ = false;
  if (folder_open_) {
    folder_hover_child_ = -1;
    folder_title_hovered_ = false;
  }
  if (hover_idx_ >= 0 && !power_confirm_open_) {
    hover_idx_ = -1;
    if (!open_) return;
    if (!page_frame_cb_) schedule_page_frame();
  }
}

// Drag threshold in ui-scaled pixels before drag starts.
constexpr double kDragThresholdPx = 16.0;

void Host::handle_pointer_motion(double sx, double sy) {
   
  BenchScope bench("ptr_motion", 4);
  if (!pointer_focus_) return;
  ptr_x_ = sx;
  ptr_y_ = sy;

  // Drag detection: if we have a drag candidate and moved past threshold
  if (drag_candidate_) {
    const double dx = sx - drag_start_x_;
    const double dy = sy - drag_start_y_;
    const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
    if (dx * dx + dy * dy > kDragThresholdPx * us * kDragThresholdPx * us) {
      drag_active_ = true;
      drag_candidate_ = false;
      if (!page_frame_cb_) schedule_page_frame();
    }
    return; // Don't update hover while a drag candidate is pending
  }

  if (drag_active_) {
    // During active drag, still update position for ghost rendering
    if (!page_frame_cb_) schedule_page_frame();
    return;
  }

  // Folder child drag detection
  if (folder_drag_candidate_) {
    const double dx = sx - folder_drag_start_x_;
    const double dy = sy - folder_drag_start_y_;
    const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
    if (dx * dx + dy * dy > kDragThresholdPx * us * kDragThresholdPx * us) {
      folder_drag_active_ = true;
      folder_drag_candidate_ = false;
      if (!page_frame_cb_) schedule_page_frame();
    }
    return;
  }

  if (folder_drag_active_) {
    if (!page_frame_cb_) schedule_page_frame();
    return;
  }

  // Context menu hover tracking
  if (ctx_open_) {
    double mx = 0, my = 0, mw = 0, mh = 0;
    constexpr int n = 3;
    eh::shell::dock::app_drawer::app_drawer_row_context_menu_layout(
        ctx_menu_x_, ctx_menu_y_, static_cast<double>(w_), static_cast<double>(h_), n, &mx, &my, &mw, &mh);
    const int hi = eh::shell::dock::app_drawer::app_drawer_row_context_menu_pick(sx, sy, mx, my, n);
    if (hi != ctx_hover_item_) {
      ctx_hover_item_ = hi;
      if (!page_frame_cb_) schedule_page_frame();
    }
    return;
  }

  if (folder_open_) {
    const LaunchpadPaintModel m = paint_model();
    const int child = eh_launchpad_pick_folder_child(m, sx, sy);
    const bool titleHover = eh_launchpad_pick_folder_title(m, sx, sy);
    bool changed = false;
    if (child != folder_hover_child_) {
      folder_hover_child_ = child;
      changed = true;
    }
    if (titleHover != folder_title_hovered_) {
      folder_title_hovered_ = titleHover;
      changed = true;
    }
    if (changed && !page_frame_cb_) schedule_page_frame();
    return;
  }

  const LaunchpadPaintModel m = paint_model();
  const int idx = eh_launchpad_pick_catalog_index(m, surface_, surface_, sx, sy);
  if (idx != hover_idx_ && !power_confirm_open_) {
    const int old_hover = hover_idx_;
    hover_idx_ = idx;
    // Start bounce animation on hover entry (scale-up then settle back)
    if (idx >= 0 && idx != old_hover) {
      hover_anim_.cancel_all();
      hover_anim_target_ = idx;
      hover_anim_scale_ = 1.0f;
      // Scale up.
      hover_anim_.animate(1.0f, 1.12f, 100.0f, eh::shell::Easing::EaseOutQuad,
          [this](float v) { hover_anim_scale_ = v; if (!page_frame_cb_) schedule_page_frame(); },
          [this]() {
            // Settle back to 1.0.
            hover_anim_.animate(1.12f, 1.0f, 200.0f, eh::shell::Easing::EaseOutCubic,
                [this](float v) { hover_anim_scale_ = v; if (!page_frame_cb_) schedule_page_frame(); },
                nullptr);
          });
    } else if (idx < 0) {
      hover_anim_.cancel_all();
      hover_anim_scale_ = 1.0f;
    }
    if (!page_frame_cb_) schedule_page_frame();
  }
  // Organize button hover
  {
    const AppDrawerHitZone z = eh_launchpad_hit_zone(m, surface_, surface_, sx, sy);
    const bool oh = (z == AppDrawerHitZone::OrganizeButton);
    if (oh != organize_hovered_) {
      organize_hovered_ = oh;
      if (!page_frame_cb_) schedule_page_frame();
    }
  }
}

void Host::handle_pointer_button(uint32_t button, uint32_t state) {
   
  BenchScope bench("ptr_button", 4);
  if (!open_ || !pointer_focus_ || !surface_) return;
  const bool left = (button == 0x110);
  const bool right = (button == 0x111 || button == 0x112 || button == 0x113);
  if (!left && !right) return;

  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    // Right-click: show context menu
    if (right) {
      if (ctx_open_) {
        ctx_open_ = false;
        ctx_anchor_row_ = -1;
        repaint_all();
        if (wl_) wl_display_flush(wl_->display());
        return;
      }
      LaunchpadPaintModel m = paint_model();
      if (folder_open_) return;
      if (backdrop_surface_ && pointer_focus_) {
        wl_surface* focus = surface_;
        if (focus == backdrop_surface_) {
          close();
          if (wl_) wl_display_flush(wl_->display());
          return;
        }
      }

      const AppDrawerHitZone zone = eh_launchpad_hit_zone(m, surface_, surface_, ptr_x_, ptr_y_);
      if (zone == AppDrawerHitZone::AppListRow) {
        const int row = eh_launchpad_pick_catalog_index(m, surface_, surface_, ptr_x_, ptr_y_);
        if (row >= 0 && row < static_cast<int>(hits_.size())) {
          if (hits_[static_cast<size_t>(row)].folder_def_idx >= 0) return;

          const std::string stem = eh_app_drawer_desktop_stem_from_path(hits_[static_cast<size_t>(row)].path);
          ctx_open_ = true;
          ctx_anchor_row_ = row;
          ctx_menu_x_ = ptr_x_;
          ctx_menu_y_ = ptr_y_;
          ctx_hover_item_ = -1;
          ctx_pinned_dock_ = dock_is_app_pinned(dock_, stem);
          ctx_pinned_start_ = dock_is_start_menu_pinned(dock_, stem);
          ctx_pinned_drawer_ = dock_is_drawer_pinned(dock_, stem);
          repaint_all();
          if (wl_) wl_display_flush(wl_->display());
        }
      }
      return;
    }

    // Left click while context menu is open: handle menu selection
    if (ctx_open_) {
      double mx = 0, my = 0, mw = 0, mh = 0;
      constexpr int n = 3;
      eh::shell::dock::app_drawer::app_drawer_row_context_menu_layout(
          ctx_menu_x_, ctx_menu_y_, static_cast<double>(w_), static_cast<double>(h_), n, &mx, &my, &mw, &mh);
      const int pick = eh::shell::dock::app_drawer::app_drawer_row_context_menu_pick(ptr_x_, ptr_y_, mx, my, n);
      if (pick >= 0 && ctx_anchor_row_ >= 0 && ctx_anchor_row_ < static_cast<int>(hits_.size())) {
        const std::string stem = eh_app_drawer_desktop_stem_from_path(hits_[static_cast<size_t>(ctx_anchor_row_)].path);
        if (pick == 0) {
          dock_pinned_toggle_silent(dock_, stem);
          dock_.deferDockRedraw = true;
        } else if (pick == 1) {
          dock_start_menu_pinned_toggle(dock_, stem);
        } else {
          dock_drawer_pinned_toggle(dock_, stem);
        }
      }
      ctx_open_ = false;
      ctx_anchor_row_ = -1;
      repaint_all();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }

    // PRESS.
    LaunchpadPaintModel m = paint_model();

    // Folder open: handle on press (tap behavior inside folder)
    if (folder_open_) {
      // If editing and clicking outside title, commit rename
      if (folder_editing_ && !eh_launchpad_pick_folder_title(m, ptr_x_, ptr_y_)) {
        commit_folder_rename();
        schedule_page_frame();
        return;
      }
      // Click on title area toggles edit mode
      if (eh_launchpad_pick_folder_title(m, ptr_x_, ptr_y_)) {
        if (!folder_editing_) {
          folder_editing_ = true;
          folder_edit_buffer_ = folders_[
              static_cast<size_t>(hits_[static_cast<size_t>(folder_open_idx_)].folder_def_idx)].name;
          schedule_page_frame();
        }
        return;
      }
      // Check child: set drag candidate instead of launching immediately
      const int child = eh_launchpad_pick_folder_child(m, ptr_x_, ptr_y_);
      if (child >= 0 && child < static_cast<int>(folder_children_.size())) {
        folder_drag_candidate_ = true;
        folder_drag_active_ = false;
        folder_drag_child_idx_ = child;
        folder_drag_start_x_ = ptr_x_;
        folder_drag_start_y_ = ptr_y_;
        return;
      }
      // Click outside the folder card closes the folder overlay
      if (!eh_launchpad_pick_folder_card(m, ptr_x_, ptr_y_)) {
        close_folder();
        return;
      }
      // Inside the card but not on title or children: ignore
      return;
    }

    // Backdrop click: close launchpad
    if (backdrop_surface_ && pointer_focus_) {
      wl_surface* focus = surface_;
      if (focus == backdrop_surface_) {
        close();
        if (wl_) wl_display_flush(wl_->display());
        return;
      }
    }

    // Organize button press
    {
      const AppDrawerHitZone z = eh_launchpad_hit_zone(m, surface_, surface_, ptr_x_, ptr_y_);
      if (z == AppDrawerHitZone::OrganizeButton) {
        organize_folders();
        return;
      }
    }

    // Main grid press: set drag candidate (or open folder immediately)
    const AppDrawerHitZone zone = eh_launchpad_hit_zone(m, surface_, surface_, ptr_x_, ptr_y_);
    if (zone == AppDrawerHitZone::SearchField) return;
    if (page_anim_.has_active() && zone == AppDrawerHitZone::AppListRow) return;
    if (zone == AppDrawerHitZone::AppListRow) {
      const int row = eh_launchpad_pick_catalog_index(m, surface_, surface_, ptr_x_, ptr_y_);
      if (row >= 0 && row < static_cast<int>(hits_.size())) {
        // Folder entries open immediately on press
        if (hits_[static_cast<size_t>(row)].folder_def_idx >= 0) {
          open_folder(row);
          return;
        }
        // Regular app: set drag candidate, don't launch yet
        drag_candidate_ = true;
        drag_src_idx_ = row;
        drag_active_ = false;
        drag_start_x_ = ptr_x_;
        drag_start_y_ = ptr_y_;
        return;
      }
    }
    return;
  }

  // RELEASE.
  // Handle folder child drag first (drag out of folder)
  if (folder_drag_active_) {
    const LaunchpadPaintModel m = paint_model();
    // If released outside the folder card, remove from folder
    if (!eh_launchpad_pick_folder_card(m, ptr_x_, ptr_y_)) {
      if (folder_open_idx_ >= 0 && folder_open_idx_ < static_cast<int>(hits_.size())) {
        const int fdi = hits_[static_cast<size_t>(folder_open_idx_)].folder_def_idx;
        if (fdi >= 0) {
          remove_app_from_folder(fdi, folder_drag_child_idx_);
          // If folder is now empty, close overlay
          if (folders_[static_cast<size_t>(fdi)].appIds.empty()) {
            close_folder();
          } else {
            // Rebuild folder children
            open_folder(folder_open_idx_);
          }
        }
      }
    }
    folder_drag_active_ = false;
    folder_drag_candidate_ = false;
    folder_drag_child_idx_ = -1;
    repaint_all();
    if (wl_) wl_display_flush(wl_->display());
    return;
  }
  if (folder_drag_candidate_) {
    // Click on folder child (no drag) — launch the app
    folder_drag_candidate_ = false;
    const int ch = folder_drag_child_idx_;
    folder_drag_child_idx_ = -1;
    if (ch >= 0 && ch < static_cast<int>(folder_children_.size())) {
      launch_exec_command(folder_children_[static_cast<size_t>(ch)].exec);
      dock_start_launch_bounce(dock_, folder_children_[static_cast<size_t>(ch)].path, true);
      close();
      if (wl_) wl_display_flush(wl_->display());
    }
    return;
  }

  if (!drag_candidate_ && !drag_active_) return;

  if (drag_active_) {
    // Drag release on main grid: check for folder target or create new folder
    const LaunchpadPaintModel m = paint_model();
    const AppDrawerHitZone zone = eh_launchpad_hit_zone(m, surface_, surface_, ptr_x_, ptr_y_);
    const int dst = (zone == AppDrawerHitZone::AppListRow)
        ? eh_launchpad_pick_catalog_index(m, surface_, surface_, ptr_x_, ptr_y_) : -1;
    if (dst >= 0 && dst < static_cast<int>(hits_.size())) {
      const auto& dstHit = hits_[static_cast<size_t>(dst)];
      if (dstHit.folder_def_idx >= 0) {
        // Dropping onto an existing folder: add the dragged app to it
        const auto& src = hits_[static_cast<size_t>(drag_src_idx_)];
        if (src.folder_def_idx < 0) {
          const std::string stem = eh_app_drawer_desktop_stem_from_path(src.path);
          const std::string appId = eh::shell::paths::normalize_desktop_app_id(stem);
          if (!appId.empty()) {
            add_app_to_folder(dstHit.folder_def_idx, appId);
          }
        }
      } else {
        // Dropping onto a regular app: create new folder
        create_folder_from_drag(drag_src_idx_, dst);
      }
    }
    // Clear drag state
    drag_active_ = false;
    drag_candidate_ = false;
    drag_src_idx_ = -1;
    repaint_all();
    if (wl_) wl_display_flush(wl_->display());
    return;
  }

  // Click (no drag) — launch the app we pressed on
  drag_candidate_ = false;
  const int src_idx = drag_src_idx_;
  drag_src_idx_ = -1;
  if (src_idx >= 0 && src_idx < static_cast<int>(hits_.size())) {
    // Verify we're still over the same item
    const LaunchpadPaintModel m = paint_model();
    const AppDrawerHitZone zone = eh_launchpad_hit_zone(m, surface_, surface_, ptr_x_, ptr_y_);
    const int row = (zone == AppDrawerHitZone::AppListRow)
        ? eh_launchpad_pick_catalog_index(m, surface_, surface_, ptr_x_, ptr_y_) : -1;
    if (row == src_idx) {
      sel_ = src_idx;
      hover_idx_ = -1;
      repaint_all();
      if (wl_) wl_display_flush(wl_->display());
      launch_exec_command(hits_[static_cast<size_t>(src_idx)].exec);
      dock_start_launch_bounce(dock_, hits_[static_cast<size_t>(src_idx)].path, true);
      close();
      if (wl_) wl_display_flush(wl_->display());
    }
  }
}

void Host::handle_pointer_axis(double delta) {
  BenchScope bench("ptr_axis", 4);
  if (!open_) return;
  if (page_anim_.has_active()) return;

  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  constexpr double kScrollThreshold = 25.0;
  scrollAccum_ += -delta;

  int step = 0;
  if (scrollAccum_ < -kScrollThreshold * us) { step = 1; scrollAccum_ += kScrollThreshold * us; }
  else if (scrollAccum_ > kScrollThreshold * us) { step = -1; scrollAccum_ -= kScrollThreshold * us; }
  if (step == 0) return;

  // If folder is open, scroll folder pages instead of main grid pages
  if (folder_open_) {
    const LaunchpadPaintModel m = paint_model();
    (void)m;
    const int nPerPage = 9;
    const int n = static_cast<int>(folder_children_.size());
    const int pageCount = std::max(1, (n + nPerPage - 1) / nPerPage);
    const int oldFp = folder_page_;
    folder_page_ = std::clamp(folder_page_ + step, 0, pageCount - 1);
    if (folder_page_ != oldFp) {
      scrollAccum_ = 0;
      schedule_page_frame();
    }
    return;
  }

  const int oldp = page_;
  LaunchpadPaintModel m = paint_model();
  const double W = static_cast<double>(m.popup_w);
  const double H = static_cast<double>(m.popup_h);
  const LaunchpadLayout lay = compute_launchpad_layout(m, W, H);
  m.page = std::clamp(page_ + step, 0, lay.pageCount - 1);
  if (m.page == oldp) { scrollAccum_ = 0; return; }
  page_ = m.page;
  begin_page_slide(oldp, m.page);
}

void Host::handle_key(uint32_t key, uint32_t state) {
   
  if (!open_ || !xkbState_) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState_, key + 8);

  // Dismiss context menu on Escape
  if (sym == XKB_KEY_Escape && ctx_open_) {
    ctx_open_ = false;
    ctx_anchor_row_ = -1;
    schedule_page_frame();
    if (wl_) wl_display_flush(wl_->display());
    return;
  }

  // Folder editing mode
  if (folder_editing_) {
    if (sym == XKB_KEY_Escape) {
      folder_editing_ = false;
      folder_edit_buffer_.clear();
      schedule_page_frame();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      commit_folder_rename();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }
    if (sym == XKB_KEY_BackSpace) {
      eh::shell::str::utf8_pop_back(folder_edit_buffer_);
      schedule_page_frame();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }
    {
      char utf8[64]{};
      const int n8 = xkb_state_key_get_utf8(xkbState_, key + 8, utf8, static_cast<int>(sizeof(utf8)) - 1);
      if (n8 > 0) {
        bool printable = true;
        for (int i = 0; i < n8; ++i) {
          if (static_cast<unsigned char>(utf8[i]) < 0x20u) { printable = false; break; }
        }
        if (printable) {
          folder_edit_buffer_.append(utf8, static_cast<size_t>(n8));
          schedule_page_frame();
          if (wl_) wl_display_flush(wl_->display());
          return;
        }
      }
    }
    return;
  }

  if (sym == XKB_KEY_Escape) {
    if (folder_open_) {
      folder_open_ = false;
      folder_editing_ = false;
      folder_edit_buffer_.clear();
      folder_open_idx_ = -1;
      folder_children_.clear();
      folder_hover_child_ = -1;
      folder_title_hovered_ = false;
      schedule_page_frame();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }
    if (power_confirm_open_) {
      power_confirm_open_ = false;
      power_confirm_idx_  = -1;
      repaint_all();
      if (wl_) wl_display_flush(wl_->display());
      return;
    }
    close();
    if (wl_) wl_display_flush(wl_->display());
    return;
  }

  if (sym == XKB_KEY_BackSpace) {
    const auto t_kbd = std::chrono::steady_clock::now();
    eh::shell::str::utf8_pop_back(query_);
    refresh_catalog();
    repaint_all();
    if (wl_) wl_display_flush(wl_->display());
    {
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_kbd).count();
      std::cerr << "[search-bench] lp_e2e_backspace query=\"" << query_ << "\" " << us << "us" << std::endl;
    }
    return;
  }

  {
    char utf8[64]{};
    const int n8 = xkb_state_key_get_utf8(xkbState_, key + 8, utf8, static_cast<int>(sizeof(utf8)) - 1);
    if (n8 > 0) {
      bool printable = true;
      for (int i = 0; i < n8; ++i) {
        if (static_cast<unsigned char>(utf8[i]) < 0x20u) { printable = false; break; }
      }
      if (printable) {
        const auto t_kbd = std::chrono::steady_clock::now();
        query_.append(utf8, static_cast<size_t>(n8));
        refresh_catalog();
        repaint_all();
        if (wl_) wl_display_flush(wl_->display());
        {
          const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_kbd).count();
          std::cerr << "[search-bench] lp_e2e_keypress query=\"" << query_ << "\" " << us << "us" << std::endl;
        }
        return;
      }
    }
  }

  // Folder page navigation
  if (folder_open_) {
    const int nPerPageF = 9;
    const int nF = static_cast<int>(folder_children_.size());
    const int pageCountF = std::max(1, (nF + nPerPageF - 1) / nPerPageF);
    if (sym == XKB_KEY_Left || sym == XKB_KEY_KP_Left) {
      if (folder_page_ > 0) {
        folder_page_--;
        schedule_page_frame();
        if (wl_) wl_display_flush(wl_->display());
      }
      return;
    }
    if (sym == XKB_KEY_Right || sym == XKB_KEY_KP_Right) {
      if (folder_page_ < pageCountF - 1) {
        folder_page_++;
        schedule_page_frame();
        if (wl_) wl_display_flush(wl_->display());
      }
      return;
    }
  }
}

// Folder drag / create / helpers.

void Host::create_folder_from_drag(int src_idx, int dst_idx) {
  if (src_idx < 0 || src_idx >= static_cast<int>(hits_.size()) ||
      dst_idx < 0 || dst_idx >= static_cast<int>(hits_.size()) ||
      src_idx == dst_idx) return;
  const auto& src = hits_[static_cast<size_t>(src_idx)];
  const auto& dst = hits_[static_cast<size_t>(dst_idx)];
  if (src.folder_def_idx >= 0 || dst.folder_def_idx >= 0) return;

  auto to_app_id = [&](const LaunchpadHit& h) -> std::string {
    const std::string stem = eh_app_drawer_desktop_stem_from_path(h.path);
    return eh::shell::paths::normalize_desktop_app_id(stem);
  };
  const std::string srcId = to_app_id(src);
  const std::string dstId = to_app_id(dst);
  if (srcId.empty() || dstId.empty()) return;

  std::string folder_name = "Apps";
  const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
  for (const auto& entry : catalog) {
    if (entry.path == src.path) {
      if (!entry.categories.empty()) {
        struct { const char* xdg; const char* label; int priority; } known[] = {
          {"Game", "Games", 3}, {"AudioVideo", "Media", 2}, {"Audio", "Media", 2},
          {"Video", "Media", 2}, {"Development", "Development", 2},
          {"Education", "Education", 2}, {"Science", "Science", 2},
          {"Graphics", "Graphics", 2}, {"Office", "Office", 2},
          {"Network", "Network", 1}, {"Settings", "Settings", 1},
          {"System", "System", 1}, {"Utility", "Utilities", 1},
        };
        int bestPri = -1;
        const char* bestLabel = nullptr;
        size_t pos = 0;
        while (pos < entry.categories.size()) {
          size_t next = entry.categories.find(';', pos);
          std::string_view cat(next == std::string::npos
              ? entry.categories.data() + pos
              : entry.categories.data() + pos,
              next == std::string::npos ? entry.categories.size() - pos : next - pos);
          for (auto& k : known) {
            if (cat == k.xdg && k.priority > bestPri) {
              bestPri = k.priority;
              bestLabel = k.label;
            }
          }
          if (next == std::string::npos) break;
          pos = next + 1;
        }
        if (bestLabel) folder_name = bestLabel;
        if (folder_name == "Apps") {
          size_t first = entry.categories.find(';');
          folder_name = (first == std::string::npos) ? entry.categories : entry.categories.substr(0, first);
        }
      }
      break;
    }
  }

  LaunchpadFolderDef fd;
  fd.name = folder_name;
  fd.appIds.push_back(srcId);
  fd.appIds.push_back(dstId);
  folders_.insert(folders_.begin(), std::move(fd));
  save_launchpad_settings();
  refresh_catalog();
  schedule_page_frame();
}

void Host::add_app_to_folder(int folder_def_idx, const std::string& appId) {
  if (folder_def_idx < 0 || folder_def_idx >= static_cast<int>(folders_.size())) return;
  if (appId.empty()) return;
  auto& folder = folders_[static_cast<size_t>(folder_def_idx)];
  for (const auto& existing : folder.appIds) {
    if (existing == appId) return;
  }
  folder.appIds.push_back(appId);
  save_launchpad_settings();
  refresh_catalog();
  schedule_page_frame();
}

void Host::remove_app_from_folder(int folder_def_idx, int child_hit_idx) {
  if (folder_def_idx < 0 || folder_def_idx >= static_cast<int>(folders_.size())) return;
  if (child_hit_idx < 0 || child_hit_idx >= static_cast<int>(folder_children_.size())) return;
  auto& folder = folders_[static_cast<size_t>(folder_def_idx)];
  const auto& child = folder_children_[static_cast<size_t>(child_hit_idx)];
  const std::string stem = eh_app_drawer_desktop_stem_from_path(child.path);
  const std::string appId = eh::shell::paths::normalize_desktop_app_id(stem);
  for (auto it = folder.appIds.begin(); it != folder.appIds.end(); ++it) {
    if (*it == appId) {
      folder.appIds.erase(it);
      break;
    }
  }
  save_launchpad_settings();
  remove_empty_folders();
}

void Host::remove_empty_folders() {
  bool changed = false;
  for (auto it = folders_.begin(); it != folders_.end(); ) {
    if (it->appIds.empty()) {
      it = folders_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (changed) {
    save_launchpad_settings();
    refresh_catalog();
    schedule_page_frame();
  }
}

int Host::open_appdir_inotify() {
  if (appdir_inotify_fd_ >= 0) return appdir_inotify_fd_;
  appdir_inotify_fd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (appdir_inotify_fd_ < 0) return -1;
  // Watch common XDG app directories
  auto watch = [&](const char* dir) {
    (void)inotify_add_watch(appdir_inotify_fd_, dir,
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
  };
  watch("/usr/share/applications");
  watch("/usr/local/share/applications");
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    std::string p = std::string(xdg) + "/applications";
    watch(p.c_str());
  } else if (const char* home = std::getenv("HOME")) {
    std::string p = std::string(home) + "/.local/share/applications";
    watch(p.c_str());
  }
  if (const char* xdgDirs = std::getenv("XDG_DATA_DIRS")) {
    std::string s(xdgDirs);
    size_t pos = 0;
    while (pos < s.size()) {
      size_t colon = s.find(':', pos);
      std::string dir = s.substr(pos, colon == std::string::npos ? s.size() - pos : colon - pos);
      if (!dir.empty()) {
        if (dir.back() != '/') dir += '/';
        dir += "applications";
        watch(dir.c_str());
      }
      if (colon == std::string::npos) break;
      pos = colon + 1;
    }
  }
  return appdir_inotify_fd_;
}

void Host::drain_appdir_inotify() {
  if (appdir_inotify_fd_ < 0) return;
  // Non-blocking poll to check if there's data
  struct pollfd pfd;
  pfd.fd = appdir_inotify_fd_;
  pfd.events = POLLIN;
  if (poll(&pfd, 1, 0) <= 0) return;
  // Drain events
  char buf[4096];
  bool changed = false;
  while (true) {
    int n = static_cast<int>(read(appdir_inotify_fd_, buf, sizeof(buf)));
    if (n <= 0) break;
    changed = true;
  }
  if (changed) {
    eh::shell::dock::app_drawer::invalidate_desktop_entries_cache();
    refresh_catalog();
  }
}

void Host::organize_folders() {
  static const struct { const char* xdg; const char* label; int pri; } catMap[] = {
    {"Game", "Games", 3},
    {"AudioVideo", "Media", 2}, {"Audio", "Media", 2}, {"Video", "Media", 2},
    {"Development", "Development", 2}, {"Education", "Education", 2},
    {"Science", "Science", 2}, {"Graphics", "Graphics", 2},
    {"Office", "Office", 2}, {"Network", "Network", 2},
    {"Otter", "Otter", 3},
    {"Settings", "Settings", 1}, {"System", "System", 1},
    {"Utility", "Utilities", 1},
  };

  auto contains_otter = [](std::string_view text) -> bool {
    for (size_t i = 0; i + 4 < text.size(); ++i) {
      char c0 = (text[i] >= 'A' && text[i] <= 'Z') ? static_cast<char>(text[i] + 32) : text[i];
      if (c0 != 'o') continue;
      char c1 = (text[i+1] >= 'A' && text[i+1] <= 'Z') ? static_cast<char>(text[i+1] + 32) : text[i+1];
      if (c1 != 't') continue;
      char c2 = (text[i+2] >= 'A' && text[i+2] <= 'Z') ? static_cast<char>(text[i+2] + 32) : text[i+2];
      if (c2 != 't') continue;
      char c3 = (text[i+3] >= 'A' && text[i+3] <= 'Z') ? static_cast<char>(text[i+3] + 32) : text[i+3];
      if (c3 != 'e') continue;
      char c4 = (text[i+4] >= 'A' && text[i+4] <= 'Z') ? static_cast<char>(text[i+4] + 32) : text[i+4];
      if (c4 != 'r') continue;
      return true;
    }
    return false;
  };

  std::unordered_map<std::string, std::vector<std::string>> groups;
  std::vector<std::string> uncategorized;

  const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
  for (const auto& entry : catalog) {
    if (entry.noDisplay || entry.hidden) continue;
    const std::string stem = eh_app_drawer_desktop_stem_from_path(entry.path);
    const std::string appId = eh::shell::paths::normalize_desktop_app_id(stem);
    if (appId.empty()) continue;
    int bestPri = -1;
    const char* bestLabel = nullptr;
    size_t pos = 0;
    while (pos < entry.categories.size()) {
      size_t next = entry.categories.find(';', pos);
      std::string_view cat(next == std::string::npos
          ? entry.categories.data() + pos : entry.categories.data() + pos,
          next == std::string::npos ? entry.categories.size() - pos : next - pos);
      for (auto& k : catMap) {
        if (cat == k.xdg && k.pri > bestPri) {
          bestPri = k.pri;
          bestLabel = k.label;
        }
      }
      if (next == std::string::npos) break;
      pos = next + 1;
    }
    if (bestLabel && bestPri >= 0) {
      groups[bestLabel].push_back(appId);
    } else if (contains_otter(entry.name) || contains_otter(entry.genericName) || contains_otter(entry.keywords)) {
      groups["Otter"].push_back(appId);
    } else {
      uncategorized.push_back(appId);
    }
  }

  folders_.clear();
  // Group labels by the order they appear in catMap
  std::unordered_set<std::string> used;
  for (auto& k : catMap) {
    auto it = groups.find(k.label);
    if (it != groups.end() && !it->second.empty()) {
      LaunchpadFolderDef fd;
      fd.name = it->first;
      fd.appIds = std::move(it->second);
      folders_.push_back(std::move(fd));
      used.insert(it->first);
    }
  }
  // Any remaining custom groups not in catMap
  for (auto& [label, ids] : groups) {
    if (used.count(label) || ids.empty()) continue;
    LaunchpadFolderDef fd;
    fd.name = label;
    fd.appIds = std::move(ids);
    folders_.push_back(std::move(fd));
  }
  // Uncategorized
  if (!uncategorized.empty()) {
    LaunchpadFolderDef fd;
    fd.name = "Other";
    fd.appIds = std::move(uncategorized);
    folders_.push_back(std::move(fd));
  }

  close_folder();
  save_launchpad_settings();
  refresh_catalog();
  schedule_page_frame();
}

void Host::open_folder(int hit_idx) {
  if (hit_idx < 0 || hit_idx >= static_cast<int>(hits_.size())) return;
  const auto& hit = hits_[static_cast<size_t>(hit_idx)];
  const int fdi = hit.folder_def_idx;
  if (fdi < 0 || fdi >= static_cast<int>(folders_.size())) return;
  const auto& folder = folders_[static_cast<size_t>(fdi)];

  folder_open_idx_ = hit_idx;
  folder_children_.clear();
  folder_editing_ = false;
  folder_page_ = 0;
  folder_edit_buffer_.clear();
  folder_hover_child_ = -1;
  folder_title_hovered_ = false;
  folder_drag_active_ = false;
  folder_drag_candidate_ = false;
  folder_drag_child_idx_ = -1;

  const auto& catalog = eh::shell::dock::app_drawer::get_cached_entries();
  for (const auto& appId : folder.appIds) {
    if (auto resolved = find_desktop_file_for_appid(appId)) {
      for (const auto& entry : catalog) {
        if (entry.path == *resolved) {
          LaunchpadHit ch;
          ch.path  = entry.path;
          ch.name  = entry.name;
          ch.exec  = entry.exec;
          ch.iconKey = entry.icon.empty() ? std::string("application-x-executable") : entry.icon;
          folder_children_.push_back(std::move(ch));
          break;
        }
      }
    }
  }

  folder_open_ = true;
  repaint_all();
  schedule_page_frame();
}

void Host::close_folder() {
  if (!folder_open_) return;
  folder_open_ = false;
  folder_editing_ = false;
  folder_edit_buffer_.clear();
  folder_open_idx_ = -1;
  folder_children_.clear();
  folder_hover_child_ = -1;
  folder_page_ = 0;
  folder_title_hovered_ = false;
  folder_drag_active_ = false;
  folder_drag_candidate_ = false;
  folder_drag_child_idx_ = -1;
  repaint_all();
}

void Host::commit_folder_rename() {
  if (!folder_editing_ || folder_open_idx_ < 0 ||
      folder_open_idx_ >= static_cast<int>(hits_.size())) return;
  const int fdi = hits_[static_cast<size_t>(folder_open_idx_)].folder_def_idx;
  if (fdi < 0 || fdi >= static_cast<int>(folders_.size())) return;
  folders_[static_cast<size_t>(fdi)].name = folder_edit_buffer_;
  save_launchpad_settings();
  folder_editing_ = false;
  folder_edit_buffer_.clear();
  // Update the hit entry name too
  hits_[static_cast<size_t>(folder_open_idx_)].name = folders_[static_cast<size_t>(fdi)].name;
  schedule_page_frame();
}

// Launchpad TOML state.

std::string Host::launchpad_state_path() {
  return eh::config::state_event_horizon_dir() + "/launchpad.toml";
}

void Host::load_launchpad_settings() {
  folders_.clear();
  const std::string path = launchpad_state_path();
  toml::table tbl;
  try {
    tbl = toml::parse_file(path);
  } catch (const toml::parse_error&) {
    return;
  }
  if (const auto* folders = tbl["folder"].as_array()) {
    for (const auto& el : *folders) {
      if (const auto* ft = el.as_table()) {
        LaunchpadFolderDef fd;
        if (auto n = (*ft)["name"].value<std::string>()) fd.name = *n;
        if (const auto* ids = (*ft)["app_ids"].as_array()) {
          for (const auto& id : *ids)
            if (const auto* s = id.as_string()) fd.appIds.push_back(std::string(s->get()));
        }
        if (!fd.appIds.empty()) folders_.push_back(std::move(fd));
      }
    }
  }
  // Appearance is owned by the settings app via load_launchpad_appearance_from_file
  // → settings_to_shell_config → shell_config_apply_from_memory.  Do NOT load it here
  // or we risk overwriting the live g_cache with a stale TOML value.
}

void Host::save_launchpad_settings() const {
  toml::array folders_arr;
  for (const auto& f : folders_) {
    toml::table ft;
    ft.insert_or_assign("name", f.name);
    toml::array ids;
    for (const auto& id : f.appIds) ids.push_back(id);
    ft.insert_or_assign("app_ids", std::move(ids));
    folders_arr.push_back(std::move(ft));
  }
  // Save appearance settings from the current global config
  const auto& appearance = eh::config::shell_config_snapshot().appearance;
  toml::table ap;
  ap.insert_or_assign("launchpad_grid_columns", static_cast<int64_t>(appearance.launchpadGridColumns));
  ap.insert_or_assign("launchpad_grid_rows", static_cast<int64_t>(appearance.launchpadGridRows));
  ap.insert_or_assign("launchpad_cell_gap_px", static_cast<int64_t>(appearance.launchpadCellGapPx));
  ap.insert_or_assign("launchpad_icon_fill_pct", static_cast<int64_t>(appearance.launchpadIconFillPct));
  ap.insert_or_assign("launchpad_layout_scale_pct", static_cast<int64_t>(appearance.launchpadLayoutScalePct));
  ap.insert_or_assign("launchpad_dpi_scale_pct", static_cast<int64_t>(appearance.launchpadDpiScalePct));
  ap.insert_or_assign("launchpad_view_mode", static_cast<int64_t>(appearance.launchpadViewMode));
  ap.insert_or_assign("launchpad_folder_size_pct", static_cast<int64_t>(appearance.launchpadFolderSizePct));
  ap.insert_or_assign("launchpad_folder_gap_px", static_cast<int64_t>(appearance.launchpadFolderGapPx));
  ap.insert_or_assign("overlay_opacity_launchpad", static_cast<double>(appearance.overlayOpacityLaunchpad));

  toml::table root;
  root.insert_or_assign("folder", std::move(folders_arr));
  root.insert_or_assign("appearance", std::move(ap));
  std::error_code ec;
  std::filesystem::create_directories(
      std::filesystem::path(launchpad_state_path()).parent_path(), ec);
  std::ofstream ofs(launchpad_state_path());
  if (ofs) ofs << root << "\n";
}

// Lifecycle.

void Host::close_before_dock_popup(DockApp& dock) {
  if (dock.launchpad) dock.launchpad->close();
}

Host::Host(DockApp& dock, std::unique_ptr<eh::wayland::WaylandConnection> wl)
    : wl_(std::move(wl)), dock_(dock) {
  MANGOWM_DEBUG("Launchpad Host ctor this=%p", (void*)this);
  xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  load_launchpad_settings();

  // Seed the last seen grid size from current config so the first paint
  // doesn't falsely think it changed.
  {
    const auto& ap0 = eh::config::shell_config_snapshot().appearance;
    last_grid_cols_ = ap0.launchpadGridColumns;
    last_grid_rows_ = ap0.launchpadGridRows;
  }
}

Host::~Host() {
  MANGOWM_DEBUG("Launchpad Host dtor this=%p", (void*)this);
  close();
  destroy_seat();
  if (xkbState_)  { xkb_state_unref(xkbState_);  xkbState_  = nullptr; }
  if (xkbKeymap_) { xkb_keymap_unref(xkbKeymap_); xkbKeymap_ = nullptr; }
  if (xkbCtx_)   { xkb_context_unref(xkbCtx_);   xkbCtx_    = nullptr; }
}

void Host::destroy_seat() {
   
  if (keyboard_) { wl_keyboard_release(keyboard_); keyboard_ = nullptr; }
  if (pointer_)  { wl_pointer_release(pointer_);   pointer_  = nullptr; }
  pointer_focus_ = false;
}

// Paint model.

LaunchpadPaintModel Host::paint_model() {
  LaunchpadPaintModel m;
  m.popup_w            = w_;
  m.popup_h            = h_;
  m.page               = page_;
  m.sel                = sel_;
  m.hover_idx          = hover_idx_;
  m.hits               = &hits_;
  m.launchpad_folders  = &folders_;
  m.search_query       = query_;
  m.power_confirm_open = power_confirm_open_;
  m.power_confirm_idx  = power_confirm_idx_;
  m.pointer_x          = ptr_x_;
  m.pointer_y          = ptr_y_;
  m.page_slide_t       = page_slide_t_;
  m.page_slide_from    = page_slide_from_;
  m.page_slide_to      = page_slide_to_;
  m.folder_open        = folder_open_;
  m.folder_open_idx    = folder_open_idx_;
  m.folder_hover_child = folder_hover_child_;
  m.folder_page        = folder_page_;
  m.folder_children    = &folder_children_;
  m.folder_editing     = folder_editing_;
  m.folder_edit_buffer = folder_edit_buffer_;
  m.folder_title_hovered = folder_title_hovered_;
  m.organize_hovered     = organize_hovered_;
  m.drag_active          = drag_active_;
  m.drag_src_idx         = drag_src_idx_;
  m.folder_drag_active   = folder_drag_active_;
  m.folder_drag_child_idx = folder_drag_child_idx_;
  m.hover_anim_scale     = hover_anim_scale_;
  cached_layout_ = compute_launchpad_layout(m, static_cast<double>(w_), static_cast<double>(h_));
  m.layout             = &cached_layout_;
  return m;
}

// Animation.

void Host::schedule_page_frame() {
  if (!open_ || !surface_ || page_frame_cb_) return;
  page_frame_cb_ = wl_surface_frame(surface_);
  wl_callback_add_listener(page_frame_cb_, &kLaunchpadPageFrameListener, this);
  wl_surface_commit(surface_);
}

void Host::begin_page_slide(int from_page, int to_page) {
  BenchScope bench("begin_page_slide", 3);
  if (from_page == to_page) return;
  page_anim_.cancel_all();
  if (page_frame_cb_) { wl_callback_destroy(page_frame_cb_); page_frame_cb_ = nullptr; }
  page_slide_from_ = from_page;
  page_slide_to_   = to_page;
  if (eh::shell::animations_reduced_motion()) {
    page_slide_t_ = -1.f;
    repaint_all();
    if (wl_) wl_display_flush(wl_->display());
    return;
  }

  // Pre-render both pages to cached surfaces for smooth animation.
  {
    const auto& sc = eh::config::shell_config_snapshot();
    const float alpha = static_cast<float>(
        eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Launchpad));
    const auto mc = eh::config::derived_chrome_colors(sc.appearance);
    LaunchpadPaintModel pm = paint_model();
    LaunchpadLayout L;
    if (pm.layout) L = *pm.layout;
    else L = compute_launchpad_layout(pm, static_cast<double>(w_), static_cast<double>(h_));

    cached_page_from_.ensure(w_, h_);
    {
      cairo_t* cc = cairo_create(cached_page_from_.cairo_surface());
      cairo_set_operator(cc, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cc);
      cairo_set_operator(cc, CAIRO_OPERATOR_OVER);
      eh_launchpad_render_page(cc, dock_, pm, L, from_page, alpha, mc);
      cairo_destroy(cc);
    }
    cached_page_to_.ensure(w_, h_);
    {
      cairo_t* cc = cairo_create(cached_page_to_.cairo_surface());
      cairo_set_operator(cc, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cc);
      cairo_set_operator(cc, CAIRO_OPERATOR_OVER);
      eh_launchpad_render_page(cc, dock_, pm, L, to_page, alpha, mc);
      cairo_destroy(cc);
    }
    cached_from_page_ = from_page;
    cached_to_page_ = to_page;
    bench.checkpoint("cache_render");
  }

  page_anim_.animate(
      0.f, 1.f, launchpad_page_slide_duration_ms(dock_), eh::shell::Easing::EaseOutCubic,
      [this](float v) { page_slide_t_ = v; repaint_all(); },
      [this]()        { page_slide_t_ = -1.f; repaint_all(); });
  schedule_page_frame();
}

// Catalog.

void Host::refresh_catalog() {
  BenchScope bench("refresh_catalog", 3);
  page_anim_.cancel_all();
  page_slide_t_ = -1.f;
  if (page_frame_cb_) { wl_callback_destroy(page_frame_cb_); page_frame_cb_ = nullptr; }
  cached_from_page_ = -1;
  cached_to_page_ = -1;
  cached_content_key_ = 0;
  hover_idx_ = -1;

  // Also pick up any launcher grid size change here (in case settings were
  // changed and we are about to rebuild hits + page for the folders).
  {
    const auto& ap = eh::config::shell_config_snapshot().appearance;
    if (last_grid_cols_ < 0) {
      last_grid_cols_ = ap.launchpadGridColumns;
      last_grid_rows_ = ap.launchpadGridRows;
    } else if (last_grid_cols_ != ap.launchpadGridColumns ||
               last_grid_rows_ != ap.launchpadGridRows) {
      last_grid_cols_ = ap.launchpadGridColumns;
      last_grid_rows_ = ap.launchpadGridRows;
    }
  }

  // Prefix optimisation: if the new query extends the previous one we can
  // re-score only the entries that matched before instead of scanning the
  // full catalog.
  const bool canFilter = !prev_query_.empty() && query_.size() > prev_query_.size() &&
      query_.compare(0, prev_query_.size(), prev_query_) == 0 &&
      !prev_cat_indices_.empty();
  if (canFilter) {
    eh::shell::launchpad::launchpad_search_filter(query_, prev_cat_indices_,
                                                   &dock_.settings.startMenuPinnedApps, &hits_);
  } else {
    eh::shell::launchpad::launchpad_search_run(query_, &dock_.settings.startMenuPinnedApps, &hits_);
    // Cache catalog indices for future prefix searches
    const auto& catalog = eh::app_drawer::get_cached_entries();
    std::unordered_map<std::string_view, size_t> pathToIdx;
    pathToIdx.reserve(catalog.size());
    for (size_t i = 0; i < catalog.size(); ++i)
      pathToIdx.emplace(catalog[i].path, i);
    prev_cat_indices_.clear();
    prev_cat_indices_.reserve(hits_.size());
    for (const auto& hit : hits_) {
      auto it = pathToIdx.find(hit.path);
      if (it != pathToIdx.end())
        prev_cat_indices_.push_back(it->second);
    }
  }
  prev_query_ = query_;

  // Remove apps that are already inside a folder from the main grid (only when browsing, not searching)
  if (!folders_.empty() && query_.empty()) {
    std::unordered_set<std::string> folderAppIds;
    for (auto& f : folders_)
      for (auto& id : f.appIds)
        folderAppIds.insert(id);
    for (auto it = hits_.begin(); it != hits_.end(); ) {
      if (it->folder_def_idx >= 0) { ++it; continue; }
      const std::string stem = eh_app_drawer_desktop_stem_from_path(it->path);
      const std::string id = eh::shell::paths::normalize_desktop_app_id(stem);
      if (!id.empty() && folderAppIds.count(id))
        it = hits_.erase(it);
      else
        ++it;
    }
  }
  // Inject folder entries first (so folders appear at the start of the grid / page 0 for visibility).
  // (skip empty — they get cleaned elsewhere)
  std::vector<LaunchpadHit> folder_hits;
  for (int fi = 0; fi < static_cast<int>(folders_.size()); ++fi) {
    LaunchpadHit fh;
    fh.name = folders_[static_cast<size_t>(fi)].name;
    fh.folderAppIds = folders_[static_cast<size_t>(fi)].appIds;
    fh.folder_def_idx = fi;
    folder_hits.push_back(std::move(fh));
  }
  // Prepend folders, then the (filtered) loose apps
  auto loose_hits = std::move(hits_);
  hits_.clear();
  for (auto& fh : folder_hits) {
    hits_.push_back(std::move(fh));
  }
  for (auto& h : loose_hits) {
    hits_.push_back(std::move(h));
  }
  bench.checkpoint("search");
  page_ = 0;
  if (hits_.empty()) { sel_ = -1; return; }
  if (sel_ >= static_cast<int>(hits_.size())) sel_ = static_cast<int>(hits_.size()) - 1;
  LaunchpadPaintModel m = paint_model();
  eh_launchpad_clamp_page(&m);
  page_ = m.page;

  if (open_) {
    // Ensure a paint happens with the (possibly newly detected) grid size so
    // the prepended folder cards lay out with the correct cols/rows wrapping.
    schedule_page_frame();
  }
}

// Painting.

// Key for base-grid content (excludes hover/focus/drag overlays).
uint64_t Host::base_content_key() const {
  constexpr auto mix = [](uint64_t& k, uint64_t v) {
    k ^= v * 0x9e3779b97f4a7c15ULL;
    k  = std::rotl(k, 37) + 0x3c6ef372fe94f82aULL;
  };
  uint64_t k = 0;
  mix(k, static_cast<uint64_t>(page_));
  mix(k, static_cast<uint64_t>(folder_open_));
  mix(k, static_cast<uint64_t>(folder_open_idx_));
  mix(k, static_cast<uint64_t>(folder_page_));
  mix(k, static_cast<uint64_t>(hits_.size()));
  mix(k, static_cast<uint64_t>(query_.size()));
  mix(k, static_cast<uint64_t>(w_));
  mix(k, static_cast<uint64_t>(h_));
  const auto& ap = eh::config::shell_config_snapshot().appearance;
  mix(k, static_cast<uint64_t>(ap.launchpadGridColumns));
  mix(k, static_cast<uint64_t>(ap.launchpadGridRows));
  mix(k, static_cast<uint64_t>(ap.launchpadCellGapPx));
  mix(k, static_cast<uint64_t>(ap.launchpadIconFillPct));
  mix(k, static_cast<uint64_t>(ap.launchpadLayoutScalePct));
  mix(k, static_cast<uint64_t>(ap.launchpadDpiScalePct));
  mix(k, static_cast<uint64_t>(ap.launchpadFolderSizePct));
  mix(k, static_cast<uint64_t>(ap.launchpadFolderGapPx));
  // Icons are baked into the cached page bitmap: re-render when the icon
  // theme changes so theme switches apply live.
  mix(k, dock_.icons.icon_theme_generation());
  return k;
}

void Host::paint_backdrop() {
  BenchScope bench("paint_backdrop", 3);
  if (!open_ || !backdrop_surface_ || w_ <= 0 || h_ <= 0) return;
  if (backdrop_buf_.busy()) return;
  if (!wl_ || !wl_->shm()) return;
  const int bufW = backdrop_viewport_ ? 1 : w_;
  const int bufH = backdrop_viewport_ ? 1 : h_;
  if (!backdrop_buf_.ensure(wl_->shm(), eh::shell::kLaunchpadBackdropNamespace, bufW, bufH)) return;

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const float alpha = static_cast<float>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Launchpad));

  cairo_t* cr = backdrop_buf_.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  const double dimA = std::clamp(static_cast<double>(alpha), 0.0, 1.0);
  const eh::config::ShellAppearance& ap = sc.appearance;
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(ap);
  double br = 0.04, bg = 0.05, bb = 0.08;
  if (ap.anyPaletteActive()) {
    constexpr double k = 0.42;
    br = mc.drawerDimR * k + 0.015 * (1.0 - k);
    bg = mc.drawerDimG * k + 0.02  * (1.0 - k);
    bb = mc.drawerDimB * k + 0.025 * (1.0 - k);
  }
  cairo_set_source_rgba(cr, br, bg, bb, dimA);
  cairo_paint(cr);
  cairo_restore(cr);

  cairo_surface_flush(backdrop_buf_.cairo_surface());
  if (backdrop_viewport_) wp_viewport_set_destination(backdrop_viewport_, w_, h_);
  wl_surface_attach(backdrop_surface_, backdrop_buf_.wl(), 0, 0);
  wl_surface_damage_buffer(backdrop_surface_, 0, 0, bufW, bufH);
  backdrop_buf_.mark_busy();
  wl_surface_commit(backdrop_surface_);
}

void Host::paint_content() {
  BenchScope bench("paint_content", 3);
  if (!open_ || !surface_ || w_ <= 0 || h_ <= 0) return;
  if (!wl_ || !wl_->compositor()) return;

  // Live-update support for launcher grid "row count" (and columns) changes.
  // When the user changes Columns or Rows in the launcher settings tab while
  // the launchpad is open, the snapshot will have the new values. We detect
  // the difference, blow away the content cache (so baseChanged will be true
  // and the grid page will be re-rendered with fresh L.gridCols/L.gridRows),
  // and kick a frame so the layer surface actually repaints. This makes the
  // top-level folder cards (the 9 folders) wrap according to the configured
  // density instead of all appearing on one long first row.
  {
    const auto& ap = eh::config::shell_config_snapshot().appearance;
    if (last_grid_cols_ < 0) {
      last_grid_cols_ = ap.launchpadGridColumns;
      last_grid_rows_ = ap.launchpadGridRows;
    } else if (last_grid_cols_ != ap.launchpadGridColumns ||
               last_grid_rows_ != ap.launchpadGridRows) {
      last_grid_cols_ = ap.launchpadGridColumns;
      last_grid_rows_ = ap.launchpadGridRows;
      cached_content_key_ = 0;
      cached_from_page_ = -1;
      cached_to_page_ = -1;
      // Kick the frame callback so paint actually runs and the surface updates.
      schedule_page_frame();
    }
  }

  // Launchpad must not depend on the dock's render backend: paint always
  // happens into cpu_buf_; Vulkan is only an upload/present optimization
  // with a plain SHM fallback when the dock runs in CPU mode.
  bool useVk = dock_.dockVk && dock_.dockVk->valid();
  if (useVk && !vk_layer_.valid()) {
    if (!vk_layer_.create(*dock_.dockVk, wl_->display(), surface_, w_, h_))
      useVk = false;
  }

  if (!cpu_buf_.ensure(w_, h_)) return;

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const float alpha = static_cast<float>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Launchpad));

  const LaunchpadPaintModel pm = paint_model();
  bench.checkpoint("setup");

  const bool inSlide = (pm.page_slide_t >= 0.f && pm.page_slide_from != pm.page_slide_to);
  const bool useSlideCache = inSlide &&
      cached_from_page_ == pm.page_slide_from && cached_to_page_ == pm.page_slide_to &&
      cached_page_from_.cairo_surface() && cached_page_to_.cairo_surface();

  const uint64_t baseKey = inSlide ? 0 : base_content_key();
  const bool baseChanged = (baseKey != cached_content_key_);

  const auto& ap = sc.appearance;
  const auto mc = eh::config::derived_chrome_colors(ap);
  const double bs = std::clamp(static_cast<double>(alpha), 0.0, 1.0);
  const double primR = mc.accentR, primG = mc.accentG, primB = mc.accentB;
  const double outR = mc.outlineR, outG = mc.outlineG, outB = mc.outlineB;
  const double baseUs = dock_ui_scale(sc.dock);
  const double us = baseUs * (ap.launchpadDpiScalePct / 100.0);

  LaunchpadLayout L;
  if (pm.layout) L = *pm.layout;
  else L = compute_launchpad_layout(pm, static_cast<double>(w_), static_cast<double>(h_));

  // Render the base grid to output_cache_ if stale.
  if (!inSlide && baseChanged && w_ > 0 && h_ > 0) {
    if (output_cache_.width() != w_ || output_cache_.height() != h_)
      output_cache_.ensure(w_, h_);

    cairo_t* cache_cr = cairo_create(output_cache_.cairo_surface());
    cairo_set_operator(cache_cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cache_cr);
    cairo_set_operator(cache_cr, CAIRO_OPERATOR_OVER);

    cairo_save(cache_cr);
    cairo_rectangle(cache_cr, L.gridLeft, L.gridTop, L.gridW, std::max(0.0, L.gridBottom - L.gridTop));
    cairo_clip(cache_cr);
    eh_launchpad_render_page(cache_cr, dock_, pm, L, pm.page, alpha, mc);
    cairo_restore(cache_cr);

    cairo_destroy(cache_cr);
    cached_content_key_ = baseKey;
  }

  // Paint to cpu_buf_.
  cairo_t* cr = cpu_buf_.cairo();
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  // Search bar (always visible) - glassy Tahoe style pill
  {
    m3::Box sb;
    sb.setColor(0.15f, 0.16f, 0.20f, 0.85f * static_cast<float>(bs));
    sb.setRadius(static_cast<float>(L.searchBarH * 0.5));
    sb.setGeometry(static_cast<float>(L.searchBarX), static_cast<float>(L.searchBarY),
                   static_cast<float>(L.searchBarW), static_cast<float>(L.searchBarH));
    sb.setGlassy(true);
    sb.paint(cr);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0 * us);
    const double iconCx = L.searchBarX + 18.0 * us;
    const double iconCy = L.searchBarY + L.searchBarH * 0.5;
    const double textX = L.searchBarX + 40.0 * us;
    const double textY = L.searchBarY + L.searchBarH * 0.64;
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, bs);
    eh::shell::draw_material_glyph(cr, iconCx, iconCy, 18.0 * us, "search", 1.0, 1.0, 1.0, bs);
    cairo_move_to(cr, textX, textY);
    if (pm.search_query.empty())
      cairo_show_text(cr, "Search");
    else
      cairo_show_text(cr, pm.search_query.data());
  }

  // Organize button - glassy
  {
    m3::Box ob;
    ob.setColor(1.0f, 1.0f, 1.0f, pm.organize_hovered ? 0.15f : 0.05f);
    ob.setRadius(static_cast<float>(L.organizeBtnH * 0.2));
    ob.setGeometry(static_cast<float>(L.organizeBtnX), static_cast<float>(L.organizeBtnY),
                   static_cast<float>(L.organizeBtnW), static_cast<float>(L.organizeBtnH));
    ob.setGlassy(true);
    ob.paint(cr);
    const double gp = L.organizeBtnH * 0.6;
    eh::shell::draw_material_glyph(cr, L.organizeBtnX + L.organizeBtnW * 0.5, L.organizeBtnY + L.organizeBtnH * 0.5,
                                   gp, "auto_awesome", 1.0, 1.0, 1.0, pm.organize_hovered ? 0.9 : 0.5);
  }

  // Grid area
  if (inSlide) {
    // Slide animation path
    if (useSlideCache) {
      cairo_save(cr);
      cairo_rectangle(cr, L.gridLeft, L.gridTop, L.gridW, std::max(0.0, L.gridBottom - L.gridTop));
      cairo_clip(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
      const double u = std::clamp(static_cast<double>(pm.page_slide_t), 0.0, 1.0);
      const double slide = u * L.gridW;
      cairo_save(cr);
      cairo_translate(cr, -slide, 0);
      cairo_set_source_surface(cr, cached_page_from_.cairo_surface(), 0, 0);
      cairo_paint(cr);
      cairo_translate(cr, L.gridW, 0);
      cairo_set_source_surface(cr, cached_page_to_.cairo_surface(), 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
      cairo_restore(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else {
      eh_launchpad_paint_body(dock_, cr, false, true, alpha, pm);
    }
  } else {
    // Steady state: blit cached grid from output_cache_
    if (output_cache_.cairo_surface() && cached_content_key_ != 0) {
      cairo_save(cr);
      cairo_rectangle(cr, L.gridLeft, L.gridTop, L.gridW, std::max(0.0, L.gridBottom - L.gridTop));
      cairo_clip(cr);
      cairo_set_source_surface(cr, output_cache_.cairo_surface(), 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      eh_launchpad_paint_body(dock_, cr, false, true, alpha, pm);
    }

    // Hover/selection overlays
    eh_launchpad_paint_grid_hover_overlays(cr, dock_, pm, L, bs, primR, primG, primB, outR, outG, outB);
  }

  // Page dots (always painted fresh) - placed under the centered grid block so they scale/move uniformly with DPI
  if (L.pageCount > 1) {
    const double spacing = 22.0 * us;
    const double activeW = 28.0 * us;
    const double activeH = 6.0 * us;
    const double inactiveR = 3.0 * us;
    const double cy = L.gridBottom + 20.0 * us;
    const double span = static_cast<double>(L.pageCount - 1) * spacing;
    double ox = (L.gridLeft + L.gridW * 0.5) - span * 0.5;
    for (int p = 0; p < L.pageCount; ++p) {
      const double cx = ox + static_cast<double>(p) * spacing;
      if (p == pm.page) {
        m3::Box dot;
        dot.setColor(static_cast<float>(primR), static_cast<float>(primG),
                     static_cast<float>(primB), static_cast<float>(bs));
        dot.setRadius(static_cast<float>(activeH * 0.5));
        dot.setGeometry(static_cast<float>(cx - activeW * 0.5), static_cast<float>(cy - activeH * 0.5),
                        static_cast<float>(activeW), static_cast<float>(activeH));
        dot.setGlassy(true);
        dot.paint(cr);
      } else {
        cairo_arc(cr, cx, cy, inactiveR, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, outR, outG, outB, 0.45 * bs);
        cairo_fill(cr);
      }
    }
  }

  // Power confirm
  if (pm.power_confirm_open && pm.power_confirm_idx >= 1 && pm.power_confirm_idx <= 3) {
    eh::shell::dock::app_drawer::paint_power_confirm_modal(cr, static_cast<double>(w_), static_cast<double>(h_),
                                              pm.power_confirm_idx, pm.pointer_x, pm.pointer_y, mc);
  }

  // Folder overlay
  if (pm.folder_open) {
    const double usO = baseUs * (ap.launchpadDpiScalePct / 100.0);
    const double bsO = std::clamp(static_cast<double>(alpha), 0.0, 1.0);
    eh_launchpad_paint_folder_overlay(cr, dock_, pm, bsO, primR, primG, primB,
                                       outR, outG, outB, usO, mc);
  }

  // Drag ghost
  if (pm.drag_active && pm.hits && pm.drag_src_idx >= 0 &&
      pm.drag_src_idx < static_cast<int>(pm.hits->size())) {
    const auto& src = (*pm.hits)[static_cast<size_t>(pm.drag_src_idx)];
    const double ghostSz = 80.0 * us;
    const double ghostX = pm.pointer_x - ghostSz * 0.5;
    const double ghostY = pm.pointer_y - ghostSz * 0.5;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_translate(cr, ghostX + ghostSz * 0.5, ghostY + ghostSz * 0.5);
    cairo_scale(cr, 0.85, 0.85);
    cairo_translate(cr, -ghostX - ghostSz * 0.5, -ghostY - ghostSz * 0.5);
    cairo_arc(cr, ghostX + ghostSz * 0.5, ghostY + ghostSz * 0.5, ghostSz * 0.6, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.08, 0.09, 0.12, 0.7);
    cairo_fill(cr);
    if (src.folderAppIds.empty()) {
      if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(dock_, src.path, src.iconKey)) {
        if (ic->surface) {
          cairo_save(cr);
          cairo_translate(cr, ghostX, ghostY);
          const double sc = ghostSz / std::max(1.0, std::max(static_cast<double>(ic->width), static_cast<double>(ic->height)));
          cairo_scale(cr, sc, sc);
          cairo_set_source_surface(cr, ic->surface, 0, 0);
          cairo_paint_with_alpha(cr, 0.85);
          cairo_restore(cr);
        }
      }
    }
    cairo_restore(cr);
  }

  // Folder child drag ghost
  if (pm.folder_drag_active && pm.folder_children && pm.folder_drag_child_idx >= 0 &&
      pm.folder_drag_child_idx < static_cast<int>(pm.folder_children->size())) {
    const auto& src = (*pm.folder_children)[static_cast<size_t>(pm.folder_drag_child_idx)];
    const double ghostSz = 80.0 * us;
    const double ghostX = pm.pointer_x - ghostSz * 0.5;
    const double ghostY = pm.pointer_y - ghostSz * 0.5;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_translate(cr, ghostX + ghostSz * 0.5, ghostY + ghostSz * 0.5);
    cairo_scale(cr, 0.85, 0.85);
    cairo_translate(cr, -ghostX - ghostSz * 0.5, -ghostY - ghostSz * 0.5);
    cairo_arc(cr, ghostX + ghostSz * 0.5, ghostY + ghostSz * 0.5, ghostSz * 0.6, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 0.08, 0.09, 0.12, 0.7);
    cairo_fill(cr);
    if (const eh::icons::IconEntry* ic = eh_app_drawer_resolve_catalog_icon(dock_, src.path, src.iconKey)) {
      if (ic->surface) {
        cairo_save(cr);
        cairo_translate(cr, ghostX, ghostY);
        const double sc = ghostSz / std::max(1.0, std::max(static_cast<double>(ic->width), static_cast<double>(ic->height)));
        cairo_scale(cr, sc, sc);
        cairo_set_source_surface(cr, ic->surface, 0, 0);
        cairo_paint_with_alpha(cr, 0.85);
        cairo_restore(cr);
      }
    }
    cairo_restore(cr);
  }

  // Context menu
  if (ctx_open_ && ctx_anchor_row_ >= 0 && ctx_anchor_row_ < static_cast<int>(hits_.size())) {
    eh::shell::dock::app_drawer::AppDrawerChromeColors adc{};
    adc.dockFillR = mc.dockFillR;
    adc.dockFillG = mc.dockFillG;
    adc.dockFillB = mc.dockFillB;
    adc.drawerDimR = mc.drawerDimR;
    adc.drawerDimG = mc.drawerDimG;
    adc.drawerDimB = mc.drawerDimB;
    adc.accentR = mc.accentR;
    adc.accentG = mc.accentG;
    adc.accentB = mc.accentB;
    adc.outlineR = mc.outlineR;
    adc.outlineG = mc.outlineG;
    adc.outlineB = mc.outlineB;

    const char* l0 = ctx_pinned_dock_ ? "Unpin from dock" : "Pin to dock";
    const char* l1 = ctx_pinned_start_ ? "Unpin from Start" : "Pin to Start";
    const char* l2 = ctx_pinned_drawer_ ? "Unpin from drawer" : "Pin to drawer";
    const char* linesR[3] = {l0, l1, l2};
    constexpr int n = 3;
    double mx = 0, my = 0, mw = 0, mh = 0;
    eh::shell::dock::app_drawer::app_drawer_row_context_menu_layout(
        ctx_menu_x_, ctx_menu_y_, static_cast<double>(w_), static_cast<double>(h_), n, &mx, &my, &mw, &mh);
    eh::shell::dock::app_drawer::app_drawer_row_context_menu_paint(cr, mx, my, linesR, n, ctx_hover_item_, adc);
  }

  cairo_restore(cr);

  eh_launchpad_set_content_input_region(wl_->compositor(), surface_, pm);

  cairo_surface_flush(cpu_buf_.cairo_surface());
  bool presented = false;
  if (useVk && dock_.dockVk) {
    presented =
        vk_layer_.present_cpu_bgra(*dock_.dockVk, cpu_buf_.data(), w_, h_, cpu_buf_.stride());
  }
  if (!presented) {
    if (!useVk) {
      vk_layer_.detach();
      vk_layer_.destroy();
    }
    // SHM fallback: copy the finished frame into an shm buffer and commit it.
    if (wl_->shm() && shm_buf_.ensure(wl_->shm(), "launchpad-content", w_, h_) &&
        cpu_buf_.data() && shm_buf_.data()) {
      const int srcStride = cpu_buf_.stride();
      const int dstStride = shm_buf_.stride();
      const unsigned char* src = cpu_buf_.data();
      auto* dst = static_cast<unsigned char*>(shm_buf_.data());
      if (srcStride == dstStride) {
        std::memcpy(dst, src, static_cast<std::size_t>(dstStride) * static_cast<std::size_t>(h_));
      } else {
        const int row = std::min(srcStride, dstStride);
        for (int y = 0; y < h_; ++y)
          std::memcpy(dst + static_cast<std::size_t>(y) * dstStride,
                      src + static_cast<std::size_t>(y) * srcStride,
                      static_cast<std::size_t>(row));
      }
      cairo_surface_mark_dirty(shm_buf_.cairo_surface());
      wl_surface_attach(surface_, shm_buf_.wl(), 0, 0);
      wl_surface_damage_buffer(surface_, 0, 0, w_, h_);
      shm_buf_.mark_busy();
      presented = true;
    }
  }
  if (presented) {
    wl_surface_damage_buffer(surface_, 0, 0, w_, h_);
    wl_surface_commit(surface_);
  }
  bench.checkpoint("commit");
}

void Host::request_repaint() {
  if (!open_ || !surface_) return;
  schedule_page_frame();
}

void Host::repaint_all() {
    
  paint_backdrop();
  paint_content();
}

// Layer surface management.

wl_output* Host::pick_dock_follow_output() const {
  if (!dock_.dockLayers.empty() && dock_.dockLayers[0] && dock_.dockLayers[0]->wlOut) {
    for (const auto& slot : dock_.outputSlots) {
      if (slot && slot->output == dock_.dockLayers[0]->wlOut && !slot->output_name.empty()) {
        if (auto* lpOut = wl_->output_by_name(slot->output_name))
          return lpOut;
        break;
      }
    }
  }
  return wl_->pick_largest_logical_output().output;
}

bool Host::create_layer() {
   
  if (surface_) return true;
  if (!wl_ || !wl_->compositor() || !wl_->layer_shell()) return false;

  if (wl_->seat() && !seat_) {
    seat_ = wl_->seat();
    wl_seat_add_listener(seat_, &kSeatListener, this);
    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) {
      wl_pointer_set_user_data(pointer_, this);
      wl_pointer_add_listener(pointer_, &kPointerListener, this);
    }
    keyboard_ = wl_seat_get_keyboard(seat_);
    if (keyboard_)
      wl_keyboard_add_listener(keyboard_, &kKeyboardListener, this);
  }

  eh::wayland::LayerSurfaceConfig bcfg{};
  bcfg.nameSpace     = eh::shell::kDockNamespace;
  bcfg.layer         = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  bcfg.anchor        = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  bcfg.width         = 0;
  bcfg.height        = 0;
  bcfg.exclusiveZone = -1;
  bcfg.keyboard      = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

  wl_surface* bsurf = nullptr;
  zwlr_layer_surface_v1* blayer = nullptr;
  auto* const primaryOut = pick_dock_follow_output();
  lp_log("create_layer primaryOut=" + (primaryOut ? std::to_string((uintptr_t)primaryOut) : "null"));
  if (!eh::wayland::create_layer_surface(wl_->compositor(), wl_->layer_shell(), primaryOut, bcfg,
                                         &kLayerListener, this, &bsurf, &blayer))
    return false;
  backdrop_surface_ = bsurf;
  backdrop_layer_   = blayer;
  if (wl_->viewporter())
    backdrop_viewport_ = wp_viewporter_get_viewport(wl_->viewporter(), backdrop_surface_);
  wl_surface_commit(backdrop_surface_);

  eh::wayland::LayerSurfaceConfig cfg{};
  cfg.nameSpace     = eh::shell::kLaunchpadNamespace;
  cfg.layer         = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  cfg.anchor        = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  cfg.width         = 0;
  cfg.height        = 0;
  cfg.exclusiveZone = -1;
  cfg.keyboard      = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;

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
  return true;
}

void Host::destroy_layer() {
  lp_log("destroy_layer");
  page_anim_.cancel_all();
  if (page_frame_cb_) { wl_callback_destroy(page_frame_cb_); page_frame_cb_ = nullptr; }
  vk_layer_.destroy();
  cpu_buf_.destroy();
  output_cache_.destroy();
  backdrop_buf_.destroy();
  if (backdrop_surface_) {
    wl_surface_attach(backdrop_surface_, nullptr, 0, 0);
    wl_surface_commit(backdrop_surface_);
  }
  if (surface_) {
    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
  }
  if (layer_)   { zwlr_layer_surface_v1_destroy(layer_);   layer_   = nullptr; }
  if (surface_) { wl_surface_destroy(surface_);             surface_ = nullptr; }
  if (backdrop_viewport_) { wp_viewport_destroy(backdrop_viewport_); backdrop_viewport_ = nullptr; }
  if (backdrop_layer_)   { zwlr_layer_surface_v1_destroy(backdrop_layer_);   backdrop_layer_   = nullptr; }
  if (backdrop_surface_) { wl_surface_destroy(backdrop_surface_);             backdrop_surface_ = nullptr; }
  cached_page_from_.destroy();
  cached_page_to_.destroy();
  cached_from_page_ = -1;
  cached_to_page_ = -1;
  w_ = h_ = 0;
}

// Open/close.

void Host::close() {
  lp_log("close");
  if (!open_) return;
  open_ = false;
  ctx_open_ = false;
  ctx_anchor_row_ = -1;
  close_folder();
  drag_candidate_ = false;
  drag_active_ = false;
  drag_src_idx_ = -1;
  folder_drag_candidate_ = false;
  folder_drag_active_ = false;
  folder_drag_child_idx_ = -1;
  scrollAccum_ = 0.0;
  destroy_layer();
  if (wl_) {
    wl_display_flush(wl_->display());
    (void)wl_display_roundtrip(wl_->display());
  }
}

void Host::toggle(int rel_x, uint32_t serial) {
  lp_log("toggle open_=" + std::to_string(open_));
  (void)rel_x; (void)serial;
  if (open_) { close(); return; }
  popup_close(dock_);

  if (dock_.display) (void)wl_display_roundtrip(dock_.display);

  hits_.clear();
  query_.clear();
  open_appdir_inotify();
  drain_appdir_inotify();
  refresh_catalog();
  ctx_open_            = false;
  ctx_anchor_row_      = -1;
  power_confirm_open_ = false;
  power_confirm_idx_  = -1;
  sel_                = -1;
  hover_idx_          = -1;
  ptr_x_ = ptr_y_     = 0;

  if (!dock_.configured) {
    if (eh_app_drawer_debug_level() >= 1)
      eh::app_drawer::trace_line(1, "launchpad", "suppressed: dock layer not configured yet");
    return;
  }
  if (zwlr_layer_surface_v1* pl = dock_popup_parent_layer_surface(dock_))
    zwlr_layer_surface_v1_set_keyboard_interactivity(pl, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  if (wl_surface* ps = dock_popup_parent_wl_surface(dock_)) wl_surface_commit(ps);

  if (!create_layer()) {
    std::cerr << "[launchpad] create_layer_surface failed\n";
    return;
  }

  open_ = true;
  if (w_ > 0 && h_ > 0) repaint_all();
  if (wl_) wl_display_flush(wl_->display());
}

} // namespace eh::shell::launchpad
