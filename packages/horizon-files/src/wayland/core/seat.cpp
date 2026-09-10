#include "wayland/core/seat.hpp"

#include <algorithm>
#include <sys/mman.h>
#include <unistd.h>

namespace eh::wayland {

WaylandSeat::~WaylandSeat() {
  MANGOWM_DEBUG("WaylandSeat dtor this=%p", (void*)this);
  unbind();
}

void WaylandSeat::ensure_xkb() {
  if (!xkbCtx_) xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

void WaylandSeat::bind(wl_seat* seat) {
    
  if (!seat || seat_ == seat) return;
  unbind();
  seat_ = seat;
  ensure_xkb();
  wl_seat_add_listener(seat_, &kSeatListener_, this);
  // Eagerly create pointer and keyboard instead of waiting for the
  // capabilities event (which may have already been dispatched before
  // the listener was attached).
  if (!pointer_) {
    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) wl_pointer_add_listener(pointer_, &kPointerListener_, this);
  }
  if (!keyboard_) {
    keyboard_ = wl_seat_get_keyboard(seat_);
    if (keyboard_) wl_keyboard_add_listener(keyboard_, &kKeyboardListener_, this);
  }
}

void WaylandSeat::unbind() {
   
  if (keyboard_) {
    wl_keyboard_destroy(keyboard_);
    keyboard_ = nullptr;
  }
  if (pointer_) {
    wl_pointer_destroy(pointer_);
    pointer_ = nullptr;
  }
  ptrFocusSurface_ = nullptr;
  if (seat_) {

    seat_ = nullptr;
  }
  if (xkbState_) {
    xkb_state_unref(xkbState_);
    xkbState_ = nullptr;
  }
  if (xkbKeymap_) {
    xkb_keymap_unref(xkbKeymap_);
    xkbKeymap_ = nullptr;
  }
  if (xkbCtx_) {
    xkb_context_unref(xkbCtx_);
    xkbCtx_ = nullptr;
  }
}

void WaylandSeat::seat_caps(void* data, wl_seat* seat, uint32_t caps) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  const bool hasPtr = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
  const bool hasKbd = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

  if (hasPtr && !self.pointer_) {
    self.pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(self.pointer_, &kPointerListener_, &self);
  } else if (!hasPtr && self.pointer_) {
    wl_pointer_destroy(self.pointer_);
    self.pointer_ = nullptr;
  }

  if (hasKbd && !self.keyboard_) {
    self.keyboard_ = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(self.keyboard_, &kKeyboardListener_, &self);
  } else if (!hasKbd && self.keyboard_) {
    wl_keyboard_destroy(self.keyboard_);
    self.keyboard_ = nullptr;
  }
}

void WaylandSeat::seat_name(void* data, wl_seat*  , const char*  ) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  (void)self;
}

void WaylandSeat::ptr_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  self.ptrFocusSurface_ = surface;
  if (!self.ptrEnterCb_) return;
  self.ptrEnterCb_(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

void WaylandSeat::ptr_leave(void* data, wl_pointer*, uint32_t, wl_surface* surface) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (self.ptrFocusSurface_ == surface) self.ptrFocusSurface_ = nullptr;
  if (!self.ptrLeaveCb_) return;
  self.ptrLeaveCb_();
}

void WaylandSeat::ptr_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (!self.ptrMotionCb_) return;
  self.ptrMotionCb_(self.ptrFocusSurface_, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

void WaylandSeat::ptr_button(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  self.lastPointerButtonSerial_ = serial;
  if (!self.ptrButtonCb_) return;
  self.ptrButtonCb_(button, state);
}

void WaylandSeat::ptr_axis(void* data, wl_pointer*, uint32_t  , uint32_t axis, wl_fixed_t value) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !self.ptrAxisVertCb_) return;
  const double dv = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dv * 20.0));
  self.ptrAxisVertCb_(-deltaPx);
}

void WaylandSeat::kb_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !self.xkbCtx_) {
    if (fd >= 0) close(fd);
    return;
  }
  char* map_str = static_cast<char*>(mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;

  if (self.xkbKeymap_) xkb_keymap_unref(self.xkbKeymap_);
  if (self.xkbState_) xkb_state_unref(self.xkbState_);

  const size_t map_len = size > 0 ? static_cast<size_t>(size) - 1 : 0;
  self.xkbKeymap_ = xkb_keymap_new_from_buffer(self.xkbCtx_, map_str, map_len, XKB_KEYMAP_FORMAT_TEXT_V1,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, static_cast<size_t>(size));
  self.xkbState_ = self.xkbKeymap_ ? xkb_state_new(self.xkbKeymap_) : nullptr;
}

void WaylandSeat::kb_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched,
                               uint32_t locked, uint32_t group) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (!self.xkbState_) return;
  const auto gl = static_cast<xkb_layout_index_t>(group);
  xkb_state_update_mask(self.xkbState_, depressed, latched, locked, gl, gl, gl);
}

void WaylandSeat::kb_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t keycode, uint32_t state) {
   
  auto& self = *static_cast<WaylandSeat*>(data);
  if (!self.keyCb_) return;

  KeyboardEvent ev{};
  ev.keycode = keycode;
  ev.state = state;

  if (self.xkbState_) {
    ev.sym = xkb_state_key_get_one_sym(self.xkbState_, keycode + 8);
    ev.utf8_len = xkb_state_key_get_utf8(self.xkbState_, keycode + 8, ev.utf8.data(), ev.utf8.size() - 1);
    if (ev.utf8_len < 0) ev.utf8_len = 0;
    ev.utf8[static_cast<size_t>(ev.utf8_len)] = '\0';
  }

  self.keyCb_(ev);
}

}
