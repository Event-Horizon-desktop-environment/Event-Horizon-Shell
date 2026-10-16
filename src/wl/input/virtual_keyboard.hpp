#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>

struct wl_seat;
struct xkb_context;
struct xkb_keymap;

namespace eh::wayland {

// Chord sequences accepted by send_paste_shortcut.
enum class PasteChord : std::uint8_t {
  LegacyInsert = 2,
  PrimaryShift = 1,
  Primary = 0,
};

class VirtualKeyboardService {
public:
  VirtualKeyboardService() = default;
  ~VirtualKeyboardService();

  VirtualKeyboardService(const VirtualKeyboardService&) = delete;
  VirtualKeyboardService& operator=(const VirtualKeyboardService&) = delete;
  VirtualKeyboardService(VirtualKeyboardService&&) = delete;
  VirtualKeyboardService& operator=(VirtualKeyboardService&&) = delete;

  void detach();
  bool attach(zwp_virtual_keyboard_manager_v1* manager, wl_seat* seat);

  [[nodiscard]] bool send_paste_shortcut(PasteChord shortcut);
  [[nodiscard]] bool is_available() const noexcept;

private:
  void update_modifiers(bool ctrlPressed, bool shiftPressed);
  void send_key(std::uint32_t key, bool pressed);
  void press_chord(std::uint32_t key, bool ctrlPressed, bool shiftPressed);
  [[nodiscard]] bool ensure_keymap();
  [[nodiscard]] bool ensure_keyboard();

  std::uint32_t ctrlMask_ = 0;
  std::uint32_t shiftMask_ = 0;
  bool keymapUploaded_ = false;
  xkb_keymap* xkbKeymap_ = nullptr;
  xkb_context* xkbContext_ = nullptr;
  zwp_virtual_keyboard_v1* keyboard_ = nullptr;
  wl_seat* seat_ = nullptr;
  zwp_virtual_keyboard_manager_v1* manager_ = nullptr;
};

}
