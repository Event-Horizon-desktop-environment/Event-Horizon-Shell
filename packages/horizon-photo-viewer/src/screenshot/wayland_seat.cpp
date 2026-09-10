#include "screenshot/wayland_seat.hpp"
#include "core/screenshot/logging.hpp"

#include <algorithm>

namespace hpv::sc {

WaylandSeat::~WaylandSeat() { unbind(); }

void WaylandSeat::bind(wl_seat* seat) {
  SC_LOG("WaylandSeat::bind seat=%p", (void*)seat);
  if (!seat || seat_ == seat) return;
  unbind();
  seat_ = seat;
  wl_seat_add_listener(seat_, &kSeatListener_, this);
  if (!pointer_) {
    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) wl_pointer_add_listener(pointer_, &kPointerListener_, this);
  }
}

void WaylandSeat::unbind() {
  SC_LOG("WaylandSeat::unbind");
  if (pointer_) {
    wl_pointer_destroy(pointer_);
    pointer_ = nullptr;
  }
  ptrFocusSurface_ = nullptr;
  seat_ = nullptr;
}

void WaylandSeat::seat_caps(void* data, wl_seat* seat, uint32_t caps) {
  SC_LOG("WaylandSeat::seat_caps caps=0x%x", caps);
  auto& self = *static_cast<WaylandSeat*>(data);
  const bool hasPtr = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
  if (hasPtr && !self.pointer_) {
    self.pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(self.pointer_, &kPointerListener_, &self);
  } else if (!hasPtr && self.pointer_) {
    wl_pointer_destroy(self.pointer_);
    self.pointer_ = nullptr;
  }
}

void WaylandSeat::seat_name(void*, wl_seat*, const char*) {}

void WaylandSeat::ptr_enter(void* data, wl_pointer*, uint32_t, wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
  SC_LOG("WaylandSeat::ptr_enter surface=%p sx=%f sy=%f", (void*)surface, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
  auto& self = *static_cast<WaylandSeat*>(data);
  self.ptrFocusSurface_ = surface;
  if (self.ptrEnterCb_) self.ptrEnterCb_(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

void WaylandSeat::ptr_leave(void* data, wl_pointer*, uint32_t, wl_surface* surface) {
  SC_LOG("WaylandSeat::ptr_leave surface=%p", (void*)surface);
  auto& self = *static_cast<WaylandSeat*>(data);
  if (self.ptrFocusSurface_ == surface) self.ptrFocusSurface_ = nullptr;
  if (self.ptrLeaveCb_) self.ptrLeaveCb_();
}

void WaylandSeat::ptr_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
  SC_LOG("WaylandSeat::ptr_motion sx=%f sy=%f", wl_fixed_to_double(sx), wl_fixed_to_double(sy));
  auto& self = *static_cast<WaylandSeat*>(data);
  if (self.ptrMotionCb_) self.ptrMotionCb_(self.ptrFocusSurface_, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

void WaylandSeat::ptr_button(void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
  SC_LOG("WaylandSeat::ptr_button serial=%u button=%u state=%u", serial, button, state);
  auto& self = *static_cast<WaylandSeat*>(data);
  self.lastPointerButtonSerial_ = serial;
  if (self.ptrButtonCb_) self.ptrButtonCb_(button, state);
}

void WaylandSeat::ptr_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
  SC_LOG("WaylandSeat::ptr_axis axis=%u value=%f", axis, wl_fixed_to_double(value));
  auto& self = *static_cast<WaylandSeat*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || !self.ptrAxisVertCb_) return;
  const double dv = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dv * 20.0));
  self.ptrAxisVertCb_(-deltaPx);
}

}
