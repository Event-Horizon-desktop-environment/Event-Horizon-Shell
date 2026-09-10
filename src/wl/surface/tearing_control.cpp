#include "wl/surface/tearing_control.hpp"

#include <utility>

namespace eh::wayland {

TearingControl::TearingControl(TearingControl&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)),
      tearingCtrl_(std::exchange(other.tearingCtrl_, nullptr)) {}

TearingControl& TearingControl::operator=(TearingControl&& other) noexcept {
  if (this != &other) {
    cleanup();
    manager_ = std::exchange(other.manager_, nullptr);
    tearingCtrl_ = std::exchange(other.tearingCtrl_, nullptr);
  }
  return *this;
}

TearingControl::~TearingControl() { cleanup(); }

bool TearingControl::create(wl_surface* surface) {
  if (!manager_ || !surface) return false;
  if (tearingCtrl_) return true;
  tearingCtrl_ = wp_tearing_control_manager_v1_get_tearing_control(manager_, surface);
  return tearingCtrl_ != nullptr;
}

void TearingControl::destroy() {
  if (tearingCtrl_) {
    wp_tearing_control_v1_destroy(tearingCtrl_);
    tearingCtrl_ = nullptr;
  }
}

void TearingControl::cleanup() {
  destroy();
  manager_ = nullptr;
}

void TearingControl::set_hint(TearingHint hint) {
  if (tearingCtrl_) {
    wp_tearing_control_v1_set_presentation_hint(tearingCtrl_, static_cast<uint32_t>(hint));
  }
}

} // namespace eh::wayland
