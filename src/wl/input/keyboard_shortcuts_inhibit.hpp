#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <functional>

struct wl_surface;
struct wl_seat;

namespace eh::wayland {

class KeyboardShortcutsInhibit {
public:
  using StateCallback = std::function<void(bool active)>;

  KeyboardShortcutsInhibit() = default;
  KeyboardShortcutsInhibit(const KeyboardShortcutsInhibit&) = delete;
  KeyboardShortcutsInhibit& operator=(const KeyboardShortcutsInhibit&) = delete;
  KeyboardShortcutsInhibit(KeyboardShortcutsInhibit&& other) noexcept;
  KeyboardShortcutsInhibit& operator=(KeyboardShortcutsInhibit&& other) noexcept;
  ~KeyboardShortcutsInhibit();

  void bind(zwp_keyboard_shortcuts_inhibit_manager_v1* manager) { manager_ = manager; }

  bool inhibit(wl_surface* surface, wl_seat* seat);
  void uninhibit();
  void cleanup();

  void set_state_callback(StateCallback cb) { stateCb_ = std::move(cb); }

  explicit operator bool() const { return inhibitor_ != nullptr; }

private:
  static void active_tramp(void* data, zwp_keyboard_shortcuts_inhibitor_v1*);
  static void inactive_tramp(void* data, zwp_keyboard_shortcuts_inhibitor_v1*);

  zwp_keyboard_shortcuts_inhibit_manager_v1* manager_ = nullptr;
  zwp_keyboard_shortcuts_inhibitor_v1* inhibitor_ = nullptr;
  StateCallback stateCb_;

  static constexpr zwp_keyboard_shortcuts_inhibitor_v1_listener kListener_{
    .active = active_tramp,
    .inactive = inactive_tramp,
  };
};

} // namespace eh::wayland
