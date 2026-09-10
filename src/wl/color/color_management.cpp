#include "wl/color/color_management.hpp"

#include <utility>

namespace eh::wayland {

ColorManager::ColorManager(ColorManager&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)) {}

ColorManager& ColorManager::operator=(ColorManager&& other) noexcept {
  if (this != &other) {
    manager_ = std::exchange(other.manager_, nullptr);
  }
  return *this;
}

ColorManager::~ColorManager() {
  // The manager is a global proxy; WaylandConnection owns its lifetime.
  manager_ = nullptr;
}

} // namespace eh::wayland
