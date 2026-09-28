#include "services/redeye/redeye_service.hpp"

#include <wayland-client-protocol.h>

#include "wl/core/protocols.hpp"

namespace eh::redeye {

RedeyeService& RedeyeService::instance() {
  static RedeyeService svc;
  return svc;
}

void RedeyeService::set_manager(zwp_idle_inhibit_manager_v1* mgr) {
  std::lock_guard<std::mutex> lock(mutex_);
  manager_ = mgr;
  if (!enabled_ || !surface_) {
    if (inhibitor_) {
      zwp_idle_inhibitor_v1_destroy(inhibitor_);
      inhibitor_ = nullptr;
    }
    return;
  }
  if (!inhibitor_ && manager_ && surface_) {
    inhibitor_ = zwp_idle_inhibit_manager_v1_create_inhibitor(manager_, surface_);
  }
}

void RedeyeService::setEnabled(bool on, wl_surface* surface) {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = on;
  if (surface) surface_ = surface;
  if (!on) {
    if (inhibitor_) {
      zwp_idle_inhibitor_v1_destroy(inhibitor_);
      inhibitor_ = nullptr;
    }
    return;
  }
  if (!inhibitor_ && manager_ && surface_) {
    inhibitor_ = zwp_idle_inhibit_manager_v1_create_inhibitor(manager_, surface_);
  }
}

bool RedeyeService::isEnabled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return enabled_;
}

}
