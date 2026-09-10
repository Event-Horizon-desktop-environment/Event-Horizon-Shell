#pragma once

#include "desktop_shell/Overview/overview_painter.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon.h>

struct wl_array;
struct wl_keyboard;
struct wl_pointer;
struct wl_seat;
struct wl_surface;

namespace eh::shell::overview {

class Host;

class Input {
public:
  explicit Input(Host& host);
  ~Input();

  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  void destroy_seat();
  void setup_seat(wl_seat* seat);

  void handle_pointer_enter(wl_surface* surface, double sx, double sy);
  void handle_pointer_leave();
  void handle_pointer_motion(double sx, double sy);
  void handle_pointer_button(uint32_t button, uint32_t state);
  void handle_axis_discrete(uint32_t axis, int32_t discrete);
  void handle_axis(double value);
  void handle_axis_stop(uint32_t axis);
  void handle_key(uint32_t key, uint32_t state);
  void update_hover_from_pointer();

  void on_seat_capabilities(wl_seat* seat, uint32_t capabilities);
  void on_keymap(uint32_t format, int32_t fd, uint32_t size);
  void on_keyboard_modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group);

  bool pointer_focus() const noexcept { return pointer_focus_; }
  wl_surface* pointer_focus_surface() const noexcept { return pointer_focus_surface_; }
  double ptr_x() const noexcept { return ptr_x_; }
  double ptr_y() const noexcept { return ptr_y_; }

  int hovered_ws = -1;
  int hovered_win = -1;
  int hovered_qs = -1;
  bool hovered_search = false;
  bool close_hovered = false;

  bool button_pressed = false;
  double press_x = 0.0;
  double press_y = 0.0;
  int press_ws = -1;
  int press_win = -1;
  int press_app_idx = -1;
  bool drag_active = false;
  int drop_target_ws = -1;
  int drop_target_win = -1;
  // True while a dragged window hovers the strip's add-workspace slot.
  bool drop_target_add = false;
  int drag_auto_scroll_dir = 0;
  double drag_grab_dx = 0.0;
  double drag_grab_dy = 0.0;
  uint32_t axis_source_ = 0;

  int hovered_app = -1;
  float app_hover_lift = 0.f;
  uint64_t trackpad_settle_until_ms_ = 0;

  bool owns_surface(const wl_surface* s) const noexcept;

  bool needs_frame() const;

private:
  Host& host_;

  wl_seat* seat_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  wl_keyboard* keyboard_ = nullptr;

  xkb_context* xkbCtx_ = nullptr;
  xkb_keymap* xkbKeymap_ = nullptr;
  xkb_state* xkbState_ = nullptr;

  bool pointer_focus_ = false;
  wl_surface* pointer_focus_surface_ = nullptr;
  double ptr_x_ = 0.0;
  double ptr_y_ = 0.0;
};

} // namespace eh::shell::overview
