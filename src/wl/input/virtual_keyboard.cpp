#include "wl/input/virtual_keyboard.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <linux/input-event-codes.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <xkbcommon/xkbcommon.h>

namespace eh::wayland {
namespace {

std::uint32_t modifier_mask(xkb_keymap* keymap, const char* name) {
   
  if (!keymap || !name) return 0;
  const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap, name);
  if (index == XKB_MOD_INVALID || index >= 32) return 0;
  return 1u << index;
}

std::uint32_t event_time_ms() {
  using namespace std::chrono;
  return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}

VirtualKeyboardService::~VirtualKeyboardService() { cleanup(); }

bool VirtualKeyboardService::bind(zwp_virtual_keyboard_manager_v1* manager, wl_seat* seat) {
   
  if (!manager || !seat) {
    cleanup();
    return false;
  }
  if (manager_ == manager && seat_ == seat && keyboard_ != nullptr) return true;

  cleanup();
  manager_ = manager;
  seat_ = seat;
  return ensure_keyboard();
}

void VirtualKeyboardService::cleanup() {
   
  if (keyboard_) {
    zwp_virtual_keyboard_v1_destroy(keyboard_);
    keyboard_ = nullptr;
  }
  if (xkbKeymap_) {
    xkb_keymap_unref(xkbKeymap_);
    xkbKeymap_ = nullptr;
  }
  if (xkbContext_) {
    xkb_context_unref(xkbContext_);
    xkbContext_ = nullptr;
  }
  manager_ = nullptr;
  seat_ = nullptr;
  ctrlMask_ = 0;
  shiftMask_ = 0;
  keymapUploaded_ = false;
}

bool VirtualKeyboardService::is_available() const noexcept { return manager_ && seat_; }

bool VirtualKeyboardService::send_paste_shortcut(VirtualPasteShortcut shortcut) {
   
  if (!ensure_keyboard() || !ensure_keymap()) return false;

  switch (shortcut) {
  case VirtualPasteShortcut::CtrlV:
    press_chord(KEY_V, true, false);
    break;
  case VirtualPasteShortcut::CtrlShiftV:
    press_chord(KEY_V, true, true);
    break;
  case VirtualPasteShortcut::ShiftInsert:
    press_chord(KEY_INSERT, false, true);
    break;
  }

  auto* display = wl_proxy_get_display(reinterpret_cast<wl_proxy*>(keyboard_));
  if (display) (void)wl_display_flush(display);
  return true;
}

bool VirtualKeyboardService::ensure_keyboard() {
   
  if (keyboard_) return true;
  if (!manager_ || !seat_) return false;
  keyboard_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager_, seat_);
  return keyboard_ != nullptr;
}

bool VirtualKeyboardService::ensure_keymap() {
   
  if (keymapUploaded_) return true;
  if (!keyboard_) return false;

  if (!xkbContext_) xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkbContext_) return false;

  if (!xkbKeymap_) xkbKeymap_ = xkb_keymap_new_from_names(xkbContext_, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!xkbKeymap_) return false;

  char* keymapString = xkb_keymap_get_as_string(xkbKeymap_, XKB_KEYMAP_FORMAT_TEXT_V1);
  if (!keymapString) return false;

  char path[] = "/tmp/event-horizon-virtual-keyboard-XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    std::free(keymapString);
    return false;
  }
  unlink(path);

  const std::size_t size = std::strlen(keymapString) + 1;
  const bool wroteAll = write(fd, keymapString, size) == static_cast<ssize_t>(size);
  std::free(keymapString);
  if (!wroteAll) {
    close(fd);
    return false;
  }

  zwp_virtual_keyboard_v1_keymap(keyboard_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, static_cast<std::uint32_t>(size));
  close(fd);

  ctrlMask_ = modifier_mask(xkbKeymap_, XKB_MOD_NAME_CTRL);
  shiftMask_ = modifier_mask(xkbKeymap_, XKB_MOD_NAME_SHIFT);
  keymapUploaded_ = true;
  return true;
}

void VirtualKeyboardService::press_chord(std::uint32_t key, bool ctrlPressed, bool shiftPressed) {
   
  if (ctrlPressed) send_key(KEY_LEFTCTRL, true);
  if (shiftPressed) send_key(KEY_LEFTSHIFT, true);
  update_modifiers(ctrlPressed, shiftPressed);

  send_key(key, true);
  send_key(key, false);

  if (shiftPressed) send_key(KEY_LEFTSHIFT, false);
  if (ctrlPressed) send_key(KEY_LEFTCTRL, false);
  update_modifiers(false, false);
}

void VirtualKeyboardService::send_key(std::uint32_t key, bool pressed) {
   
  if (!keyboard_) return;
  zwp_virtual_keyboard_v1_key(keyboard_, event_time_ms(), key,
                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}

void VirtualKeyboardService::update_modifiers(bool ctrlPressed, bool shiftPressed) {
   
  if (!keyboard_) return;
  std::uint32_t depressed = 0;
  if (ctrlPressed) depressed |= ctrlMask_;
  if (shiftPressed) depressed |= shiftMask_;
  zwp_virtual_keyboard_v1_modifiers(keyboard_, depressed, 0, 0, 0);
}

}
