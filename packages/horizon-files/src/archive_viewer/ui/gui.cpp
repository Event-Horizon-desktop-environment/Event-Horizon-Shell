#include "archive_viewer/ui/state.hpp"
#include "archive_viewer/core/archive.hpp"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "nanosvg/src/nanosvg.h"
#include "nanosvg/src/nanosvgrast.h"

namespace archive_viewer {
namespace {

// ── Debug log ────────────────────────────────────────────────────────

static void alog(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  FILE* f = fopen("/tmp/horizon-archive.log", "a");
  if (f) {
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm{};
    localtime_r(&ts.tv_sec, &tm);
    std::fprintf(f, "%02d:%02d:%02d.%03ld [gui] %s\n",
                 tm.tm_hour, tm.tm_min, tm.tm_sec,
                 ts.tv_nsec / 1000000, buf);
    std::fclose(f);
  }
}

// ── Buffer listener ──────────────────────────────────────────────────

static void wl_buffer_release(void* data, wl_buffer*) {
  auto* buf = static_cast<ArchiveState::Buffer*>(data);
  buf->busy = false;
}

static const wl_buffer_listener buffer_listener = {wl_buffer_release};

// ── SHM pool helper ──────────────────────────────────────────────────

static int create_shm_fd(size_t size) {
  char name[] = "/tmp/horizon-archive-XXXXXX";
  int fd = mkstemp(name);
  if (fd < 0) return -1;
  unlink(name);
  if (ftruncate(fd, static_cast<off_t>(size)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static bool allocate_shm_buffer(ArchiveState::Buffer& buf, wl_shm* shm,
                                 int width, int height) {
  int stride = width * 4;
  size_t size = static_cast<size_t>(stride * height);
  int fd = create_shm_fd(size);
  if (fd < 0) return false;

  void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return false;
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, fd, static_cast<int32_t>(size));
  wl_buffer* wl_buf = wl_shm_pool_create_buffer(pool, 0, width, height,
                                                  stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  cairo_surface_t* cs = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(data), CAIRO_FORMAT_ARGB32,
      width, height, stride);

  buf.wl_buf = wl_buf;
  buf.cairo_surface = cs;
  buf.cr = cairo_create(cs);
  buf.data = data;
  buf.width = width;
  buf.height = height;
  buf.busy = false;
  wl_buffer_set_user_data(buf.wl_buf, &buf);
  wl_buffer_add_listener(buf.wl_buf, &buffer_listener, &buf);
  return true;
}

static void destroy_buffer(ArchiveState::Buffer& buf) {
  if (buf.cr) cairo_destroy(buf.cr);
  if (buf.cairo_surface) cairo_surface_destroy(buf.cairo_surface);
  if (buf.wl_buf) wl_buffer_destroy(buf.wl_buf);
  if (buf.data) munmap(buf.data, static_cast<size_t>(buf.width * buf.height * 4));
  buf = {};
}

void update_hover(ArchiveState* s, int mx, int my);
void handle_click(ArchiveState* s, int mx, int my, uint32_t serial);
extern const wl_seat_listener seat_listener;

// ── Pointer events ───────────────────────────────────────────────────

struct PointerState {
  ArchiveState* state;
  int x = 0, y = 0;
  bool inside = false;
  bool button_pressed = false;
  uint32_t button_serial = 0;
};

static void pointer_enter(void* data, wl_pointer* ptr, uint32_t serial,
                           wl_surface*, wl_fixed_t sx, wl_fixed_t sy) {
  auto* ps = static_cast<PointerState*>(data);
  ps->inside = true;
  ps->x = wl_fixed_to_int(sx);
  ps->y = wl_fixed_to_int(sy);
  update_hover(ps->state, ps->x, ps->y);
}

static void pointer_leave(void* data, wl_pointer* ptr, uint32_t serial,
                           wl_surface*) {
  auto* ps = static_cast<PointerState*>(data);
  ps->inside = false;
  ps->state->hover_entry = -1;
  ps->state->hover_btn = -1;
  ps->state->hover_gear = false;
  ps->state->hover_close = false;
  ps->state->hover_settings_close = false;
  ps->state->hover_slider = -1;
  ps->state->hover_open_dest = false;
  ps->state->fb_panel.hover_idx = -1;
  ps->state->fb_panel.side_hover_idx = -1;
  ps->state->fb_panel.back_hover = false;
  ps->state->fb_panel.select_hover = false;
  ps->state->fb_panel.cancel_hover = false;
  ps->state->needs_redraw = true;
}

static void set_slider_from_x(ArchiveState* s, int slider, int mx);
static void pointer_motion(void* data, wl_pointer* ptr, uint32_t time,
                            wl_fixed_t sx, wl_fixed_t sy) {
  auto* ps = static_cast<PointerState*>(data);
  ps->x = wl_fixed_to_int(sx);
  ps->y = wl_fixed_to_int(sy);
  if (ps->state->fb_panel.active) {
    ps->state->fb_panel.on_motion(ps->x, ps->y);
    ps->state->needs_redraw = true;
  } else if (ps->button_pressed && ps->state->drag_slider >= 0) {
    set_slider_from_x(ps->state, ps->state->drag_slider, ps->x);
  } else {
    update_hover(ps->state, ps->x, ps->y);
  }
}

static void pointer_button(void* data, wl_pointer* ptr, uint32_t serial,
                            uint32_t time, uint32_t button, uint32_t state_) {
  auto* ps = static_cast<PointerState*>(data);
  ps->button_serial = serial;
  if (state_ == WL_POINTER_BUTTON_STATE_PRESSED) {
    ps->button_pressed = true;
    if (ps->state->fb_panel.active) {
      ps->state->fb_panel.on_click(ps->x, ps->y, button);
      ps->state->needs_redraw = true;
    } else {
      handle_click(ps->state, ps->x, ps->y, serial);
    }
  } else {
    ps->button_pressed = false;
    ps->state->drag_slider = -1;
  }
}

static void pointer_axis(void* data, wl_pointer* ptr, uint32_t time,
                          uint32_t axis, wl_fixed_t value) {
  auto* ps = static_cast<PointerState*>(data);
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    int delta = -wl_fixed_to_int(value);
    if (ps->state->fb_panel.active) {
      ps->state->fb_panel.on_scroll(ps->x, ps->y, 0, delta * 3);
      ps->state->needs_redraw = true;
    } else {
      ps->state->scroll_px = std::clamp(
          ps->state->scroll_px + delta * 3,
          0, std::max(0, ps->state->max_scroll));
      ps->state->needs_redraw = true;
    }
  }
}

static void pointer_frame(void*, wl_pointer*) {}
static void pointer_axis_source(void*, wl_pointer*, uint32_t) {}
static void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
static void pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
static void pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const wl_pointer_listener pointer_listener = {
    pointer_enter, pointer_leave, pointer_motion,
    pointer_button, pointer_axis,
    pointer_frame, pointer_axis_source, pointer_axis_stop, pointer_axis_discrete,
    pointer_axis_value120, pointer_axis_relative_direction};
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop

// ── Keyboard events ──────────────────────────────────────────────────

static void key_keymap(void* data, wl_keyboard*, uint32_t format, int fd,
                        uint32_t size) {
  auto* s = static_cast<ArchiveState*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ,
                                           MAP_PRIVATE, fd, 0));
  if (map_str == MAP_FAILED) { close(fd); return; }

  if (s->keymap) xkb_keymap_unref(s->keymap);
  if (s->xkb) xkb_state_unref(s->xkb);
  s->keymap = xkb_keymap_new_from_string(
      s->xkb_ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (s->keymap)
    s->xkb = xkb_state_new(s->keymap);
  munmap(map_str, size);
  close(fd);
}

static void key_enter(void*, wl_keyboard*, uint32_t, wl_surface*,
                       wl_array*) {}
static void key_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

static void key_key(void* data, wl_keyboard*, uint32_t, uint32_t time,
                     uint32_t key, uint32_t state_) {
  auto* s = static_cast<ArchiveState*>(data);
  if (state_ != WL_KEYBOARD_KEY_STATE_PRESSED) return;
  if (!s->xkb) return;

  xkb_keysym_t sym = xkb_state_key_get_one_sym(s->xkb, key + 8);

  // ── Embedded file browser panel keyboard ──
  if (s->fb_panel.active) {
    s->fb_panel.on_key(static_cast<uint32_t>(sym));
    s->needs_redraw = true;
    return;
  }

  int row_h = static_cast<int>(ArchiveState::row_h * s->zoom_level);

  // ── Create mode keyboard ──
  if (s->create_mode) {
    if (sym == XKB_KEY_Escape) {
      // Close dialog without creating
      s->create_mode = false;
      s->create_editing = false;
      s->needs_redraw = true;
      return;
    }
    if (s->create_editing) {
      // Handle text input for name field
      if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        s->create_editing = false;
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_BackSpace) {
        if (s->create_cursor > 0) {
          s->create_name.erase(s->create_cursor - 1, 1);
          --s->create_cursor;
        }
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_Delete) {
        if (s->create_cursor < static_cast<int>(s->create_name.size())) {
          s->create_name.erase(s->create_cursor, 1);
        }
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_Left) {
        if (s->create_cursor > 0) --s->create_cursor;
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_Right) {
        if (s->create_cursor < static_cast<int>(s->create_name.size())) ++s->create_cursor;
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_Home) {
        s->create_cursor = 0;
        s->needs_redraw = true;
        return;
      }
      if (sym == XKB_KEY_End) {
        s->create_cursor = static_cast<int>(s->create_name.size());
        s->needs_redraw = true;
        return;
      }
      // Printable character
      char buf[8] = {};
      int len = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
      if (len > 0 && buf[0] >= 32) {
        s->create_name.insert(s->create_cursor, buf);
        s->create_cursor += len;
        s->needs_redraw = true;
      }
      return;
    }
    // If not editing, Eat all keys except specific ones
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      s->create_editing = true;
      s->needs_redraw = true;
    }
    return;
  }

