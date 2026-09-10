#pragma once

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-client-protocol.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace eh::wayland {
class ShmBuffer;
} // namespace eh::wayland

namespace eh::dialog {

/// Base class for native Wayland dialogs.
class DialogBase {
public:
  struct Result {
    uint32_t response = 1; // 1 = failed/cancelled
    std::vector<std::string> uris;
  };

  DialogBase(const DialogBase&) = delete;
  DialogBase& operator=(const DialogBase&) = delete;
  DialogBase(DialogBase&&) = delete;
  DialogBase& operator=(DialogBase&&) = delete;
  virtual ~DialogBase();

  /// Show the dialog. Blocks until the user responds.
  /// Must be called from any thread; creates its own Wayland event loop.
  Result run();

protected:
  DialogBase(int default_w, int default_h,
             const char* title);

  // ── subclass overrides ──────────────────────────────────────

  /// Draw the dialog content. Called once per frame.
  virtual void draw(cairo_t* cr, int w, int h) = 0;

  /// Pointer button press (local surface coordinates).
  virtual void on_click(int x, int y, int button) { (void)x; (void)y; (void)button; }

  /// Pointer motion (local surface coordinates).
  virtual void on_pointer_move(int x, int y) { (void)x; (void)y; }

  /// Scroll (positive dy = scroll down).
  virtual void on_scroll(int x, int y, double dx, double dy) {
     
    (void)x; (void)y; (void)dx; (void)dy;
  }

  /// Keyboard key press/release.
  virtual void on_key(xkb_keysym_t sym, uint32_t state, const char* utf8, int utf8_len) {
     
    (void)sym; (void)state; (void)utf8; (void)utf8_len;
  }

  // ── helpers ─────────────────────────────────────────────────

  /// Close the dialog with a result.
  void finish(Result res);

  /// Trigger a redraw on the next frame.
  void request_redraw();

  int width() const noexcept { return width_; }
  int height() const noexcept { return height_; }

  /// Draw a pill/rounded-rect button. Returns true if (x,y) is inside.
  bool draw_button(cairo_t* cr, int x, int y, int w, int h,
                   const char* label, bool hovered, bool pressed,
                   double r, double g, double b);

  /// Draw a text label centered at (x,y) with optional color.
  void draw_text(cairo_t* cr, const char* text, int x, int y,
                 double r, double g, double b, double font_size);

  /// Draw left-aligned text with (x,y) as top-left.
  void draw_text_left(cairo_t* cr, const char* text, int x, int y,
                       double r, double g, double b, double font_size);

private:
  // Wayland event-loop thread
  void thread_main_();

  // xdg_surface / xdg_toplevel listeners
  static void on_xdg_configure_(void* data, xdg_surface* xdg, uint32_t serial);
  static void on_toplevel_configure_(void* data, xdg_toplevel*, int32_t w, int32_t h, wl_array*);
  static void on_toplevel_close_(void* data, xdg_toplevel*);
  static constexpr xdg_surface_listener kXdgListener_{.configure = on_xdg_configure_};
  static constexpr xdg_toplevel_listener kTopListener_{
    .configure = on_toplevel_configure_,
    .close = on_toplevel_close_,
    .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t){},
    .wm_capabilities = [](void*, xdg_toplevel*, wl_array*){}
  };

  // wl_buffer release callback
  static void on_buf_release_(void* data);

  int default_w_ = 0;
  int default_h_ = 0;
  std::string title_;
  std::atomic<bool> running_{false};

  // Dialog's own Wayland connection objects
  wl_display* dpy_ = nullptr;
  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  xdg_wm_base* xdg_base_ = nullptr;
  wl_seat* seat_ = nullptr;

  wl_surface* surface_ = nullptr;
  xdg_surface* xdg_surface_ = nullptr;
  xdg_toplevel* xdg_toplevel_ = nullptr;
  int width_ = 640;
  int height_ = 480;
  bool configured_ = false;
  bool redraw_pending_ = true;

  // Double-buffered SHM buffers
  std::array<std::unique_ptr<eh::wayland::ShmBuffer>, 2> bufs_{};
  int front_ = 0;

  // Thread
  std::thread thread_;
  Result result_;
};

} // namespace eh::dialog
