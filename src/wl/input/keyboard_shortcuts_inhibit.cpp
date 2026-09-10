#include "wl/input/keyboard_shortcuts_inhibit.hpp"

#include <utility>

namespace eh::wayland {

KeyboardShortcutsInhibit::KeyboardShortcutsInhibit(KeyboardShortcutsInhibit&& other) noexcept
    : manager_(std::exchange(other.manager_, nullptr)),
      inhibitor_(std::exchange(other.inhibitor_, nullptr)),
      stateCb_(std::move(other.stateCb_)) {}

KeyboardShortcutsInhibit& KeyboardShortcutsInhibit::operator=(KeyboardShortcutsInhibit&& other) noexcept {
  if (this != &other) {
    cleanup();
    manager_ = std::exchange(other.manager_, nullptr);
    inhibitor_ = std::exchange(other.inhibitor_, nullptr);
    stateCb_ = std::move(other.stateCb_);
  }
  return *this;
}

KeyboardShortcutsInhibit::~KeyboardShortcutsInhibit() { cleanup(); }

bool KeyboardShortcutsInhibit::inhibit(wl_surface* surface, wl_seat* seat) {
  if (!manager_ || !surface || !seat) return false;
  if (inhibitor_) return true;
  inhibitor_ = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(manager_, surface, seat);
  if (!inhibitor_) return false;
  zwp_keyboard_shortcuts_inhibitor_v1_add_listener(inhibitor_, &kListener_, this);
  return true;
}

void KeyboardShortcutsInhibit::uninhibit() {
  if (inhibitor_) {
    zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor_);
    inhibitor_ = nullptr;
  }
}

void KeyboardShortcutsInhibit::cleanup() {
  uninhibit();
  manager_ = nullptr;
  stateCb_ = nullptr;
}

void KeyboardShortcutsInhibit::active_tramp(void* data, zwp_keyboard_shortcuts_inhibitor_v1*) {
  auto* self = static_cast<KeyboardShortcutsInhibit*>(data);
  if (self->stateCb_) self->stateCb_(true);
}

void KeyboardShortcutsInhibit::inactive_tramp(void* data, zwp_keyboard_shortcuts_inhibitor_v1*) {
  auto* self = static_cast<KeyboardShortcutsInhibit*>(data);
  if (self->stateCb_) self->stateCb_(false);
}

} // namespace eh::wayland