  if (sym == XKB_KEY_Escape) {
    if (s->show_settings) {
      s->show_settings = false;
      s->needs_redraw = true;
    } else {
      s->running = false;
    }
  } else if (sym == XKB_KEY_Up) {
    if (s->show_settings) return;
    if (s->hover_entry > 0) --s->hover_entry;
    s->scroll_px = std::min(s->scroll_px,
        s->hover_entry * row_h - row_h * 2);
    s->scroll_px = std::max(0, s->scroll_px);
    s->needs_redraw = true;
  } else if (sym == XKB_KEY_Down) {
    if (s->show_settings) return;
    if (s->hover_entry < static_cast<int>(s->visible_entries.size()) - 1)
      ++s->hover_entry;
    s->scroll_px = std::max(s->scroll_px,
        s->hover_entry * row_h - s->height + s->header_h + s->info_h +
        s->breadcrumb_h + s->footer_h + row_h);
    s->needs_redraw = true;
  } else if (sym == XKB_KEY_Home) {
    s->scroll_px = 0;
    s->hover_entry = 0;
    s->needs_redraw = true;
  } else if (sym == XKB_KEY_End) {
    s->hover_entry = static_cast<int>(s->visible_entries.size()) - 1;
    s->scroll_px = s->max_scroll;
    s->needs_redraw = true;
  } else if (sym == XKB_KEY_space) {
    s->needs_redraw = true;
  } else if (sym == XKB_KEY_Return) {
    s->needs_redraw = true;
  }
}

static void key_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t,
                            uint32_t, uint32_t, uint32_t) {}

static void key_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

static const wl_keyboard_listener keyboard_listener = {
    key_keymap, key_enter, key_leave, key_key, key_modifiers, key_repeat_info};

// ── Seat capabilities ────────────────────────────────────────────────

static void seat_capabilities(void* data, wl_seat* seat_, uint32_t caps) {
  auto* s = static_cast<ArchiveState*>(data);
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
    s->pointer = wl_seat_get_pointer(seat_);
    auto* ps = new PointerState{s};
    wl_pointer_set_user_data(s->pointer, ps);
    wl_pointer_add_listener(s->pointer, &pointer_listener, ps);
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
    auto* ps = static_cast<PointerState*>(wl_pointer_get_user_data(s->pointer));
    delete ps;
    wl_pointer_destroy(s->pointer);
    s->pointer = nullptr;
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) {
    s->keyboard = wl_seat_get_keyboard(seat_);
    wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && s->keyboard) {
    wl_keyboard_destroy(s->keyboard);
    s->keyboard = nullptr;
  }
}

static void seat_name(void*, wl_seat*, const char*) {}

const wl_seat_listener seat_listener = {seat_capabilities, seat_name};

// ── xdg_wm_base ping ─────────────────────────────────────────────────

static void xdg_wm_base_ping(void*, xdg_wm_base* wm, uint32_t serial) {
  xdg_wm_base_pong(wm, serial);
}

static const xdg_wm_base_listener wm_base_listener = {xdg_wm_base_ping};

// ── Registry ─────────────────────────────────────────────────────────

static void registry_global(void* data, wl_registry* registry, uint32_t name,
                             const char* iface, uint32_t version) {
  auto* s = static_cast<ArchiveState*>(data);
  if (strcmp(iface, wl_compositor_interface.name) == 0) {
    s->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, 4));
  } else if (strcmp(iface, wl_shm_interface.name) == 0) {
    s->shm = static_cast<wl_shm*>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (strcmp(iface, wl_seat_interface.name) == 0) {
    s->seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, 7));
    wl_seat_add_listener(s->seat, &seat_listener, s);
  } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
    s->wm_base = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
  }
}

static void registry_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener registry_listener = {
    registry_global, registry_global_remove};

// ── xdg-shell listeners ──────────────────────────────────────────────

static void xdg_surface_configure(void* data, xdg_surface* xdg, uint32_t serial) {
  auto* s = static_cast<ArchiveState*>(data);
  xdg_surface_ack_configure(xdg, serial);
  s->configured = true;
  s->needs_redraw = true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const xdg_surface_listener xdg_surface_listener = {xdg_surface_configure};
#pragma GCC diagnostic pop

static void xdg_toplevel_configure(void* data, xdg_toplevel*,
                                    int32_t width, int32_t height,
                                    wl_array*) {
  auto* s = static_cast<ArchiveState*>(data);
  if (width > 0) s->width = width;
  if (height > 0) s->height = height;
  s->needs_redraw = true;
}

static void xdg_toplevel_close(void* data, xdg_toplevel*) {
  auto* s = static_cast<ArchiveState*>(data);
  s->running = false;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
static const xdg_toplevel_listener toplevel_listener = {
    xdg_toplevel_configure, xdg_toplevel_close};
#pragma GCC diagnostic pop

// ── Frame callback ───────────────────────────────────────────────────

static void frame_done(void* data, wl_callback* cb, uint32_t) {
  auto* s = static_cast<ArchiveState*>(data);
  wl_callback_destroy(cb);
  s->frame_cb = nullptr;
}

static const wl_callback_listener frame_listener = {frame_done};

// ── Helpers ──────────────────────────────────────────────────────────

static void draw_rounded_rect(cairo_t* cr, double x, double y,
                               double w, double h, double r) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

static void set_color(cairo_t* cr, double r, double g, double b, double a) {
  cairo_set_source_rgba(cr, r, g, b, a);
}

// ── Embedded SVG assets ─────────────────────────────────────────────

static const char close_svg[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10" fill="#e74c3c"/><path d="M8 8l8 8M16 8l-8 8" stroke="#fff" stroke-width="2" stroke-linecap="round"/></svg>)SVG";

static const char settings_svg[] = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><g data-name="Layer 2"><g data-name="settings-2"><rect width="24" height="24" transform="rotate(180 12 12)" opacity="0"/><circle cx="12" cy="12" r="1.5"/><path d="M20.32 9.37h-1.09c-.14 0-.24-.11-.3-.26a.34.34 0 0 1 0-.37l.81-.74a1.63 1.63 0 0 0 .5-1.18 1.67 1.67 0 0 0-.5-1.19L18.4 4.26a1.67 1.67 0 0 0-2.37 0l-.77.74a.38.38 0 0 1-.41 0 .34.34 0 0 1-.22-.29V3.68A1.68 1.68 0 0 0 13 2h-1.94a1.69 1.69 0 0 0-1.69 1.68v1.09c0 .14-.11.24-.26.3a.34.34 0 0 1-.37 0L8 4.26a1.72 1.72 0 0 0-1.19-.5 1.65 1.65 0 0 0-1.18.5L4.26 5.6a1.67 1.67 0 0 0 0 2.4l.74.74a.38.38 0 0 1 0 .41.34.34 0 0 1-.29.22H3.68A1.68 1.68 0 0 0 2 11.05v1.89a1.69 1.69 0 0 0 1.68 1.69h1.09c.14 0 .24.11.3.26a.34.34 0 0 1 0 .37l-.81.74a1.72 1.72 0 0 0-.5 1.19 1.66 1.66 0 0 0 .5 1.19l1.34 1.36a1.67 1.67 0 0 0 2.37 0l.77-.74a.38.38 0 0 1 .41 0 .34.34 0 0 1 .22.29v1.09A1.68 1.68 0 0 0 11.05 22h1.89a1.69 1.69 0 0 0 1.69-1.68v-1.09c0-.14.11-.24.26-.3a.34.34 0 0 1 .37 0l.76.77a1.72 1.72 0 0 0 1.19.5 1.65 1.65 0 0 0 1.18-.5l1.34-1.34a1.67 1.67 0 0 0 0-2.37l-.73-.73a.34.34 0 0 1 0-.37.34.34 0 0 1 .29-.22h1.09A1.68 1.68 0 0 0 22 13v-1.94a1.69 1.69 0 0 0-1.68-1.69zM12 15.5a3.5 3.5 0 1 1 3.5-3.5 3.5 3.5 0 0 1-3.5 3.5z"/></g></g></svg>)SVG";

static cairo_surface_t* svg_to_surface(const char* svg_data, int size) {
  std::string copy(svg_data);
  NSVGimage* img = nsvgParse(copy.data(), "px", 96.0f);
  if (!img) return nullptr;
  float scale = static_cast<float>(size) / std::max(img->width, img->height);
  int w = std::max(1, static_cast<int>(img->width * scale));
  int h = std::max(1, static_cast<int>(img->height * scale));
  auto* rgba = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(w) * h * 4));
  if (!rgba) { nsvgDelete(img); return nullptr; }
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) { std::free(rgba); nsvgDelete(img); return nullptr; }
  nsvgRasterize(rast, img, 0, 0, scale, rgba, w, h, w * 4);
  nsvgDeleteRasterizer(rast);
  nsvgDelete(img);
  for (int i = 0; i < w * h; i++) {
    unsigned char* p = rgba + i * 4;
    unsigned char t = p[0]; p[0] = p[2]; p[2] = t;
  }
  static const cairo_user_data_key_t key = {};
  cairo_surface_t* surf = cairo_image_surface_create_for_data(
      rgba, CAIRO_FORMAT_ARGB32, w, h, w * 4);
  cairo_surface_set_user_data(surf, &key, rgba,
      [](void* d) { std::free(d); });
  return surf;
}

