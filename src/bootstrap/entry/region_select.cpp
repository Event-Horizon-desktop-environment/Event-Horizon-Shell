#include "bootstrap/entry/region_select.hpp"
#include "configuration/shell_config.hpp"
#include "wl/core/connection.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <wayland-client.h>
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
}

#include <xkbcommon/xkbcommon.h>

namespace eh::app {
namespace {

struct SelectState {
  wl_display* display = nullptr;
  wl_surface* surface = nullptr;
  zwlr_layer_surface_v1* layer_surface = nullptr;

  int buf_w = 0;
  int buf_h = 0;
  int output_global_x = 0;
  int output_global_y = 0;

  int cur_x = 0;
  int cur_y = 0;
  bool dragging = false;
  bool done = false;
  bool cancelled = false;

  int drag_start_x = 0;
  int drag_start_y = 0;
  int drag_end_x = 0;
  int drag_end_y = 0;

  int result_x = 0;
  int result_y = 0;
  int result_w = 0;
  int result_h = 0;

  bool configured = false;
  bool redraw = false;
  uint32_t* shm_data = nullptr;
  int shm_fd = -1;
  int shm_size = 0;
  wl_buffer* buffer = nullptr;
  wl_pointer* pointer = nullptr;
  wl_keyboard* keyboard = nullptr;
  xkb_context* xkbCtx = nullptr;
  xkb_keymap* xkbKeymap = nullptr;
  xkb_state* xkbState = nullptr;
  uint32_t fill_color = 0x557777FF;
  uint32_t border_color = 0xFFFFFFFF;
};

void layer_surface_configure(void* data, zwlr_layer_surface_v1* ls,
                              uint32_t serial, uint32_t w, uint32_t h) {
  auto* s = static_cast<SelectState*>(data);
  s->buf_w = static_cast<int>(w);
  s->buf_h = static_cast<int>(h);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  s->configured = true;
}

void layer_surface_closed(void* data, zwlr_layer_surface_v1*) {
  auto* s = static_cast<SelectState*>(data);
  s->cancelled = true;
  s->done = true;
}

static constexpr zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

void pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface*,
                   wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<SelectState*>(data);
  s->cur_x = wl_fixed_to_int(sx);
  s->cur_y = wl_fixed_to_int(sy);
}

void pointer_leave(void*, wl_pointer*, uint32_t, wl_surface*) {}

void pointer_motion(void* data, wl_pointer*, uint32_t,
                    wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<SelectState*>(data);
  s->cur_x = wl_fixed_to_int(sx);
  s->cur_y = wl_fixed_to_int(sy);
  if (s->dragging) {
    s->drag_end_x = s->cur_x;
    s->drag_end_y = s->cur_y;
    s->redraw = true;
  }
}

#ifdef EH_HAVE_POINTER_WARP
void pointer_warp(void* data, wl_pointer*, wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<SelectState*>(data);
  s->cur_x = wl_fixed_to_int(sx);
  s->cur_y = wl_fixed_to_int(sy);
  if (s->dragging) {
    s->drag_end_x = s->cur_x;
    s->drag_end_y = s->cur_y;
    s->redraw = true;
  }
}
#endif

void pointer_button(void* data, wl_pointer*, uint32_t, uint32_t,
                    uint32_t button, uint32_t state) {
  auto* s = static_cast<SelectState*>(data);
  if (button == 273) {
    if (state) s->cancelled = true;
    s->done = true;
    return;
  }
  if (button != 272) return;
  if (state) {
    s->dragging = true;
    s->drag_start_x = s->cur_x;
    s->drag_start_y = s->cur_y;
    s->drag_end_x = s->cur_x;
    s->drag_end_y = s->cur_y;
    s->redraw = true;
  } else {
    s->dragging = false;
    int x1 = s->drag_start_x;
    int y1 = s->drag_start_y;
    int x2 = s->drag_end_x;
    int y2 = s->drag_end_y;
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);
    int w = x2 - x1;
    int h = y2 - y1;
    if (w < 2 || h < 2) {
      s->cancelled = true;
      s->done = true;
      return;
    }
    s->result_x = s->output_global_x + x1;
    s->result_y = s->output_global_y + y1;
    s->result_w = w;
    s->result_h = h;
    s->done = true;
  }
}

