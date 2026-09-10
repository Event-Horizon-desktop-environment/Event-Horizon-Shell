#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>

struct wl_seat;
struct xkb_context;
struct xkb_keymap;

namespace eh::wayland {

enum class VirtualPasteShortcut : std::uint8_t {
  CtrlV = 0,
  CtrlShiftV = 1,
  ShiftInsert = 2,
};

class VirtualKeyboardService {
public:
  VirtualKeyboardService() = default;
  VirtualKeyboardService(const VirtualKeyboardService&) = delete;
  VirtualKeyboardService& operator=(const VirtualKeyboardService&) = delete;
  VirtualKeyboardService(VirtualKeyboardService&&) = delete;
  VirtualKeyboardService& operator=(VirtualKeyboardService&&) = delete;
  ~VirtualKeyboardService();

  bool bind(zwp_virtual_keyboard_manager_v1* manager, wl_seat* seat);
  void cleanup();

  [[nodiscard]] bool is_available() const noexcept;
  [[nodiscard]] bool send_paste_shortcut(VirtualPasteShortcut shortcut);

private:
  [[nodiscard]] bool ensure_keyboard();
  [[nodiscard]] bool ensure_keymap();
  void press_chord(std::uint32_t key, bool ctrlPressed, bool shiftPressed);
  void send_key(std::uint32_t key, bool pressed);
  void update_modifiers(bool ctrlPressed, bool shiftPressed);

  zwp_virtual_keyboard_manager_v1* manager_ = nullptr;
  wl_seat* seat_ = nullptr;
  zwp_virtual_keyboard_v1* keyboard_ = nullptr;

  xkb_context* xkbContext_ = nullptr;
  xkb_keymap* xkbKeymap_ = nullptr;
  std::uint32_t ctrlMask_ = 0;
  std::uint32_t shiftMask_ = 0;
  bool keymapUploaded_ = false;
};

}