// ── format options ─────────────────────────────────────────────

static constexpr const char* kFormatLabels[] = {
  "ZIP", "Tar.gz", "Tar.bz2", "Tar.xz", "7z", "Tar"
};
static constexpr const char* kFormatExts[] = {
  ".zip", ".tar.gz", ".tar.bz2", ".tar.xz", ".7z", ".tar"
};
static constexpr int kFormatCount = 6;

// ── Main draw function ───────────────────────────────────────────────

static void draw_create_dialog(ArchiveState& s, cairo_t* cr);

static void draw_archive_viewer(ArchiveState& s) {
  auto& buf = s.bufs[s.front];
  if (!buf.cr) return;

  cairo_t* cr = buf.cr;
  int w = s.width;
  int h = s.height;
  int row_h = static_cast<int>(ArchiveState::row_h * s.zoom_level);

  // Background (SOURCE operator to fully replace buffer content)
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  set_color(cr, s.bg_r, s.bg_g, s.bg_b, s.bg_opacity);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  // Header bar
  int header_y = 0;
  set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity);
  draw_rounded_rect(cr, 0, header_y, w, s.header_h, 8);
  cairo_fill(cr);

  // Header title
  std::string title = s.archive_path;
  auto slash = title.rfind('/');
  if (slash != std::string::npos) title = title.substr(slash + 1);
  if (title.empty()) title = "Horizon Archive Manager";

  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  cairo_move_to(cr, 12, s.header_h / 2 + 5);
  cairo_show_text(cr, title.c_str());

  // Gear button (settings)
  int gear_x = w - 66;
  int gear_y = (s.header_h - 24) / 2;
  int gear_s = 24;
  set_color(cr, s.hover_gear ? 0.4 : 0.3, 0.3, 0.35, s.panel_opacity);
  draw_rounded_rect(cr, gear_x, gear_y, gear_s, gear_s, 4);
  cairo_fill(cr);
  if (s.icon_settings) {
    cairo_set_source_rgba(cr, s.text_r, s.text_g, s.text_b, 1.0);
    cairo_mask_surface(cr, s.icon_settings, gear_x + 3, gear_y + 3);
  }

  // Close button (X)
  int close_x = w - 36;
  int close_y = (s.header_h - 24) / 2;
  int close_s = 24;
  if (s.icon_close) {
    cairo_set_source_surface(cr, s.icon_close, close_x + 2, close_y + 2);
    cairo_paint(cr);
  }

  // Info bar
  int info_y = s.header_h;
  set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity * 0.7);
  cairo_rectangle(cr, 0, info_y, w, s.info_h);
  cairo_fill(cr);

  // Format info text
  if (s.scan_error) {
    cairo_set_source_rgb(cr, 0.9, 0.3, 0.3);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 12, info_y + s.info_h / 2 + 4);
    cairo_show_text(cr, s.scan_error_msg.c_str());
  } else if (!s.all_entries.empty()) {
    size_t dirs = 0, files = 0;
    for (const auto& e : s.all_entries) {
      if (e.is_dir) ++dirs; else ++files;
    }
    char info_buf[128];
    std::snprintf(info_buf, sizeof(info_buf),
                  "Archive  \xe2\x80\xa2  %zu files  \xe2\x80\xa2  %zu folders",
                  files, dirs);
    cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 12, info_y + s.info_h / 2 + 4);
    cairo_show_text(cr, info_buf);
  }

  // Breadcrumb bar
  int bread_y = info_y + s.info_h;
  set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity * 0.5);
  cairo_rectangle(cr, 0, bread_y, w, s.breadcrumb_h);
  cairo_fill(cr);

  std::string bread_text = "/ " + s.current_dir;
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_move_to(cr, 36, bread_y + s.breadcrumb_h / 2 + 4);
  cairo_show_text(cr, bread_text.c_str());

  // Up button
  int up_x = 6;
  int up_y = bread_y + 4;
  int up_w = 24;
  int up_h = s.breadcrumb_h - 8;
  set_color(cr, s.hover_btn == 0 ? s.hover_r : s.surface_r,
            s.hover_btn == 0 ? s.hover_g : s.surface_g,
            s.hover_btn == 0 ? s.hover_b : s.surface_b, 1.0);
  draw_rounded_rect(cr, up_x, up_y, up_w, up_h, 4);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_set_line_width(cr, 2);
  cairo_move_to(cr, up_x + up_w / 2, up_y + 4);
  cairo_line_to(cr, up_x + 4, up_y + up_h / 2);
  cairo_line_to(cr, up_x + up_w / 2, up_y + up_h - 4);
  cairo_move_to(cr, up_x + up_w - 4, up_y + up_h / 2);
  cairo_line_to(cr, up_x + 4, up_y + up_h / 2);
  cairo_stroke(cr);

  s.btn_x[0] = up_x; s.btn_y[0] = up_y; s.btn_w[0] = up_w; s.btn_h[0] = up_h;

  // File list
  int list_x = 0;
  int list_y = bread_y + s.breadcrumb_h;
  int list_w = w;
  int list_h = h - list_y - s.footer_h;

  // Clip to list area
  cairo_save(cr);
  cairo_rectangle(cr, list_x, list_y, list_w, list_h);
  cairo_clip(cr);

  int total_entries = static_cast<int>(s.visible_entries.size());
  int content_h = total_entries * row_h;
  s.max_scroll = std::max(0, content_h - list_h);

  for (int i = 0; i < total_entries; ++i) {
    int ey = list_y + i * row_h - s.scroll_px;
    if (ey + row_h < list_y || ey > list_y + list_h) continue;

    const auto& ve = s.visible_entries[i];
    bool hovered = (i == s.hover_entry);

    // Row background
    if (hovered) {
      set_color(cr, s.hover_r, s.hover_g, s.hover_b, 1.0);
      cairo_rectangle(cr, 0, ey, w, row_h);
      cairo_fill(cr);
    } else if (ve.selected) {
      set_color(cr, s.selected_r, s.selected_g, s.selected_b, 0.3);
      cairo_rectangle(cr, 0, ey, w, row_h);
      cairo_fill(cr);
    }

    // Checkbox
    int chk_x = 10;
    int chk_y = ey + (row_h - 16) / 2;
    int chk_s = 16;
    if (ve.has_selected_descendants) {
      set_color(cr, s.accent_r, s.accent_g, s.accent_b, 0.5);
      draw_rounded_rect(cr, chk_x, chk_y, chk_s, chk_s, 3);
      cairo_fill(cr);
    } else if (ve.selected) {
      set_color(cr, s.accent_r, s.accent_g, s.accent_b, 1.0);
      draw_rounded_rect(cr, chk_x, chk_y, chk_s, chk_s, 3);
      cairo_fill(cr);
      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_set_line_width(cr, 2);
      cairo_move_to(cr, chk_x + 3, chk_y + chk_s / 2);
      cairo_line_to(cr, chk_x + chk_s / 3, chk_y + chk_s - 4);
      cairo_line_to(cr, chk_x + chk_s - 2, chk_y + 3);
      cairo_stroke(cr);
    } else {
      set_color(cr, s.outline_r, s.outline_g, s.outline_b, 1.0);
      draw_rounded_rect(cr, chk_x, chk_y, chk_s, chk_s, 3);
      cairo_stroke(cr);
    }

    // Icon indicator (folder v file)
    cairo_set_font_size(cr, 13 * s.zoom_level);
    cairo_move_to(cr, 34, ey + row_h / 2 + 4);
    if (ve.is_dir) {
      cairo_set_source_rgb(cr, s.accent_r, s.accent_g, s.accent_b);
      cairo_show_text(cr, "\xe2\x96\xb6");
    } else {
      cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
      cairo_show_text(cr, " ");
    }

    // Filename
    cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13 * s.zoom_level);
    cairo_move_to(cr, 44, ey + row_h / 2 + 4);
    cairo_show_text(cr, ve.name.c_str());

    // Size
    if (!ve.is_dir) {
      char size_buf[32];
      if (ve.size < 1024)
        std::snprintf(size_buf, sizeof(size_buf), "%" PRIu64 " B", ve.size);
      else if (ve.size < 1024 * 1024)
        std::snprintf(size_buf, sizeof(size_buf), "%.0f KB", ve.size / 1024.0);
      else if (ve.size < 1024ull * 1024 * 1024)
        std::snprintf(size_buf, sizeof(size_buf), "%.1f MB", ve.size / (1024.0 * 1024.0));
      else
        std::snprintf(size_buf, sizeof(size_buf), "%.1f GB", ve.size / (1024.0 * 1024.0 * 1024.0));
      cairo_set_source_rgb(cr, 0.6, 0.6, 0.65);
      cairo_set_font_size(cr, 11 * s.zoom_level);
      cairo_text_extents_t te;
      cairo_text_extents(cr, size_buf, &te);
      cairo_move_to(cr, w - te.x_advance - 12, ey + row_h / 2 + 4);
      cairo_show_text(cr, size_buf);
    }

    // Separator line
    if (i < total_entries - 1) {
      set_color(cr, s.outline_r, s.outline_g, s.outline_b, 0.3);
      cairo_rectangle(cr, 8, ey + row_h - 1, w - 16, 1);
      cairo_fill(cr);
    }
  }

  cairo_restore(cr);

  // Scrollbar
  if (s.max_scroll > 0) {
    int sb_x = w - 6;
    int sb_h = std::max(20, list_h * list_h / content_h);
    int sb_y = list_y + (list_h - sb_h) * s.scroll_px / s.max_scroll;
    set_color(cr, 0.5, 0.5, 0.5, 0.4);
    draw_rounded_rect(cr, sb_x, sb_y, 4, sb_h, 2);
    cairo_fill(cr);
  }

  // Footer
  int footer_y = h - s.footer_h;
  set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity);
  cairo_rectangle(cr, 0, footer_y, w, s.footer_h);
  cairo_fill(cr);

  // Figure out entry counts for footer
  int sel_count = 0;
  uint64_t sel_size = 0;
  for (const auto& ve : s.visible_entries) {
    if (ve.selected) {
      ++sel_count;
      sel_size += ve.size;
    }
  }

  int fy = footer_y + 14;
  int frow2 = footer_y + 28;

  // Status line (row 1)
  if (s.extracting) {
    // Progress bar spans the whole footer
    int pb_y = footer_y + 10;
    int pb_h = s.footer_h - 20;
    int pb_w = w - 20;
    int pb_x = 10;

    set_color(cr, 0.25, 0.25, 0.27, 1.0);
    draw_rounded_rect(cr, pb_x, pb_y, pb_w, pb_h, 4);
    cairo_fill(cr);

    int fill_w = static_cast<int>(pb_w * s.progress);
    if (fill_w > 0) {
      set_color(cr, s.accent_r, s.accent_g, s.accent_b, 0.8);
      draw_rounded_rect(cr, pb_x, pb_y, fill_w, pb_h, 4);
      cairo_fill(cr);
    }

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_font_size(cr, 12);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    char prog_buf[64];
    std::snprintf(prog_buf, sizeof(prog_buf), "Extracting... %d%%",
                  static_cast<int>(s.progress * 100));
    cairo_text_extents_t te;
    cairo_text_extents(cr, prog_buf, &te);
    cairo_move_to(cr, (w - te.x_advance) / 2, pb_y + pb_h / 2 + 4);
    cairo_show_text(cr, prog_buf);
  } else {
    if (s.status_text.empty()) {
      char sel_buf[64];
      if (sel_count > 0) {
        if (sel_size < 1024)
          std::snprintf(sel_buf, sizeof(sel_buf), "Selected: %d files (%" PRIu64 " B)", sel_count, sel_size);
        else if (sel_size < 1024 * 1024)
          std::snprintf(sel_buf, sizeof(sel_buf), "Selected: %d files (%.0f KB)", sel_count, sel_size / 1024.0);
        else
          std::snprintf(sel_buf, sizeof(sel_buf), "Selected: %d files (%.1f MB)", sel_count, sel_size / (1024.0 * 1024.0));
        cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, 12, fy);
        cairo_show_text(cr, sel_buf);
      }
    } else {
      cairo_save(cr);
      cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
      cairo_set_font_size(cr, 11);
      cairo_move_to(cr, 12, fy);
      cairo_show_text(cr, s.status_text.c_str());
      cairo_restore(cr);

      if (s.show_open_dest && !s.last_extract_dest.empty()) {
        int od_w = 130;
        int od_h = 22;
        int od_x = 12;
        int od_y = fy + 16;

        s.open_dest_btn_x = od_x;
        s.open_dest_btn_y = od_y;
        s.open_dest_btn_w = od_w;
        s.open_dest_btn_h = od_h;

        set_color(cr,
            s.hover_open_dest ? s.accent_r * 0.8 : s.accent_r,
            s.hover_open_dest ? s.accent_g * 0.8 : s.accent_g,
            s.hover_open_dest ? s.accent_b * 0.8 : s.accent_b, 1.0);
        draw_rounded_rect(cr, od_x, od_y, od_w, od_h, 4);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_font_size(cr, 10);
        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_BOLD);
        cairo_text_extents_t te3;
        cairo_text_extents(cr, "Open destination", &te3);
        cairo_move_to(cr, od_x + (od_w - te3.x_advance) / 2,
                      od_y + od_h / 2 + 4);
        cairo_show_text(cr, "Open destination");
      }
    }

    // Action buttons (row 2) — laid out right-to-left
    s.btn_w[4] = 80;  s.btn_h[4] = 36;
    s.btn_y[4] = frow2;
    s.btn_x[4] = w - s.btn_w[4] - 10;

    s.btn_w[1] = 110; s.btn_h[1] = 36;
    s.btn_y[1] = frow2;
    s.btn_x[1] = s.btn_x[4] - 8 - s.btn_w[1];

    s.btn_w[2] = 130; s.btn_h[2] = 36;
    s.btn_y[2] = frow2;
    s.btn_x[2] = s.btn_x[1] - 8 - s.btn_w[2];

    s.btn_w[3] = 90; s.btn_h[3] = 36;
    s.btn_y[3] = frow2;
    s.btn_x[3] = s.btn_x[2] - 8 - s.btn_w[3];

    const char* btn_labels[5] = {"", "Extract All", "Extract Sel", "Close", "Open"};
    for (int b = 1; b < 5; ++b) {
      bool hov = s.hover_btn == b;
      set_color(cr,
          hov ? s.accent_r * 0.8 : s.accent_r,
          hov ? s.accent_g * 0.8 : s.accent_g,
          hov ? s.accent_b * 0.8 : s.accent_b, 1.0);
      draw_rounded_rect(cr, s.btn_x[b], s.btn_y[b], s.btn_w[b], s.btn_h[b], 6);
      cairo_fill(cr);

      cairo_set_source_rgb(cr, 1, 1, 1);
      cairo_set_font_size(cr, 11);
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_BOLD);
      cairo_text_extents_t te2;
      cairo_text_extents(cr, btn_labels[b], &te2);
      cairo_move_to(cr, s.btn_x[b] + (s.btn_w[b] - te2.x_advance) / 2,
                    s.btn_y[b] + s.btn_h[b] / 2 + 4);
      cairo_show_text(cr, btn_labels[b]);
    }
  }

  // ── Settings panel overlay ────────────────────────────────────────────
  if (s.show_settings) {
    int pw = 360;
    int ph = 370;
    int px = (w - pw) / 2;
    int py = (h - ph) / 2;
    s.settings_x = px;
    s.settings_y = py;
    s.settings_w = pw;
    s.settings_h = ph;

    // Dim overlay
    set_color(cr, 0, 0, 0, 0.4);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    // Panel background
    set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity);
    draw_rounded_rect(cr, px, py, pw, ph, 10);
    cairo_fill(cr);

    // Panel border
    set_color(cr, s.outline_r, s.outline_g, s.outline_b, 1.0);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, px, py, pw, ph, 10);
    cairo_stroke(cr);

    // Title
    cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                            CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 15);
    cairo_move_to(cr, px + 14, py + 22);
    cairo_show_text(cr, "Settings");

    // Close button on settings panel
    int sc_x = px + pw - 28;
    int sc_y = py + 6;
    int sc_s = 20;
    if (s.icon_close) {
      cairo_set_source_surface(cr, s.icon_close, sc_x + 1, sc_y + 1);
      cairo_paint(cr);
    }

    // Sliders
    int sw = pw - 56;
    int sx = px + 28;
    int st_h = 6;
    const char* slider_labels[5] = {
        "Background Opacity",
        "Panel Opacity",
        "Zoom",
        "FB Background Opacity",
        "FB Panel Opacity"
    };
    double slider_vals[5] = {
        s.bg_opacity,
        s.panel_opacity,
        (s.zoom_level - 0.5) / 1.5,
        s.fb_panel.bg_opacity,
        s.fb_panel.panel_opacity
    };

    for (int i = 0; i < 5; ++i) {
      int sy = py + 48 + i * 64;

      // Label
      cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      cairo_move_to(cr, sx, sy);
      cairo_show_text(cr, slider_labels[i]);

      // Track
      int ty = sy + 14;
      s.slider_track_x[i] = sx;
      s.slider_track_y[i] = ty;
      s.slider_track_w = sw;
      s.slider_track_h = st_h;

      bool hovered = (s.hover_slider == i);
      set_color(cr, 0.3, 0.3, 0.33, 1.0);
      draw_rounded_rect(cr, sx, ty, sw, st_h, 3);
      cairo_fill(cr);

      // Filled portion
      int fill_w = static_cast<int>(sw * slider_vals[i]);
      if (fill_w > 0) {
        set_color(cr, s.accent_r, s.accent_g, s.accent_b,
                  hovered ? 0.9 : 0.7);
        draw_rounded_rect(cr, sx, ty, fill_w, st_h, 3);
        cairo_fill(cr);
      }

      // Thumb
      int thumb_x = sx + fill_w;
      int thumb_r = hovered ? 8 : 6;
      set_color(cr, s.text_r, s.text_g, s.text_b,
                hovered ? 1.0 : 0.8);
      cairo_arc(cr, thumb_x, ty + st_h / 2, thumb_r, 0, 2 * M_PI);
      cairo_fill(cr);

      // Value text
      char val_buf[16];
      if (i == 2) {
        std::snprintf(val_buf, sizeof(val_buf), "%.0f%%", s.zoom_level * 100);
      } else {
        std::snprintf(val_buf, sizeof(val_buf), "%.0f%%", slider_vals[i] * 100);
      }
      cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
      cairo_set_font_size(cr, 11);
      cairo_text_extents_t te;
      cairo_text_extents(cr, val_buf, &te);
      cairo_move_to(cr, px + pw - 28 - te.x_advance, sy);
      cairo_show_text(cr, val_buf);
    }
  }

  // ── Create archive dialog overlay ──
  if (s.create_mode) {
    draw_create_dialog(s, cr);
  }

  // ── Embedded file browser panel ──
  if (s.fb_panel.active) {
    s.fb_panel.w = s.width / 2;
    s.fb_panel.h = s.height;
    s.fb_panel.x = 0;
    s.fb_panel.y = 0;
    s.fb_panel.paint(cr);
  }
}

