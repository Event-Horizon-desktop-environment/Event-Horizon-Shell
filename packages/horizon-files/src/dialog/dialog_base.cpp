#include "dialog/dialog_base.hpp"

#include "platform/common/log/mangowm_logger.hpp"
#include "wayland/buffer/shm_buffer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <unistd.h>

#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>

namespace eh::dialog {

namespace {

// ── registry listener ───────────────────────────────────────────────

struct GlobalBind {
  wl_compositor** comp;
  wl_shm** shm;
  xdg_wm_base** xdg;
  wl_seat** seat;
};

static void global_add(void* data, wl_registry* reg, uint32_t name,
                       const char* iface, uint32_t) {
   
  auto* g = static_cast<GlobalBind*>(data);
  if (strcmp(iface, wl_compositor_interface.name) == 0) {
    *g->comp = static_cast<wl_compositor*>(wl_registry_bind(reg, name, &wl_compositor_interface, 5));
  } else if (strcmp(iface, wl_shm_interface.name) == 0) {
    *g->shm = static_cast<wl_shm*>(wl_registry_bind(reg, name, &wl_shm_interface, 1));
  } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
    *g->xdg = static_cast<xdg_wm_base*>(wl_registry_bind(reg, name, &xdg_wm_base_interface, 5));
  } else if (strcmp(iface, wl_seat_interface.name) == 0) {
    *g->seat = static_cast<wl_seat*>(wl_registry_bind(reg, name, &wl_seat_interface, 9));
  }
}

static void global_remove(void*, wl_registry*, uint32_t) {
   
}

static constexpr wl_registry_listener kRegListener_{
  .global = global_add,
  .global_remove = global_remove,
};

} // anonymous namespace

// ── constructor / destructor ──────────────────────────────────────

DialogBase::DialogBase(int default_w, int default_h,
                           const char* title)
  : default_w_(default_w), default_h_(default_h),
    title_(title ? title : "") {
   
  width_ = default_w_;
  height_ = default_h_;
}

DialogBase::~DialogBase() {
   
  MANGOWM_INFO("{}", __func__);
  if (thread_.joinable()) thread_.join();
  if (dpy_) wl_display_disconnect(dpy_);
}

// ── run ───────────────────────────────────────────────────────────

auto DialogBase::run() -> Result {
   
  running_ = true;
  result_.response = 1;

  thread_ = std::thread(&DialogBase::thread_main_, this);

  thread_.join();
  return result_;
}

// ── finish / request_redraw ───────────────────────────────────────

void DialogBase::finish(Result res) {
   
  result_ = std::move(res);
  running_ = false;
}

void DialogBase::request_redraw() {
   
  redraw_pending_ = true;
}

// ── Wayland event thread ──────────────────────────────────────────