void pointer_axis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void pointer_frame(void*, wl_pointer*) {}
void pointer_axis_source(void*, wl_pointer*, uint32_t) {}
void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
void pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
void pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
void pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}

static constexpr wl_pointer_listener kPointerListener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
    .axis_relative_direction = pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
    .warp = pointer_warp,
#endif
};

void keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t keycode, uint32_t state) {
  auto* s = static_cast<SelectState*>(data);
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
  if (!s->xkbState) {
    // No xkb keymap yet; treat keycode 1 as Escape (common on evdev).
    if (keycode == 1) { s->cancelled = true; s->done = true; }
    return;
  }
  xkb_keysym_t sym = xkb_state_key_get_one_sym(s->xkbState, keycode + 8);
  if (sym == XKB_KEY_Escape) {
    s->cancelled = true;
    s->done = true;
  }
}

void keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto* s = static_cast<SelectState*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
  char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map_str == MAP_FAILED) { close(fd); return; }
  if (s->xkbCtx && s->xkbKeymap) xkb_keymap_unref(s->xkbKeymap);
  if (s->xkbCtx) {
    s->xkbKeymap = xkb_keymap_new_from_buffer(s->xkbCtx, map_str, size - 1,
                                                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  }
  if (s->xkbState) xkb_state_unref(s->xkbState);
  s->xkbState = s->xkbKeymap ? xkb_state_new(s->xkbKeymap) : nullptr;
  munmap(map_str, size);
  close(fd);
}
void keyboard_enter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void keyboard_leave(void*, wl_keyboard*, uint32_t, wl_surface*) {}
void keyboard_modifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

static constexpr wl_keyboard_listener kKeyboardListener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

void render_frame(SelectState* s) {
  uint32_t dark = 0x44000000;
  // Darken everything outside the dragged rectangle; the border stays highlighted.
  if (s->dragging) {
    int x1 = std::min(s->drag_start_x, s->drag_end_x);
    int y1 = std::min(s->drag_start_y, s->drag_end_y);
    int x2 = std::max(s->drag_start_x, s->drag_end_x);
    int y2 = std::max(s->drag_start_y, s->drag_end_y);
    x1 = std::clamp(x1, 0, s->buf_w - 1);
    y1 = std::clamp(y1, 0, s->buf_h - 1);
    x2 = std::clamp(x2, 0, s->buf_w - 1);
    y2 = std::clamp(y2, 0, s->buf_h - 1);
    for (int y = 0; y < s->buf_h; ++y) {
      for (int x = 0; x < s->buf_w; ++x) {
        if (x >= x1 && x <= x2 && y >= y1 && y <= y2 &&
            x > x1 && x < x2 && y > y1 && y < y2) {
          s->shm_data[y * s->buf_w + x] = 0x00000000;
        } else if (x >= x1 && x <= x2 && y >= y1 && y <= y2 &&
                   (x == x1 || x == x2 || y == y1 || y == y2)) {
          s->shm_data[y * s->buf_w + x] = s->border_color;
        } else {
          s->shm_data[y * s->buf_w + x] = dark;
        }
      }
    }
  } else {
    uint32_t* end = s->shm_data + s->buf_w * s->buf_h;
    for (uint32_t* p = s->shm_data; p < end; ++p) *p = dark;
  }

  wl_surface_attach(s->surface, s->buffer, 0, 0);
  wl_surface_damage_buffer(s->surface, 0, 0, s->buf_w, s->buf_h);
  wl_surface_commit(s->surface);
  wl_display_flush(s->display);
}

} // anonymous namespace
} // namespace eh::app