// ── Create archive dialog ──────────────────────────────────────

static void draw_create_dialog(ArchiveState& s, cairo_t* cr) {
  int w = s.width;
  int h = s.height;
  int pw = 480;
  int ph = 350;
  int px = (w - pw) / 2;
  int py = (h - ph) / 2;

  // Dim overlay
  set_color(cr, 0, 0, 0, 0.4);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  // Panel background
  set_color(cr, s.surface_r, s.surface_g, s.surface_b, s.panel_opacity);
  draw_rounded_rect(cr, px, py, pw, ph, 10);
  cairo_fill(cr);

  // Border
  set_color(cr, s.outline_r, s.outline_g, s.outline_b, 1.0);
  cairo_set_line_width(cr, 1);
  draw_rounded_rect(cr, px, py, pw, ph, 10);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);

  // Title
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_set_font_size(cr, 15);
  cairo_move_to(cr, px + 16, py + 26);
  cairo_show_text(cr, "Create Archive");

  // Close X button
  int close_x = px + pw - 28;
  int close_y = py + 8;
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_set_font_size(cr, 18);
  cairo_move_to(cr, close_x, close_y + 16);
  cairo_show_text(cr, "\u00D7");

  // Divider
  set_color(cr, s.outline_r, s.outline_g, s.outline_b, 0.3);
  cairo_rectangle(cr, px, py + 40, pw, 1);
  cairo_fill(cr);

  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);

  int ly = py + 56; // current y in dialog
  int lh = 22;
  int field_x = px + 90;
  int field_w = pw - 110;

  // ── Save in row ──
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_move_to(cr, px + 16, ly + 14);
  cairo_show_text(cr, "Save in:");

  int browse_w = 54;
  int save_fw = field_w - browse_w - 6;
  int save_fx = field_x;

  // path field
  set_color(cr, 0.22, 0.22, 0.24, 1.0);
  draw_rounded_rect(cr, save_fx, ly, save_fw, lh, 4);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_set_font_size(cr, 11);
  cairo_save(cr);
  cairo_rectangle(cr, save_fx + 4, ly + 2, save_fw - 8, lh - 4);
  cairo_clip(cr);
  cairo_move_to(cr, save_fx + 6, ly + 15);
  cairo_show_text(cr, s.create_dest_dir.empty() ? "(select directory)" : s.create_dest_dir.c_str());
  cairo_restore(cr);

  // Browse button
  int bx = save_fx + save_fw + 6;
  bool bh = false; // hover checked in event handler
  set_color(cr, s.accent_r, s.accent_g, s.accent_b, 1.0);
  draw_rounded_rect(cr, bx, ly, browse_w, lh, 4);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_set_font_size(cr, 11);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_text_extents_t te;
  cairo_text_extents(cr, "Br", &te);
  cairo_move_to(cr, bx + (browse_w - te.x_advance) / 2, ly + 15);
  cairo_show_text(cr, "Br");

  ly += 30;

  // ── Name row ──
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_move_to(cr, px + 16, ly + 14);
  cairo_show_text(cr, "Name:");

  // name field
  set_color(cr, 0.22, 0.22, 0.24, 1.0);
  draw_rounded_rect(cr, field_x, ly, field_w, lh, 4);
  cairo_fill(cr);
  if (s.create_editing) {
    set_color(cr, s.accent_r, s.accent_g, s.accent_b, 0.6);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, field_x, ly, field_w, lh, 4);
    cairo_stroke(cr);
  }
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_save(cr);
  cairo_rectangle(cr, field_x + 4, ly + 2, field_w - 12, lh - 4);
  cairo_clip(cr);
  cairo_move_to(cr, field_x + 6, ly + 15);
  cairo_show_text(cr, s.create_name.c_str());
  // cursor
  if (s.create_editing) {
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s.create_name.substr(0, s.create_cursor).c_str(), &ext);
    cairo_move_to(cr, field_x + 6 + ext.x_advance, ly + 6);
    cairo_line_to(cr, field_x + 6 + ext.x_advance, ly + lh - 4);
    cairo_stroke(cr);
  }
  cairo_restore(cr);

  ly += 30;

  // ── Format row ──
  cairo_set_font_size(cr, 12);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_move_to(cr, px + 16, ly + 14);
  cairo_show_text(cr, "Format:");

  // format selector
  int fmt_fw = 160;
  set_color(cr, 0.22, 0.22, 0.24, 1.0);
  draw_rounded_rect(cr, field_x, ly, fmt_fw, lh, 4);
  cairo_fill(cr);
  // dropdown arrow
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  cairo_set_font_size(cr, 10);
  cairo_move_to(cr, field_x + fmt_fw - 16, ly + 14);
  cairo_show_text(cr, "\u25BC");
  // format label
  cairo_set_font_size(cr, 11);
  cairo_move_to(cr, field_x + 8, ly + 15);
  cairo_show_text(cr, kFormatLabels[s.create_format]);

  // Dropdown list
  if (s.create_format_open) {
    int dd_x = field_x;
    int dd_y = ly + lh + 2;
    int dd_w = fmt_fw;
    int dd_item_h = 24;
    int dd_h = dd_item_h * kFormatCount;

    // dropdown background
    set_color(cr, 0.18, 0.18, 0.20, 1.0);
    draw_rounded_rect(cr, dd_x, dd_y, dd_w, dd_h, 4);
    cairo_fill(cr);
    set_color(cr, s.outline_r, s.outline_g, s.outline_b, 0.5);
    cairo_set_line_width(cr, 1);
    draw_rounded_rect(cr, dd_x, dd_y, dd_w, dd_h, 4);
    cairo_stroke(cr);

    cairo_set_font_size(cr, 11);
    for (int i = 0; i < kFormatCount; ++i) {
      int item_y = dd_y + i * dd_item_h;
      if (i == s.create_format_hover) {
        set_color(cr, s.accent_r, s.accent_g, s.accent_b, 0.3);
        cairo_rectangle(cr, dd_x + 2, item_y, dd_w - 4, dd_item_h);
        cairo_fill(cr);
      }
      cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
      cairo_move_to(cr, dd_x + 10, item_y + 16);
      cairo_show_text(cr, kFormatLabels[i]);
    }
  }

  ly += 30;

  // ── Files to add ──
  cairo_set_font_size(cr, 12);
  cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
  char files_buf[64];
  std::snprintf(files_buf, sizeof(files_buf), "Files to add (%zu):", s.create_files.size());
  cairo_move_to(cr, px + 16, ly + 14);
  cairo_show_text(cr, files_buf);

  ly += 22;
  int flist_h = 110;
  cairo_save(cr);
  cairo_rectangle(cr, px + 16, ly, pw - 32, flist_h);
  cairo_clip(cr);
  cairo_set_font_size(cr, 11);
  int fh = 18;
  int max_visible = flist_h / fh;
  int scroll_ofs = std::max(0, static_cast<int>(s.create_files.size()) - max_visible);
  int start = std::min(scroll_ofs, static_cast<int>(s.create_files.size()));
  for (int i = start; i < static_cast<int>(s.create_files.size()); ++i) {
    int item_y = ly + (i - start) * fh;
    cairo_set_source_rgb(cr, s.text_r, s.text_g, s.text_b);
    cairo_move_to(cr, px + 20, item_y + 13);
    // Truncate long paths
    std::string fname = s.create_files[i];
    if (fname.size() > 50) fname = "..." + fname.substr(fname.size() - 47);
    cairo_show_text(cr, fname.c_str());
  }
  cairo_restore(cr);

  // ── Action buttons ──
  int btn_y = py + ph - 40;
  int btn_w = 90;
  int btn_h = 28;
  int create_btn_x = px + pw - btn_w - 16;
  int cancel_btn_x = create_btn_x - btn_w - 8;

  // Cancel
  set_color(cr, 0.35, 0.35, 0.37, 1.0);
  draw_rounded_rect(cr, cancel_btn_x, btn_y, btn_w, btn_h, 6);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 1.0);
  cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12);
  cairo_text_extents(cr, "Cancel", &te);
  cairo_move_to(cr, cancel_btn_x + (btn_w - te.x_advance) / 2, btn_y + 18);
  cairo_show_text(cr, "Cancel");

  // Create
  set_color(cr, s.accent_r, s.accent_g, s.accent_b, 1.0);
  draw_rounded_rect(cr, create_btn_x, btn_y, btn_w, btn_h, 6);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_set_font_size(cr, 12);
  cairo_text_extents(cr, "Create", &te);
  cairo_move_to(cr, create_btn_x + (btn_w - te.x_advance) / 2, btn_y + 18);
  cairo_show_text(cr, "Create");
}

