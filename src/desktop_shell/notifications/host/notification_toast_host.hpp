#pragma once

#include "configuration/shell_config.hpp"
#include "wl/core/protocols.hpp"
#include "wl/buffer/shm_buffer.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct wl_callback;
struct wl_compositor;
struct wl_output;
struct wl_pointer;
struct wl_seat;
struct wl_shm;
struct zwlr_layer_shell_v1;

namespace eh::shell::notifications {

class NotificationManager;

struct ToastCard {
  std::uint32_t id = 0;
  double x = 0, y = 0, w = 0, h = 0;
};

class NotificationToastHost {
public:
  explicit NotificationToastHost(NotificationManager& manager);
  ~NotificationToastHost();

  NotificationToastHost(const NotificationToastHost&) = delete;
  NotificationToastHost& operator=(const NotificationToastHost&) = delete;
  NotificationToastHost(NotificationToastHost&&) = delete;
  NotificationToastHost& operator=(NotificationToastHost&&) = delete;

  void initialize(wl_compositor* compositor, wl_shm* shm, zwlr_layer_shell_v1* layerShell,
                  wl_seat* seat, wl_output* output);
  void shutdown();
  bool apply_config(const eh::config::ShellNotificationsToastSettings& config);
  void on_notifications_changed();

  wl_surface* surface() const noexcept { return surface_; }

  void pointer_button_press();
  void pointer_button_press(double sx, double sy);
  void pointer_motion(double sx, double sy);

  void layer_configure(uint32_t serial, uint32_t width, uint32_t height);
  void shm_buffer_released();
  void frame_callback_done();
  void destroy_layer();

  // Returns true while any notification has a running expiry countdown, and
  // forces a repaint when the compositor is not delivering frame callbacks
  // (e.g. the toast output is not being presented). Drives the progress bar in
  // real time regardless of surface focus.
  bool tick_animations();

private:
  void create_layer();
  void paint_impl();
  void compute_layout();
  static std::uint32_t anchor_for_position(const std::string& position);
  int hit_test(double sx, double sy) const;

  NotificationManager& mgr_;
  eh::config::ShellNotificationsToastSettings cfg_{};

  wl_compositor* compositor_ = nullptr;
  wl_shm* shm_ = nullptr;
  zwlr_layer_shell_v1* layerShell_ = nullptr;
  wl_seat* seat_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  wl_output* output_ = nullptr;

  wl_surface* surface_ = nullptr;
  zwlr_layer_surface_v1* layer_ = nullptr;
  eh::wayland::ShmBuffer shmBuf_{};

  int configuredWidth_ = 0;
  int configuredHeight_ = 0;
  bool configured_ = false;
  uint64_t layer_gen_ = 0;
  uint64_t configured_gen_ = 0;
  bool wantRepaint_ = false;

  std::vector<ToastCard> cards_;
  int layoutWidth_ = 0;
  int layoutHeight_ = 0;

  wl_callback* frameCallback_ = nullptr;
  std::chrono::steady_clock::time_point lastFrameDone_{};

  double pointerX_ = 0.0;
  double pointerY_ = 0.0;
};

}