namespace eh::app {

std::optional<std::string> interactive_region_select(
    wl_display* display,
    wl_compositor* compositor,
    wl_shm* shm,
    zwlr_layer_shell_v1* layer_shell,
    wl_seat* seat,
    const std::vector<eh::wayland::LogicalOutputBounds>& output_bounds,
    wl_output* target_output,
    const std::string&) {

  SelectState s{};
  s.display = display;

  {
    const std::string cache_path = eh::config::state_event_horizon_dir() + "/matugen-palette.cache";
    std::ifstream in(cache_path);
    if (in) {
      std::string ver;
      std::getline(in, ver);
      std::string discard;
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      std::getline(in, discard);
      float ar = 0.4f, ag = 0.6f, ab = 0.9f;
      in >> ar >> ag >> ab;
      auto to_u8 = [](float v) -> uint8_t {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
      };
      s.fill_color = (0x55u << 24) | (to_u8(ar) << 16) | (to_u8(ag) << 8) | to_u8(ab);
    }
  }

  for (const auto& b : output_bounds) {
    if (b.output == target_output) {
      s.output_global_x = b.global_x;
      s.output_global_y = b.global_y;
      break;
    }
  }

  s.surface = wl_compositor_create_surface(compositor);
  if (!s.surface) return std::nullopt;

  s.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell, s.surface, target_output,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "region-select");
  if (!s.layer_surface) {
    wl_surface_destroy(s.surface);
    return std::nullopt;
  }