// ── Event handling ───────────────────────────────────────────────────

static void update_hover_normal(ArchiveState* s, int mx, int my);
void update_hover(ArchiveState* s, int mx, int my) {
  s->hover_entry = -1;
  s->hover_btn = -1;
  s->hover_close = false;
  s->hover_gear = false;
  s->hover_settings_close = false;
  s->hover_slider = -1;
  s->hover_open_dest = false;

  if (s->show_settings) {
    // Check settings close button
    int sc_x = s->settings_x + s->settings_w - 28;
    int sc_y = s->settings_y + 6;
    if (mx >= sc_x && mx <= sc_x + 20 &&
        my >= sc_y && my <= sc_y + 20) {
      s->hover_settings_close = true;
      s->needs_redraw = true;
      return;
    }
    // Check sliders
    for (int i = 0; i < 5; ++i) {
      int sx = s->slider_track_x[i];
      int sy = s->slider_track_y[i];
      if (mx >= sx && mx <= sx + s->slider_track_w &&
          my >= sy && my <= sy + s->slider_track_h) {
        s->hover_slider = i;
        s->needs_redraw = true;
        return;
      }
    }
    // Click on overlay outside panel — handled in click
    return;
  }

  update_hover_normal(s, mx, my);
}

static void update_hover_normal(ArchiveState* s, int mx, int my) {
  int row_h = static_cast<int>(ArchiveState::row_h * s->zoom_level);

  // ── Create dialog hover ──
  if (s->create_mode) {
    s->create_format_hover = -1;
    int pw = 480;
    int ph = 350;
    int px = (s->width - pw) / 2;
    int py = (s->height - ph) / 2;

    // Close X button
    int cx = px + pw - 28;
    int cy = py + 8;
    if (mx >= cx && mx <= cx + 20 && my >= cy && my <= cy + 20) {
      s->hover_close = true;
      s->needs_redraw = true;
      return;
    }

    // Name field
    int ly = py + 56 + 30;
    int field_x = px + 90;
    int field_w = pw - 110;
    int lh = 22;
    if (mx >= field_x && mx <= field_x + field_w &&
        my >= ly && my <= ly + lh) {
      s->hover_btn = -1;
      s->needs_redraw = true;
      return;
    }

    // Format dropdown
    int fmt_fw = 160;
    if (mx >= field_x && mx <= field_x + fmt_fw &&
        my >= ly && my <= ly + lh) {
      s->needs_redraw = true;
      return;
    }

    // Dropdown items (if open)
    if (s->create_format_open) {
      int dd_y = ly + lh + 2;
      int dd_item_h = 24;
      if (mx >= field_x && mx <= field_x + fmt_fw &&
          my >= dd_y && my <= dd_y + dd_item_h * kFormatCount) {
        int idx = (my - dd_y) / dd_item_h;
        if (idx >= 0 && idx < kFormatCount) {
          s->create_format_hover = idx;
          s->needs_redraw = true;
          return;
        }
      }
    }

    // Browse button
    int browse_w = 54;
    int save_fw = field_w - browse_w - 6;
    int save_fx = field_x;
    int bx = save_fx + save_fw + 6;
    if (mx >= bx && mx <= bx + browse_w &&
        my >= py + 56 && my <= py + 56 + lh) {
      s->needs_redraw = true;
      return;
    }

    // Action buttons
    int btn_y = py + ph - 40;
    int btn_w = 90;
    int btn_h = 28;
    int create_btn_x = px + pw - btn_w - 16;
    int cancel_btn_x = create_btn_x - btn_w - 8;

    if (mx >= create_btn_x && mx <= create_btn_x + btn_w &&
        my >= btn_y && my <= btn_y + btn_h) {
      s->needs_redraw = true;
      return;
    }
    if (mx >= cancel_btn_x && mx <= cancel_btn_x + btn_w &&
        my >= btn_y && my <= btn_y + btn_h) {
      s->needs_redraw = true;
      return;
    }

    return; // Don't process normal hover when in create mode
  }

  // Check gear button
  int gear_x = s->width - 66;
  int gear_y = (s->header_h - 24) / 2;
  if (mx >= gear_x && mx <= gear_x + 24 &&
      my >= gear_y && my <= gear_y + 24) {
    s->hover_gear = true;
    s->needs_redraw = true;
    return;
  }

  // Check close button
  int close_x = s->width - 36;
  int close_y = (s->header_h - 24) / 2;
  if (mx >= close_x && mx <= close_x + 24 &&
      my >= close_y && my <= close_y + 24) {
    s->hover_close = true;
    s->needs_redraw = true;
    return;
  }

  // Check footer buttons
  for (int b = 1; b < 5; ++b) {
    if (mx >= s->btn_x[b] && mx <= s->btn_x[b] + s->btn_w[b] &&
        my >= s->btn_y[b] && my <= s->btn_y[b] + s->btn_h[b]) {
      s->hover_btn = b;
      s->needs_redraw = true;
      return;
    }
  }

  // Check "Open destination" button
  if (s->show_open_dest && !s->last_extract_dest.empty()) {
    if (mx >= s->open_dest_btn_x && mx <= s->open_dest_btn_x + s->open_dest_btn_w &&
        my >= s->open_dest_btn_y && my <= s->open_dest_btn_y + s->open_dest_btn_h) {
      s->hover_open_dest = true;
      s->needs_redraw = true;
      return;
    }
  }

  if (mx >= s->btn_x[0] && mx <= s->btn_x[0] + s->btn_w[0] &&
      my >= s->btn_y[0] && my <= s->btn_y[0] + s->btn_h[0]) {
    s->hover_btn = 0;
    s->needs_redraw = true;
    return;
  }

  // Check file list
  int list_y = s->header_h + s->info_h + s->breadcrumb_h;
  int list_h = s->height - list_y - s->footer_h;
  if (mx > 0 && mx < s->width - 6 && my >= list_y && my < list_y + list_h) {
    int idx = (my - list_y + s->scroll_px) / row_h;
    if (idx >= 0 && idx < static_cast<int>(s->visible_entries.size())) {
      s->hover_entry = idx;
      s->needs_redraw = true;
      return;
    }
  }
}

