#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <mutex>

struct zwp_idle_inhibit_manager_v1;
struct zwp_idle_inhibitor_v1;
struct wl_surface;

namespace eh::redeye {

class RedeyeService {
 public:
  static RedeyeService& instance();

  RedeyeService(const RedeyeService&) = delete;
  RedeyeService& operator=(const RedeyeService&) = delete;

  void set_manager(zwp_idle_inhibit_manager_v1* mgr);
  void setEnabled(bool on, wl_surface* surface);
  [[nodiscard]] bool isEnabled() const;

 private:
  RedeyeService() = default;
  mutable std::mutex mutex_;
  zwp_idle_inhibit_manager_v1* manager_ = nullptr;
  zwp_idle_inhibitor_v1* inhibitor_ = nullptr;
  wl_surface* surface_ = nullptr;
  bool enabled_ = false;
};

}