  zwlr_layer_surface_v1_add_listener(s.layer_surface, &kLayerSurfaceListener, &s);
  zwlr_layer_surface_v1_set_anchor(s.layer_surface,
      ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
      ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(s.layer_surface, -1);
  s.xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  zwlr_layer_surface_v1_set_keyboard_interactivity(s.layer_surface, true);

  // First commit sends no buffer; it exists to elicit a configure event.
  wl_surface_commit(s.surface);
  wl_display_flush(display);
  wl_display_roundtrip(display);

  if (!s.configured || s.buf_w <= 0 || s.buf_h <= 0) {
    zwlr_layer_surface_v1_destroy(s.layer_surface);
    wl_surface_destroy(s.surface);
    return std::nullopt;
  }

  // Allocate the full-size SHM buffer for the overlay.
  s.shm_size = s.buf_w * 4 * s.buf_h;
  s.shm_fd = memfd_create("wl-shm", MFD_CLOEXEC);
  if (s.shm_fd >= 0) {
    if (ftruncate(s.shm_fd, s.shm_size) == 0) {
      s.shm_data = static_cast<uint32_t*>(
          mmap(nullptr, static_cast<std::size_t>(s.shm_size),
               PROT_READ | PROT_WRITE, MAP_SHARED, s.shm_fd, 0));
    }
  }

  if (s.shm_data == MAP_FAILED || !s.shm_data) {
    if (s.shm_fd >= 0) close(s.shm_fd);
    zwlr_layer_surface_v1_destroy(s.layer_surface);
    wl_surface_destroy(s.surface);
    return std::nullopt;
  }
  std::memset(s.shm_data, 0, static_cast<std::size_t>(s.shm_size));

  {
    wl_shm_pool* pool = wl_shm_create_pool(shm, s.shm_fd, s.shm_size);
    s.buffer = wl_shm_pool_create_buffer(pool, 0,
        s.buf_w, s.buf_h, s.buf_w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
  }
  close(s.shm_fd);
  s.shm_fd = -1;

  // Paint the initial dim overlay before any dragging starts.
  render_frame(&s);

  // Take the pointer so we can track the drag.
  s.pointer = wl_seat_get_pointer(seat);
  if (!s.pointer) {
    wl_buffer_destroy(s.buffer);
    munmap(s.shm_data, static_cast<std::size_t>(s.shm_size));
    zwlr_layer_surface_v1_destroy(s.layer_surface);
    wl_surface_destroy(s.surface);
    return std::nullopt;
  }
  wl_pointer_add_listener(s.pointer, &kPointerListener, &s);
  wl_display_flush(display);

  s.keyboard = wl_seat_get_keyboard(seat);
  if (s.keyboard) {
    wl_keyboard_add_listener(s.keyboard, &kKeyboardListener, &s);
    wl_display_flush(display);
  }

  // Pump events until the selection completes.
  struct pollfd pfd;
  pfd.fd = wl_display_get_fd(display);
  pfd.events = POLLIN;

  while (!s.done) {
    wl_display_dispatch_pending(display);
    wl_display_flush(display);
    if (s.done) break;

    if (s.redraw) {
      s.redraw = false;
      render_frame(&s);
    }

    pfd.revents = 0;
    int pr = poll(&pfd, 1, 50);
    if (pr < 0) break;
    if (pr == 0) continue;

    if (wl_display_dispatch(display) < 0) break;
  }

  wl_pointer_destroy(s.pointer);
  if (s.keyboard) wl_keyboard_destroy(s.keyboard);
  if (s.buffer) wl_buffer_destroy(s.buffer);
  if (s.shm_data) munmap(s.shm_data, static_cast<std::size_t>(s.shm_size));
  if (s.shm_fd >= 0) close(s.shm_fd);
  if (s.xkbState) xkb_state_unref(s.xkbState);
  if (s.xkbKeymap) xkb_keymap_unref(s.xkbKeymap);
  if (s.xkbCtx) xkb_context_unref(s.xkbCtx);
  zwlr_layer_surface_v1_destroy(s.layer_surface);
  wl_surface_destroy(s.surface);

  if (s.cancelled) return std::nullopt;

  char result[128];
  std::snprintf(result, sizeof(result), "%dx%d+%d+%d",
                s.result_w, s.result_h, s.result_x, s.result_y);
  return std::string(result);
}

// Cursor position helper: creates transparent fullscreen layer surfaces on
// every output and waits for the first pointer_enter / pointer_motion event.

struct CursorPosState {
  wl_display* display = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wl_pointer* pointer = nullptr;

  int global_x = 0;
  int global_y = 0;
  bool done = false;
  int last_ox = 0;
  int last_oy = 0;
  bool entered = false;

  struct OutputSurf {
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layer_surface = nullptr;
    wl_buffer* buffer = nullptr;
    uint32_t* shm_data = nullptr;
    int shm_size = 0;
    int output_global_x = 0;
    int output_global_y = 0;
    int buf_w = 0;
    int buf_h = 0;
  };
  std::vector<OutputSurf> outputs;

  int configured_count = 0;
  int total_outputs = 0;
};

void ccp_pointer_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface,
                       wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<CursorPosState*>(data);
  for (const auto& os : s->outputs) {
    if (os.surface == surface) {
      s->global_x = os.output_global_x + wl_fixed_to_int(sx);
      s->global_y = os.output_global_y + wl_fixed_to_int(sy);
      s->last_ox = os.output_global_x;
      s->last_oy = os.output_global_y;
      s->entered = true;
      s->done = true;
      return;
    }
  }
}

void ccp_pointer_motion(void* data, wl_pointer*, uint32_t,
                        wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<CursorPosState*>(data);
  // Motion events don't identify a surface, so only trust them once enter has
  // picked which output we're on.
  if (s->entered) {
    s->global_x = s->last_ox + wl_fixed_to_int(sx);
    s->global_y = s->last_oy + wl_fixed_to_int(sy);
    s->done = true;
  }
}

#ifdef EH_HAVE_POINTER_WARP
void ccp_pointer_warp(void* data, wl_pointer*, wl_fixed_t sx, wl_fixed_t sy) {
  auto* s = static_cast<CursorPosState*>(data);
  if (s->entered) {
    s->global_x = s->last_ox + wl_fixed_to_int(sx);
    s->global_y = s->last_oy + wl_fixed_to_int(sy);
    s->done = true;
  }
}
#endif

void ccp_pointer_leave(void*, wl_pointer*, uint32_t, wl_surface*) {}
void ccp_pointer_button(void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void ccp_pointer_axis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {}
void ccp_pointer_frame(void*, wl_pointer*) {}
void ccp_pointer_axis_source(void*, wl_pointer*, uint32_t) {}
void ccp_pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
void ccp_pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
void ccp_pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
void ccp_pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}

static constexpr wl_pointer_listener kCcpPointerListener = {
    .enter = ccp_pointer_enter,
    .leave = ccp_pointer_leave,
    .motion = ccp_pointer_motion,
    .button = ccp_pointer_button,
    .axis = ccp_pointer_axis,
    .frame = ccp_pointer_frame,
    .axis_source = ccp_pointer_axis_source,
    .axis_stop = ccp_pointer_axis_stop,
    .axis_discrete = ccp_pointer_axis_discrete,
    .axis_value120 = ccp_pointer_axis_value120,
    .axis_relative_direction = ccp_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
    .warp = ccp_pointer_warp,
#endif
};

void ccp_layer_surface_configure(void* data, zwlr_layer_surface_v1* ls,
                                  uint32_t serial, uint32_t w, uint32_t h) {
  auto* s = static_cast<CursorPosState*>(data);
  zwlr_layer_surface_v1_ack_configure(ls, serial);
  // Match the configured layer surface back to its output entry and stash the size.
  for (auto& os : s->outputs) {
    if (os.layer_surface == ls) {
      os.buf_w = static_cast<int>(w);
      os.buf_h = static_cast<int>(h);
      break;
    }
  }
  s->configured_count++;
}

void ccp_layer_surface_closed(void*, zwlr_layer_surface_v1*) {}

static constexpr zwlr_layer_surface_v1_listener kCcpLayerSurfaceListener = {
    .configure = ccp_layer_surface_configure,
    .closed = ccp_layer_surface_closed,
};

void ccp_cleanup(CursorPosState& s) {
  if (s.pointer) wl_pointer_destroy(s.pointer);
  for (auto& os : s.outputs) {
    if (os.buffer) wl_buffer_destroy(os.buffer);
    if (os.shm_data) munmap(os.shm_data, static_cast<std::size_t>(os.shm_size));
    if (os.layer_surface) zwlr_layer_surface_v1_destroy(os.layer_surface);
    if (os.surface) wl_surface_destroy(os.surface);
  }
  s.outputs.clear();
}

std::optional<std::pair<int, int>> get_cursor_global_position(
    wl_display* display,
    wl_compositor* compositor,
    wl_shm* shm,
    zwlr_layer_shell_v1* layer_shell,
    wl_seat* seat,
    const std::vector<eh::wayland::LogicalOutputBounds>& output_bounds) {

  // Ask hyprctl first — compositor-specific, but instant and reliable when present.
  if (!output_bounds.empty()) {
    auto pipe_deleter = [](FILE* f) { if (f) pclose(f); };
    std::unique_ptr<FILE, decltype(pipe_deleter)> pipe(
        popen("hyprctl cursorpos 2>/dev/null", "r"), pipe_deleter);
    if (pipe) {
      char buf[64] = {};
      if (fgets(buf, sizeof(buf), pipe.get())) {
        int cx = 0, cy = 0;
        if (std::sscanf(buf, "%d, %d", &cx, &cy) == 2 ||
            std::sscanf(buf, "%d %d", &cx, &cy) == 2) {
          for (const auto& b : output_bounds) {
            if (cx >= b.global_x && cx < b.global_x + b.width &&
                cy >= b.global_y && cy < b.global_y + b.height) {
              return std::make_pair(cx, cy);
            }
          }
          std::cerr << "[cursor] hyprctl cursor at (" << cx << "," << cy << ") not in any output bounds\n";
        }
      }
    }
  }

  if (!display || !compositor || !shm || !layer_shell || !seat || output_bounds.empty())
    return std::nullopt;

  CursorPosState s{};
  s.display = display;
  s.compositor = compositor;
  s.shm = shm;
  s.total_outputs = static_cast<int>(output_bounds.size());

  // Put a fullscreen layer surface on every output.
  for (const auto& bounds : output_bounds) {
    if (!bounds.output) continue;

    wl_surface* surf = wl_compositor_create_surface(compositor);
    if (!surf) { ccp_cleanup(s); return std::nullopt; }

    zwlr_layer_surface_v1* ls = zwlr_layer_shell_v1_get_layer_surface(
        layer_shell, surf, bounds.output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "cursor-pos");
    if (!ls) {
      wl_surface_destroy(surf);
      ccp_cleanup(s);
      return std::nullopt;
    }

    zwlr_layer_surface_v1_add_listener(ls, &kCcpLayerSurfaceListener, &s);
    zwlr_layer_surface_v1_set_anchor(ls,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(ls, -1);

    // Commit without a buffer first so the compositor sends a configure.
    wl_surface_commit(surf);
    wl_display_flush(display);

    CursorPosState::OutputSurf os;
    os.surface = surf;
    os.layer_surface = ls;
    os.output_global_x = bounds.global_x;
    os.output_global_y = bounds.global_y;
    s.outputs.push_back(std::move(os));
  }

  if (s.outputs.empty()) { ccp_cleanup(s); return std::nullopt; }

  // Round-trip so the configure callbacks actually arrive.
  wl_display_roundtrip(display);

  // Allocate a buffer for each output that reported a size.
  for (auto& os : s.outputs) {
    if (os.buf_w <= 0 || os.buf_h <= 0) continue;

    os.shm_size = os.buf_w * 4 * os.buf_h;
    int fd = memfd_create("wl-shm", MFD_CLOEXEC);
    if (fd < 0) continue;
    if (ftruncate(fd, os.shm_size) < 0) { close(fd); continue; }

    os.shm_data = static_cast<uint32_t*>(
        mmap(nullptr, static_cast<std::size_t>(os.shm_size),
             PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (os.shm_data == MAP_FAILED || !os.shm_data) { close(fd); continue; }

    // Zero-fill is fully transparent ARGB, so the capture surfaces stay invisible.
    std::memset(os.shm_data, 0, static_cast<std::size_t>(os.shm_size));

    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, os.shm_size);
    os.buffer = wl_shm_pool_create_buffer(pool, 0,
        os.buf_w, os.buf_h, os.buf_w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(os.surface, os.buffer, 0, 0);
    wl_surface_damage_buffer(os.surface, 0, 0, os.buf_w, os.buf_h);
    wl_surface_commit(os.surface);
  }

  // Make sure the surfaces are actually up before grabbing the pointer — we need the enter.
  wl_display_roundtrip(display);

  // Grab a pointer and start listening for enter/motion.
  s.pointer = wl_seat_get_pointer(seat);
  if (!s.pointer) { ccp_cleanup(s); return std::nullopt; }
  wl_pointer_add_listener(s.pointer, &kCcpPointerListener, &s);
  wl_display_flush(display);

  // Two round-trips push wl_seat.get_pointer through so the enter event lands
  // now instead of waiting on poll.
  wl_display_roundtrip(display);
  wl_display_roundtrip(display);
  if (s.done) {
    const int rx = s.global_x;
    const int ry = s.global_y;
    ccp_cleanup(s);
    return std::make_pair(rx, ry);
  }

  // Spin until the first pointer event or the 2s timeout.
  struct pollfd pfd;
  pfd.fd = wl_display_get_fd(display);
  pfd.events = POLLIN;

  constexpr int kTimeoutMs = 2000;
  int elapsed = 0;

  while (!s.done && elapsed < kTimeoutMs) {
    wl_display_dispatch_pending(display);
    wl_display_flush(display);
    if (s.done) break;

    pfd.revents = 0;
    int pr = poll(&pfd, 1, 50);
    if (pr < 0) break;
    if (pr == 0) { elapsed += 50; continue; }

    if (wl_display_dispatch(display) < 0) break;
  }

  if (!s.done) {
    ccp_cleanup(s);
    return std::nullopt;
  }

  const int result_x = s.global_x;
  const int result_y = s.global_y;
  ccp_cleanup(s);
  return std::make_pair(result_x, result_y);
}

} // namespace eh::app