static void set_slider_from_x(ArchiveState* s, int slider, int mx) {
  int sx = s->slider_track_x[slider];
  int sw = s->slider_track_w;
  double frac = std::clamp(static_cast<double>(mx - sx) / sw, 0.0, 1.0);
  switch (slider) {
    case 0: s->bg_opacity = frac; break;
    case 1: s->panel_opacity = frac; break;
    case 2: s->zoom_level = 0.5 + frac * 1.5; break;
    case 3: s->fb_panel.bg_opacity = frac; break;
    case 4: s->fb_panel.panel_opacity = frac; break;
  }
  s->needs_redraw = true;
}

// ── config path ─────────────────────────────────────────────────

static std::string archive_config_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) return std::string(xdg) + "/event-horizon/horizon-archive.toml";
  const char* home = std::getenv("HOME");
  return home ? std::string(home) + "/.config/event-horizon/horizon-archive.toml"
              : ".config/event-horizon/horizon-archive.toml";
}

static void load_settings(ArchiveState* s) {
  std::ifstream f(archive_config_path());
  if (!f.is_open()) return;
  try {
    toml::table tbl = toml::parse(f);
    if (auto* st = tbl["settings"].as_table()) {
      auto get_or = [&](const char* key, double def) -> double {
        auto* v = st->get(key);
        return v ? v->value_or(def) : def;
      };
      s->bg_opacity          = get_or("bg_opacity",        1.0);
      s->panel_opacity       = get_or("panel_opacity",     1.0);
      s->zoom_level          = get_or("zoom_level",        1.0);
      s->fb_panel.bg_opacity    = get_or("fb_bg_opacity",     0.85);
      s->fb_panel.panel_opacity = get_or("fb_panel_opacity",  0.92);
    }
  } catch (...) {}
}

static void save_settings(ArchiveState* s) {
  try {
    toml::table settings;
    settings.emplace("bg_opacity",        s->bg_opacity);
    settings.emplace("panel_opacity",     s->panel_opacity);
    settings.emplace("zoom_level",        s->zoom_level);
    settings.emplace("fb_bg_opacity",     s->fb_panel.bg_opacity);
    settings.emplace("fb_panel_opacity",  s->fb_panel.panel_opacity);
    toml::table root;
    root.emplace("settings", std::move(settings));
    std::string path = archive_config_path();
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream out(path);
    if (out.is_open()) out << root << "\n";
  } catch (...) {}
}

