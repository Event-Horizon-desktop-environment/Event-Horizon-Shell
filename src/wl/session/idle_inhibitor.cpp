#include "wl/session/idle_inhibitor.hpp"

namespace eh::wayland {

IdleInhibitor::~IdleInhibitor() { cleanup(); }

void IdleInhibitor::cleanup() {
   
  if (inhibitor_) {
    zwp_idle_inhibitor_v1_destroy(inhibitor_);
    inhibitor_ = nullptr;
  }
  manager_ = nullptr;
}

void IdleInhibitor::set_inhibited(wl_surface* surface, bool inhibited) {
   
  if (!inhibited) {
    if (inhibitor_) {
      zwp_idle_inhibitor_v1_destroy(inhibitor_);
      inhibitor_ = nullptr;
    }
    return;
  }

  if (inhibitor_) return;
  if (!manager_ || !surface) return;
  inhibitor_ = zwp_idle_inhibit_manager_v1_create_inhibitor(manager_, surface);
}

}