void DialogBase::thread_main_() {
   
  // 0. Open our own Wayland connection
  dpy_ = wl_display_connect(nullptr);
  if (!dpy_) {
    std::cerr << "[dialog] failed to connect to Wayland display\n";
    return;
  }

  GlobalBind gb{&compositor_, &shm_, &xdg_base_, &seat_};
  auto* reg = wl_display_get_registry(dpy_);
  wl_registry_add_listener(reg, &kRegListener_, &gb);
  wl_display_roundtrip(dpy_);
  wl_display_roundtrip(dpy_);

  if (!compositor_ || !shm_ || !xdg_base_ || !seat_) {
    std::cerr << "[dialog] missing required Wayland globals\n";
    return;
  }

  // 1. Create wl_surface
  surface_ = wl_compositor_create_surface(compositor_);
  if (!surface_) {
    std::cerr << "[dialog] failed to create wl_surface\n";
    return;
  }

  // 2. Create xdg_surface
  xdg_surface_ = xdg_wm_base_get_xdg_surface(xdg_base_, surface_);
  if (!xdg_surface_) {
    std::cerr << "[dialog] failed to create xdg_surface\n";
    wl_surface_destroy(surface_); surface_ = nullptr;
    return;
  }
  xdg_surface_add_listener(xdg_surface_, &kXdgListener_, this);

  // 3. Create xdg_toplevel
  xdg_toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
  xdg_toplevel_add_listener(xdg_toplevel_, &kTopListener_, this);
  xdg_toplevel_set_title(xdg_toplevel_, title_.c_str());
  xdg_toplevel_set_app_id(xdg_toplevel_, "event-horizon-dialog");
  xdg_toplevel_set_min_size(xdg_toplevel_, 320, 200);

  // 4. Commit to get configure event
  wl_surface_commit(surface_);
  wl_display_roundtrip(dpy_);

  if (!configured_) {
    for (int i = 0; i < 50 && !configured_; ++i) {
      wl_display_roundtrip(dpy_);
    }
  }

  // 5. Allocate SHM buffers
  for (auto& buf : bufs_) {
    buf = std::make_unique<eh::wayland::ShmBuffer>();
    buf->set_release_hook(on_buf_release_, this);
    buf->ensure(shm_, "dlg", width_, height_);
  }

  // 6. Bind to seat for input
  auto* pointer = wl_seat_get_pointer(seat_);
  auto* keyboard = wl_seat_get_keyboard(seat_);

  struct InputState {
    DialogBase* dlg;
    int px = 0, py = 0;
  };
  InputState is{this, 0, 0};

  static wl_pointer_listener ptr_listener;
  ptr_listener = {
    .enter = [](void* data, wl_pointer*, uint32_t, wl_surface* surf,
                wl_fixed_t sx, wl_fixed_t sy) {
      auto* s = static_cast<InputState*>(data);
      if (surf == s->dlg->surface_) {
        s->px = wl_fixed_to_int(sx);
        s->py = wl_fixed_to_int(sy);
      }
    },
    .leave = [](void*, wl_pointer*, uint32_t, wl_surface*) {},
    .motion = [](void* data, wl_pointer*, uint32_t,
                 wl_fixed_t sx, wl_fixed_t sy) {
       
      auto* s = static_cast<InputState*>(data);
      s->px = wl_fixed_to_int(sx);
      s->py = wl_fixed_to_int(sy);
      s->dlg->on_pointer_move(s->px, s->py);
    },
    .button = [](void* data, wl_pointer*, uint32_t, uint32_t,
                 uint32_t button, uint32_t state) {
       
      auto* s = static_cast<InputState*>(data);
      if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        s->dlg->on_click(s->px, s->py, static_cast<int>(button));
      }
    },
    .axis = [](void* data, wl_pointer*, uint32_t, uint32_t axis,
                wl_fixed_t value) {
       
      auto* s = static_cast<InputState*>(data);
      if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        s->dlg->on_scroll(s->px, s->py, 0, wl_fixed_to_double(value));
      }
    },
    .frame = [](void*, wl_pointer*) {},
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_value120 = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_relative_direction = [](void*, wl_pointer*, uint32_t, uint32_t) {},
  };
  wl_pointer_add_listener(pointer, &ptr_listener, &is);

  static wl_keyboard_listener kb_listener;
  kb_listener = {
    .keymap = [](void*, wl_keyboard*, uint32_t fmt, int32_t fd, uint32_t) {
      if (fmt == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && fd >= 0) close(fd);
    },
    .enter = [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {},
    .leave = [](void*, wl_keyboard*, uint32_t, wl_surface*) {},
    .key = [](void* data, wl_keyboard*, uint32_t, uint32_t,
              uint32_t keycode, uint32_t state) {
      auto* s = static_cast<InputState*>(data);
      char buf[8] = {};
      s->dlg->on_key(static_cast<xkb_keysym_t>(keycode), state, buf, 0);
    },
    .modifiers = [](void*, wl_keyboard*, uint32_t, uint32_t, uint32_t,
                    uint32_t, uint32_t) {},
    .repeat_info = [](void*, wl_keyboard*, int32_t, int32_t) {},
  };
  wl_keyboard_add_listener(keyboard, &kb_listener, &is);

  // 7. Draw initial frame
  {
    auto& buf = bufs_[front_];
    auto* cr = buf->cairo();
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
    draw(cr, width_, height_);
    cairo_surface_flush(buf->cairo_surface());

    wl_surface_attach(surface_, buf->wl(), 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
    wl_surface_commit(surface_);
    buf->mark_busy();
    front_ = 1 - front_;
  }

  // 8. Event loop
  int dpy_fd = wl_display_get_fd(dpy_);
  while (running_) {
    wl_display_flush(dpy_);

    pollfd pfd;
    pfd.fd = dpy_fd;
    pfd.events = POLLIN | POLLERR | POLLHUP;
    int ret = poll(&pfd, 1, 50);
    if (ret < 0) {
      if (errno == EINTR) continue;
      break;
    }

    if (pfd.revents & (POLLERR | POLLHUP)) break;

    if (pfd.revents & POLLIN) {
      if (wl_display_dispatch(dpy_) < 0) break;
    }

    if (redraw_pending_ && configured_ && running_) {
      redraw_pending_ = false;

      int idx = (bufs_[0]->busy() ? 1 : 0);
      if (idx < 2 && bufs_[idx]->busy()) continue;
      auto& buf = bufs_[idx];
      if (!buf->cairo()) continue;

      if (buf->width() != width_ || buf->height() != height_) {
        buf->ensure(shm_, "dlg", width_, height_);
      }

      auto* cr = buf->cairo();
      cairo_save(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cr);
      cairo_restore(cr);
      draw(cr, width_, height_);
      cairo_surface_flush(buf->cairo_surface());

      wl_surface_attach(surface_, buf->wl(), 0, 0);
      wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
      wl_surface_commit(surface_);
      buf->mark_busy();
    }
  }

  // 9. Cleanup
  wl_pointer_destroy(pointer);
  wl_keyboard_destroy(keyboard);

  bufs_[0].reset();
  bufs_[1].reset();

  xdg_toplevel_destroy(xdg_toplevel_);
  xdg_surface_destroy(xdg_surface_);
  wl_surface_destroy(surface_);
  wl_display_roundtrip(dpy_);
  wl_display_disconnect(dpy_);
  dpy_ = nullptr;
}

// ── xdg_listeners ─────────────────────────────────────────────────

void DialogBase::on_xdg_configure_(void* data, xdg_surface* xdg, uint32_t serial) {
   
  auto* self = static_cast<DialogBase*>(data);
  if (xdg == self->xdg_surface_) {
    xdg_surface_ack_configure(xdg, serial);
    self->configured_ = true;
    self->request_redraw();
  }
}

void DialogBase::on_toplevel_configure_(void* data, xdg_toplevel*,
                                          int32_t w, int32_t h, wl_array*) {
   
  auto* self = static_cast<DialogBase*>(data);
  if (w > 0) self->width_ = w;
  if (h > 0) self->height_ = h;
}

void DialogBase::on_toplevel_close_(void* data, xdg_toplevel*) {
   
  auto* self = static_cast<DialogBase*>(data);
  Result r; r.response = 1;
  self->finish(std::move(r));
}

// ── buffer release ────────────────────────────────────────────────

void DialogBase::on_buf_release_(void* data) {
   
  (void)data;
}

// ── drawing helpers ───────────────────────────────────────────────

bool DialogBase::draw_button(cairo_t* cr, int x, int y, int w, int h,
                               const char* label, bool hovered, bool pressed,
                               double r, double g, double b) {
   
  double radius = std::min(w, h) * 0.25;
  double kAlpha = 0.9;

  double br = pressed ? r * 0.7 : (hovered ? r * 1.1 : r);
  double bg = pressed ? g * 0.7 : (hovered ? g * 1.1 : g);
  double bb = pressed ? b * 0.7 : (hovered ? b * 1.1 : b);

  cairo_save(cr);
  double x0 = x, y0 = y, x1 = x + w, y1 = y + h;
  cairo_new_path(cr);
  cairo_arc(cr, x0 + radius, y0 + radius, radius, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x1 - radius, y0 + radius, radius, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x1 - radius, y1 - radius, radius, 0, M_PI / 2);
  cairo_arc(cr, x0 + radius, y1 - radius, radius, M_PI / 2, M_PI);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, br, bg, bb, kAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  draw_text(cr, label, x + w / 2, y + h / 2, 1, 1, 1, 13);
  return (hovered);
}

void DialogBase::draw_text(cairo_t* cr, const char* text, int x, int y,
                             double r, double g, double b, double font_size) {
   
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string("Sans");
  pango_font_description_set_size(desc, font_size * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);

  pango_layout_set_text(layout, text, -1);
  pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);

  int lw, lh;
  pango_layout_get_pixel_size(layout, &lw, &lh);

  cairo_set_source_rgba(cr, r, g, b, 0.9);
  cairo_move_to(cr, x - lw / 2.0, y - lh / 2.0);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

void DialogBase::draw_text_left(cairo_t* cr, const char* text, int x, int y,
                                  double r, double g, double b, double font_size) {
   
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string("Sans");
  pango_font_description_set_size(desc, font_size * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);

  pango_layout_set_text(layout, text, -1);
  pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);

  int lw, lh;
  pango_layout_get_pixel_size(layout, &lw, &lh);

  cairo_set_source_rgba(cr, r, g, b, 0.9);
  cairo_move_to(cr, x, y - lh / 2.0);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

} // namespace eh::dialog