void handle_click(ArchiveState* s, int mx, int my, uint32_t serial) {
  // ── Create dialog click ──
  if (s->create_mode) {
    int pw = 480;
    int ph = 350;
    int px = (s->width - pw) / 2;
    int py = (s->height - ph) / 2;

    // Close X button
    int cx = px + pw - 28;
    int cy = py + 8;
    if (mx >= cx && mx <= cx + 20 && my >= cy && my <= cy + 20) {
      s->create_mode = false;
      s->create_editing = false;
      s->needs_redraw = true;
      return;
    }

    int ly = py + 56;
    int lh = 22;
    int field_x = px + 90;
    int field_w = pw - 110;
    int browse_w = 54;
    int save_fw = field_w - browse_w - 6;
    int save_fx = field_x;

    // Browse button
    int bx = save_fx + save_fw + 6;
    if (mx >= bx && mx <= bx + browse_w && my >= ly && my <= ly + lh) {
      s->fb_panel.reset();
      s->fb_panel.pick_mode = true;
      s->fb_panel.pick_file = false;
      s->fb_panel.active = true;
      s->fb_pending_action = ArchiveState::FbAction::CreateBrowse;
      s->needs_redraw = true;
      return;
    }

    // Name field focus
    ly += 30;
    if (mx >= field_x && mx <= field_x + field_w && my >= ly && my <= ly + lh) {
      s->create_editing = true;
      s->needs_redraw = true;
      return;
    } else {
      s->create_editing = false;
    }

    // Format dropdown selector
    int fmt_fw = 160;
    if (mx >= field_x && mx <= field_x + fmt_fw && my >= ly && my <= ly + lh) {
      s->create_format_open = !s->create_format_open;
      s->needs_redraw = true;
      return;
    }

    // Dropdown item selection
    if (s->create_format_open) {
      int dd_y = ly + lh + 2;
      int dd_item_h = 24;
      if (mx >= field_x && mx <= field_x + fmt_fw &&
          my >= dd_y && my <= dd_y + dd_item_h * kFormatCount) {
        int idx = (my - dd_y) / dd_item_h;
        if (idx >= 0 && idx < kFormatCount) {
          s->create_format = idx;
          s->create_format_open = false;
          s->needs_redraw = true;
          return;
        }
      } else {
        // Click outside dropdown closes it
        s->create_format_open = false;
        s->needs_redraw = true;
      }
    }

    // Action buttons
    int btn_y = py + ph - 40;
    int btn_w = 90;
    int btn_h = 28;
    int create_btn_x = px + pw - btn_w - 16;
    int cancel_btn_x = create_btn_x - btn_w - 8;

    // Cancel
    if (mx >= cancel_btn_x && mx <= cancel_btn_x + btn_w &&
        my >= btn_y && my <= btn_y + btn_h) {
      s->create_mode = false;
      s->create_editing = false;
      s->needs_redraw = true;
      return;
    }

    // Create
    if (mx >= create_btn_x && mx <= create_btn_x + btn_w &&
        my >= btn_y && my <= btn_y + btn_h) {
      if (!s->create_dest_dir.empty() && !s->create_name.empty()) {
        s->creating = true;
        s->progress = 0.0f;
        s->status_text.clear();
        s->needs_redraw = true;
        std::string dest_dir = s->create_dest_dir;
        std::string name = s->create_name;
        std::string ext = kFormatExts[s->create_format];
        std::string out_path = dest_dir + "/" + name + ext;
        std::vector<std::string> files = s->create_files;
        size_t total = files.size();
        std::thread([s, out_path, files, total]() {
          archive_viewer::create_archive(out_path, files, nullptr, &s->progress, total);
          s->creating = false;
          s->create_mode = false;
          s->needs_redraw = true;
        }).detach();
        return;
      }
      s->needs_redraw = true;
      return;
    }

    return;
  }

  // Settings panel UI
  if (s->show_settings) {
    // Close button on settings panel
    int sc_x = s->settings_x + s->settings_w - 28;
    int sc_y = s->settings_y + 6;
    if (mx >= sc_x && mx <= sc_x + 20 &&
        my >= sc_y && my <= sc_y + 20) {
      s->show_settings = false;
      save_settings(s);
      s->needs_redraw = true;
      return;
    }
    // Slider click
    for (int i = 0; i < 5; ++i) {
      int sx = s->slider_track_x[i];
      int sy = s->slider_track_y[i];
      if (mx >= sx && mx <= sx + s->slider_track_w &&
          my >= sy && my <= sy + s->slider_track_h) {
        s->drag_slider = i;
        set_slider_from_x(s, i, mx);
        return;
      }
    }
    // Click outside panel — close
    if (mx < s->settings_x || mx > s->settings_x + s->settings_w ||
        my < s->settings_y || my > s->settings_y + s->settings_h) {
      s->show_settings = false;
      save_settings(s);
      s->needs_redraw = true;
    }
    return;
  }

  // Open button (file picker)
  int open_x = s->width - 100;
  int open_y = (s->header_h - 24) / 2;
  if (mx >= open_x && mx <= open_x + 24 &&
      my >= open_y && my <= open_y + 24) {
    s->fb_panel.reset();
    s->fb_panel.pick_mode = true;
    s->fb_panel.pick_file = true;
    s->fb_panel.active = true;
    s->fb_pending_action = ArchiveState::FbAction::OpenFile;
    s->needs_redraw = true;
    return;
  }

  // Gear button (opens settings)
  int gear_x = s->width - 66;
  int gear_y = (s->header_h - 24) / 2;
  if (mx >= gear_x && mx <= gear_x + 24 &&
      my >= gear_y && my <= gear_y + 24) {
    s->show_settings = true;
    s->needs_redraw = true;
    return;
  }

  // Close button
  int close_x = s->width - 36;
  int close_y = (s->header_h - 24) / 2;
  if (mx >= close_x && mx <= close_x + 24 &&
      my >= close_y && my <= close_y + 24) {
    s->running = false;
    return;
  }

  // Footer buttons (1=ExtractAll, 2=ExtractSel, 3=Close, 4=Open)
  for (int b = 1; b < 5; ++b) {
    if (mx >= s->btn_x[b] && mx <= s->btn_x[b] + s->btn_w[b] &&
        my >= s->btn_y[b] && my <= s->btn_y[b] + s->btn_h[b]) {
      if (b == 4) {
        // Open archive
        s->fb_panel.reset();
        s->fb_panel.pick_mode = true;
        s->fb_panel.pick_file = true;
        s->fb_panel.active = true;
        s->fb_pending_action = ArchiveState::FbAction::OpenFile;
        s->needs_redraw = true;
        return;
      } else if (b == 1) {
        if (!s->archive_path.empty() && !s->extracting) {
          s->fb_panel.reset();
          s->fb_panel.pick_mode = true;
          s->fb_panel.pick_file = false;
          s->fb_panel.active = true;
          s->fb_pending_action = ArchiveState::FbAction::ExtractAll;
          s->needs_redraw = true;
        }
      } else if (b == 2) {
        if (!s->archive_path.empty() && !s->extracting) {
          std::vector<std::string> sel;
          for (auto& ve : s->visible_entries) {
            if (ve.selected) {
              sel.push_back(ve.vpath);
            }
          }
          if (sel.empty()) return;
          s->fb_panel.reset();
          s->fb_panel.pick_mode = true;
          s->fb_panel.pick_file = false;
          s->fb_panel.active = true;
          s->fb_pending_action = ArchiveState::FbAction::ExtractSel;
          s->fb_pending_sel = std::move(sel);
          s->needs_redraw = true;
        }
      } else if (b == 3) {
        s->running = false;
      }
      return;
    }
  }

  // "Open destination" button — opens in embedded panel
  if (s->show_open_dest && !s->last_extract_dest.empty()) {
    if (mx >= s->open_dest_btn_x && mx <= s->open_dest_btn_x + s->open_dest_btn_w &&
        my >= s->open_dest_btn_y && my <= s->open_dest_btn_y + s->open_dest_btn_h) {
      s->show_open_dest = false;
      s->fb_panel.reset();
      s->fb_panel.pick_mode = false;
      s->fb_panel.active = true;
      s->fb_panel.navigate_to(s->last_extract_dest);
      s->needs_redraw = true;
      return;
    }
  }

  // Up button
  if (mx >= s->btn_x[0] && mx <= s->btn_x[0] + s->btn_w[0] &&
      my >= s->btn_y[0] && my <= s->btn_y[0] + s->btn_h[0]) {
    if (!s->current_dir.empty()) {
      auto pos = s->current_dir.rfind('/');
      if (pos != std::string::npos && pos > 0) {
        s->current_dir = s->current_dir.substr(0, pos);
      } else {
        s->current_dir.clear();
      }
      s->rebuild_visible();
      s->scroll_px = 0;
      s->needs_redraw = true;
    }
    return;
  }

  int row_h = static_cast<int>(ArchiveState::row_h * s->zoom_level);
  int list_y = s->header_h + s->info_h + s->breadcrumb_h;
  int list_h = s->height - list_y - s->footer_h;
  if (mx > 0 && mx < s->width - 6 && my >= list_y && my < list_y + list_h) {
    int idx = (my - list_y + s->scroll_px) / row_h;
    if (idx >= 0 && idx < static_cast<int>(s->visible_entries.size())) {
      auto& ve = s->visible_entries[idx];
      int chk_x = 10;
      int chk_y = list_y + idx * row_h - s->scroll_px + (row_h - 16) / 2;
      if (mx >= chk_x && mx <= chk_x + 16 &&
          my >= chk_y && my <= chk_y + 16) {
        ve.selected = !ve.selected;
      } else if (ve.is_dir) {
        s->current_dir = ve.vpath;
        if (s->current_dir.back() != '/') s->current_dir += '/';
        s->rebuild_visible();
        s->scroll_px = 0;
      } else {
        ve.selected = !ve.selected;
      }
      s->needs_redraw = true;
    }
  }
}

} // namespace

// ── ArchiveState ──────────────────────────────────────────────────────

void ArchiveState::rebuild_visible() {
  visible_entries.clear();
  std::string prefix = current_dir;
  if (!prefix.empty() && prefix.back() != '/') prefix += '/';

  for (const auto& e : all_entries) {
    if (!prefix.empty() && e.path.compare(0, prefix.size(), prefix) != 0)
      continue;
    std::string rest = e.path.substr(prefix.size());
    if (rest.empty()) continue;

    auto slash = rest.find('/');
    if (slash != std::string::npos) {
      std::string dir_name = rest.substr(0, slash + 1);
      auto it = std::find_if(visible_entries.begin(), visible_entries.end(),
          [&](const ViewEntry& ve) { return ve.name == dir_name; });
      if (it == visible_entries.end()) {
        ViewEntry ve;
        ve.name = dir_name;
        ve.vpath = prefix + dir_name;
        ve.is_dir = true;
        visible_entries.push_back(ve);
      }
      continue;
    }

    ViewEntry ve;
    ve.name = rest;
    ve.vpath = e.path;
    ve.size = e.size;
    ve.mtime = e.mtime;
    ve.is_dir = e.is_dir;
    visible_entries.push_back(ve);
  }
}

