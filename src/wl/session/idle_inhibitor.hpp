#pragma once

#include "wl/core/protocols.hpp"

struct wl_surface;

namespace eh::wayland {

class IdleInhibitor {
public:
  IdleInhibitor() = default;
  IdleInhibitor(const IdleInhibitor&) = delete;
  IdleInhibitor& operator=(const IdleInhibitor&) = delete;
  IdleInhibitor(IdleInhibitor&&) = delete;
  IdleInhibitor& operator=(IdleInhibitor&&) = delete;
  ~IdleInhibitor();

  void bind(zwp_idle_inhibit_manager_v1* manager) { manager_ = manager; }
  void set_inhibited(wl_surface* surface, bool inhibited);
  void cleanup();

private:
  zwp_idle_inhibit_manager_v1* manager_ = nullptr;
  zwp_idle_inhibitor_v1* inhibitor_ = nullptr;
};

}
