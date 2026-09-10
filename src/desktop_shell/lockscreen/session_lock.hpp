#pragma once

#include "wl/core/protocols.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/input/pointer_constraints.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct wl_display;
struct wl_compositor;
struct wl_seat;
struct wl_pointer;
struct wl_keyboard;
struct wl_output;
struct wl_array;
struct xkb_state;
struct xkb_context;
struct xkb_keymap;

namespace eh::shell::lockscreen {

class LockScreen {
public:
  LockScreen(wl_display* display, wl_compositor* compositor, wl_shm* shm,
             ext_session_lock_manager_v1* lockMgr, wl_seat* seat,
             xkb_state* xkbState,
             zwp_pointer_constraints_v1* pointerConstraints = nullptr);
  ~LockScreen();

  bool lock();
  void unlock();
  bool test_lock(uint32_t auto_unlock_ms = 12000);
  [[nodiscard]] bool is_active() const { return locked_; }
  [[nodiscard]] wl_surface* surface_at(double x, double y) const;
  [[nodiscard]] wl_surface* any_surface() const;
  void add_output(wl_output* output);
  void remove_output(wl_output* output);
  // Re-sync the output set from the owning connection's current globals
  // (adds new outputs, drops outputs that disappeared).
  void sync_outputs(const std::vector<wl_output*>& outputs);

  void pointer_motion(double sx, double sy);
  void pointer_button(uint32_t button, uint32_t state);
  bool keyboard_key(uint32_t keycode, uint32_t state);

  void on_idle_flush();

  // Protocol callbacks (public, called by static trampolines in .cpp)
  void on_locked();
  void on_finished();
  void on_configure(ext_session_lock_surface_v1* ls, uint32_t serial, int w, int h);

  // Pointer listener trampolines
  static void pointer_enter_tramp(void* data, wl_pointer* ptr, uint32_t serial,
                                   wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy);
  static void pointer_leave_tramp(void* data, wl_pointer* ptr, uint32_t serial,
                                   wl_surface* surface);
  static void pointer_motion_tramp(void* data, wl_pointer* ptr, uint32_t time,
                                    wl_fixed_t sx, wl_fixed_t sy);
  static void pointer_button_tramp(void* data, wl_pointer* ptr, uint32_t serial,
                                    uint32_t time, uint32_t button, uint32_t state);
  static void pointer_axis_tramp(void* data, wl_pointer* ptr, uint32_t time,
                                  uint32_t axis, wl_fixed_t value);
  static void pointer_frame_tramp(void* data, wl_pointer* ptr);
  static void pointer_axis_source_tramp(void* data, wl_pointer* ptr, uint32_t axis_source);
  static void pointer_axis_stop_tramp(void* data, wl_pointer* ptr, uint32_t time, uint32_t axis);
  static void pointer_axis_discrete_tramp(void* data, wl_pointer* ptr, uint32_t axis, int32_t discrete);

  // Keyboard listener trampolines (own connection seat input)
  static void kb_keymap_tramp(void* data, wl_keyboard* kb, uint32_t format, int32_t fd, uint32_t size);
  static void kb_enter_tramp(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* surface, wl_array* keys);
  static void kb_leave_tramp(void* data, wl_keyboard* kb, uint32_t serial, wl_surface* surface);
  static void kb_key_tramp(void* data, wl_keyboard* kb, uint32_t serial, uint32_t time, uint32_t keycode, uint32_t state);
  static void kb_modifiers_tramp(void* data, wl_keyboard* kb, uint32_t serial, uint32_t depressed, uint32_t latched,
                                 uint32_t locked, uint32_t group);
  static void kb_repeat_info_tramp(void* data, wl_keyboard* kb, int32_t rate, int32_t delay);

private:
  struct SurfaceData {
    wl_output* output = nullptr;
    wl_surface* surface = nullptr;
    ext_session_lock_surface_v1* lockSurface = nullptr;
    eh::wayland::ShmBuffer buf{};
    int cfgW = 0, cfgH = 0;
    bool configured = false;
  };

  SurfaceData* find_by_surface(wl_surface* surf);
  SurfaceData* find_by_output(wl_output* output);
  void paint_surface(SurfaceData& sd);
  void paint_all();
  void try_authenticate();
  void confine_to_current_surface();
  void unconfine_pointer();

  wl_display* display_;
  wl_compositor* compositor_;
  wl_shm* shm_;
  ext_session_lock_manager_v1* lockMgr_;
  zwp_pointer_constraints_v1* pointerConstraints_;
  wl_seat* seat_;
  xkb_state* xkb_;

  ext_session_lock_v1* lockSession_ = nullptr;
  bool locked_ = false;

  std::vector<std::unique_ptr<SurfaceData>> surfaces_;
  std::vector<wl_output*> known_outputs_;
  wl_surface* enteredSurface_ = nullptr;

  wl_pointer* pointer_ = nullptr;
  wl_keyboard* keyboard_ = nullptr;
  eh::wayland::PointerConfine pointerConfine_;

  // Own xkb state built from this connection's keymap (null until keymap arrives)
  xkb_context* xkbCtx_ = nullptr;
  xkb_keymap* xkbKeymap_ = nullptr;
  bool owns_xkb_ = false;

  std::string password_;
  std::string status_;
  bool tried_auth_ = false;
  std::atomic<bool> auth_pending_{false};
  std::atomic<bool> auth_ok_{false};
  std::thread auth_thread_;

  // Safe test mode (auto-unlock + easy escape)
  bool test_mode_ = false;
  uint64_t test_unlock_deadline_ms_ = 0;
};

} // namespace eh::shell::lockscreen