void ArchiveState::open_archive(const std::string& path) {
  alog("open_archive called with: \"%s\"", path.c_str());
  archive_path = path;
  all_entries = archive_viewer::scan_archive(path);
  alog("scan_archive returned %zu entries", all_entries.size());
  current_dir.clear();
  scroll_px = 0;
  scan_error = false;
  scan_error_msg.clear();
  status_text.clear();
  entries_loaded = false;
  if (all_entries.empty()) {
    std::string err = archive_viewer::last_error();
    if (!err.empty()) {
      alog("open_archive: scan returned 0 entries, error=\"%s\"", err.c_str());
      scan_error = true;
      scan_error_msg = err;
    } else {
      alog("open_archive: archive is empty (0 entries, no error)");
      status_text = "Archive is empty";
      entries_loaded = true;
    }
  } else {
    alog("open_archive: loaded %zu entries successfully", all_entries.size());
    entries_loaded = true;
    rebuild_visible();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Opened: %zu entries", all_entries.size());
    status_text = buf;
  }
  if (toplevel) {
    xdg_toplevel_set_title(static_cast<xdg_toplevel*>(toplevel),
                           ("Archive: " + path).c_str());
  }
  needs_redraw = true;
}

// ── Entry point ───────────────────────────────────────────────────────

int run_gui(const std::string& archive_path,
            const std::vector<std::string>& create_files) {
  ArchiveState s;
  ArchiveState* s_ptr = &s;
  s.archive_path = archive_path;
  s.create_files = create_files;
  if (!create_files.empty()) {
    s.create_mode = true;
    s.create_dest_dir = std::filesystem::current_path().string();
    s.create_editing = true; // focus name field immediately
  }
  s.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!s.xkb_ctx) {
    std::fprintf(stderr, "Failed to create xkb context\n");
    return 1;
  }

  // Load icon surfaces from embedded SVGs
  s.icon_close = svg_to_surface(close_svg, 18);
  s.icon_settings = svg_to_surface(settings_svg, 18);

  // Connect to Wayland display
  s.display = wl_display_connect(nullptr);
  if (!s.display) {
    std::fprintf(stderr, "Failed to connect to Wayland display\n");
    return 1;
  }

  s.registry = wl_display_get_registry(s.display);
  wl_registry_add_listener(s.registry, &registry_listener, &s);
  wl_display_roundtrip(s.display);
  wl_display_roundtrip(s.display);

  if (!s.compositor || !s.shm || !s.wm_base) {
    std::fprintf(stderr, "Missing required Wayland globals\n");
    return 1;
  }

  load_settings(&s);

  xdg_wm_base_add_listener(static_cast<xdg_wm_base*>(s.wm_base), &wm_base_listener, nullptr);

  // Create surface
  s.surface = wl_compositor_create_surface(s.compositor);
  s.xdg_surf = xdg_wm_base_get_xdg_surface(static_cast<xdg_wm_base*>(s.wm_base), s.surface);
  xdg_surface_add_listener(static_cast<xdg_surface*>(s.xdg_surf), &xdg_surface_listener, &s);

  s.toplevel = xdg_surface_get_toplevel(static_cast<xdg_surface*>(s.xdg_surf));
  xdg_toplevel_add_listener(static_cast<xdg_toplevel*>(s.toplevel), &toplevel_listener, &s);
  xdg_toplevel_set_title(static_cast<xdg_toplevel*>(s.toplevel),
    s.create_mode ? "Create Archive" : ("Archive: " + archive_path).c_str());

  wl_surface_commit(s.surface);

  // Allocate initial buffers
  allocate_shm_buffer(s.bufs[0], s.shm, s.pending_w, s.pending_h);
  allocate_shm_buffer(s.bufs[1], s.shm, s.pending_w, s.pending_h);
  s.width = s.pending_w;
  s.height = s.pending_h;

  // Load archive entries (skip in create mode)
  if (!s.create_mode) {
    s.all_entries = archive_viewer::scan_archive(archive_path);
    if (s.all_entries.empty() && !archive_viewer::last_error().empty()) {
      s.scan_error = true;
      s.scan_error_msg = archive_viewer::last_error();
    } else {
      s.entries_loaded = true;
      s.rebuild_visible();
    }
  }

  // Main loop
  while (s.running) {
    wl_display_dispatch(s.display);

    // Handle pending panel result
    if (s.fb_pending_action != ArchiveState::FbAction::None && !s.fb_panel.active) {
      auto action = s.fb_pending_action;
      s.fb_pending_action = ArchiveState::FbAction::None;
      std::string result = s.fb_panel.result;
      if (!result.empty()) {
        if (action == ArchiveState::FbAction::OpenFile) {
          s.open_archive(result);
        } else if (action == ArchiveState::FbAction::CreateBrowse) {
          s.create_dest_dir = result;
          if (s.create_name.empty() && !s.create_files.empty()) {
            auto& first = s.create_files[0];
            auto slash = first.rfind('/');
            std::string basename = (slash != std::string::npos) ? first.substr(slash + 1) : first;
            auto dot = basename.rfind('.');
            if (dot != std::string::npos) basename = basename.substr(0, dot);
            s.create_name = basename;
            s.create_cursor = static_cast<int>(s.create_name.size());
          }
        } else if (action == ArchiveState::FbAction::ExtractAll) {
          if (!s.archive_path.empty() && !s.extracting) {
            s.last_extract_dest = result;
            s.show_open_dest = false;
            s.extracting = true;
            s.progress = 0.0f;
            s.status_text.clear();
            size_t total = s.all_entries.size();
            std::thread([s_ptr, result, total]() {
              archive_viewer::extract_all(s_ptr->archive_path, result, &s_ptr->progress, total);
              s_ptr->extracting = false;
              s_ptr->show_open_dest = true;
              s_ptr->status_text = "Extraction complete";
              s_ptr->needs_redraw = true;
            }).detach();
          }
        } else if (action == ArchiveState::FbAction::ExtractSel) {
          if (!s.archive_path.empty() && !s.extracting) {
            auto sel = std::move(s.fb_pending_sel);
            s.last_extract_dest = result;
            s.show_open_dest = false;
            s.extracting = true;
            s.progress = 0.0f;
            s.status_text.clear();
            size_t total = sel.size();
            std::thread([s_ptr, result, sel, total]() {
              archive_viewer::extract_selected(s_ptr->archive_path, result, sel, &s_ptr->progress, total);
              s_ptr->extracting = false;
              s_ptr->show_open_dest = true;
              s_ptr->status_text = "Extraction complete";
              s_ptr->needs_redraw = true;
            }).detach();
          }
        }
      }
      s.needs_redraw = true;
    }

    if (s.needs_redraw && s.configured) {
      int back = 1 - s.front;
      if (s.bufs[back].busy) {
        back = s.front;
        if (s.bufs[back].busy) {
          continue;
        }
      }

      auto& buf = s.bufs[back];
      if (buf.width != s.width || buf.height != s.height) {
        destroy_buffer(buf);
        allocate_shm_buffer(buf, s.shm, s.width, s.height);
      }

      if (buf.cr) {
        s.front = back;
        draw_archive_viewer(s);

        cairo_surface_flush(buf.cairo_surface);

        wl_surface_attach(s.surface, buf.wl_buf, 0, 0);
        wl_surface_damage_buffer(s.surface, 0, 0, s.width, s.height);

        s.frame_cb = wl_surface_frame(s.surface);
        wl_callback_add_listener(s.frame_cb, &frame_listener, &s);

        wl_surface_commit(s.surface);
        buf.busy = true;
      }

      s.needs_redraw = false;
    }
  }

  // Cleanup
  destroy_buffer(s.bufs[0]);
  destroy_buffer(s.bufs[1]);
  if (s.frame_cb) wl_callback_destroy(s.frame_cb);
  if (s.toplevel) xdg_toplevel_destroy(static_cast<xdg_toplevel*>(s.toplevel));
  if (s.xdg_surf) xdg_surface_destroy(static_cast<xdg_surface*>(s.xdg_surf));
  if (s.surface) wl_surface_destroy(s.surface);
  if (s.wm_base) xdg_wm_base_destroy(static_cast<xdg_wm_base*>(s.wm_base));
  if (s.pointer) {
    delete static_cast<PointerState*>(wl_pointer_get_user_data(s.pointer));
    wl_pointer_destroy(s.pointer);
  }
  if (s.keyboard) wl_keyboard_destroy(s.keyboard);
  if (s.seat) wl_seat_destroy(s.seat);
  if (s.shm) wl_shm_destroy(s.shm);
  if (s.compositor) wl_compositor_destroy(s.compositor);
  if (s.registry) wl_registry_destroy(s.registry);
  wl_display_disconnect(s.display);
  if (s.icon_close) cairo_surface_destroy(s.icon_close);
  if (s.icon_settings) cairo_surface_destroy(s.icon_settings);
  if (s.keymap) xkb_keymap_unref(s.keymap);
  if (s.xkb) xkb_state_unref(s.xkb);
  if (s.xkb_ctx) xkb_context_unref(s.xkb_ctx);

  return 0;
}

} // namespace archive_viewer
