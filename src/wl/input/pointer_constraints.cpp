#include "wl/input/pointer_constraints.hpp"

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <wayland-client-protocol.h>

namespace eh::wayland {

// RelativePointer.

RelativePointer::~RelativePointer() { cleanup(); }

RelativePointer::RelativePointer(RelativePointer&& other) noexcept
    : relPtr_(std::exchange(other.relPtr_, nullptr)),
      motionCb_(std::move(other.motionCb_)) {}

RelativePointer& RelativePointer::operator=(RelativePointer&& other) noexcept {
  if (this != &other) {
    cleanup();
    relPtr_ = std::exchange(other.relPtr_, nullptr);
    motionCb_ = std::move(other.motionCb_);
  }
  return *this;
}

bool RelativePointer::bind(zwp_relative_pointer_manager_v1* manager, wl_pointer* pointer) {
  if (!manager || !pointer) return false;
  cleanup();
  relPtr_ = zwp_relative_pointer_manager_v1_get_relative_pointer(manager, pointer);
  if (!relPtr_) return false;
  zwp_relative_pointer_v1_add_listener(relPtr_, &kListener_, this);
  return true;
}

void RelativePointer::cleanup() {
  if (relPtr_) {
    zwp_relative_pointer_v1_destroy(relPtr_);
    relPtr_ = nullptr;
  }
  motionCb_ = nullptr;
}

void RelativePointer::motion_tramp(void* data, zwp_relative_pointer_v1* /*ptr*/,
                                    uint32_t utime_hi, uint32_t utime_lo,
                                    wl_fixed_t dx, wl_fixed_t dy,
                                    wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
  auto* self = static_cast<RelativePointer*>(data);
  if (self->motionCb_) {
    const uint64_t time_us = (static_cast<uint64_t>(utime_hi) << 32) | utime_lo;
    self->motionCb_(time_us,
                    wl_fixed_to_double(dx), wl_fixed_to_double(dy),
                    wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel));
  }
}

// PointerLock.

PointerLock::~PointerLock() { cleanup(); }

PointerLock::PointerLock(PointerLock&& other) noexcept
    : lockedPtr_(std::exchange(other.lockedPtr_, nullptr)),
      stateCb_(std::move(other.stateCb_)) {}

PointerLock& PointerLock::operator=(PointerLock&& other) noexcept {
  if (this != &other) {
    cleanup();
    lockedPtr_ = std::exchange(other.lockedPtr_, nullptr);
    stateCb_ = std::move(other.stateCb_);
  }
  return *this;
}

bool PointerLock::lock(zwp_pointer_constraints_v1* constraints, wl_surface* surface,
                        wl_pointer* pointer, wl_region* region, uint32_t lifetime) {
  if (!constraints || !surface || !pointer) return false;
  cleanup();
  lockedPtr_ = zwp_pointer_constraints_v1_lock_pointer(constraints, surface, pointer, region, lifetime);
  if (!lockedPtr_) return false;
  zwp_locked_pointer_v1_add_listener(lockedPtr_, &kListener_, this);
  return true;
}

void PointerLock::unlock() {
  if (lockedPtr_) {
    zwp_locked_pointer_v1_destroy(lockedPtr_);
    lockedPtr_ = nullptr;
  }
}

void PointerLock::cleanup() {
  unlock();
  stateCb_ = nullptr;
}

void PointerLock::locked_tramp(void* data, zwp_locked_pointer_v1* /*lp*/) {
  auto* self = static_cast<PointerLock*>(data);
  if (self->stateCb_) self->stateCb_(true);
}

void PointerLock::unlocked_tramp(void* data, zwp_locked_pointer_v1* /*lp*/) {
  auto* self = static_cast<PointerLock*>(data);
  if (self->stateCb_) self->stateCb_(false);
}

// PointerConfine.

PointerConfine::~PointerConfine() { cleanup(); }

PointerConfine::PointerConfine(PointerConfine&& other) noexcept
    : confinedPtr_(std::exchange(other.confinedPtr_, nullptr)),
      stateCb_(std::move(other.stateCb_)) {}

PointerConfine& PointerConfine::operator=(PointerConfine&& other) noexcept {
  if (this != &other) {
    cleanup();
    confinedPtr_ = std::exchange(other.confinedPtr_, nullptr);
    stateCb_ = std::move(other.stateCb_);
  }
  return *this;
}

bool PointerConfine::confine(zwp_pointer_constraints_v1* constraints, wl_surface* surface,
                              wl_pointer* pointer, wl_region* region, uint32_t lifetime) {
  if (!constraints || !surface || !pointer) return false;
  cleanup();
  confinedPtr_ = zwp_pointer_constraints_v1_confine_pointer(constraints, surface, pointer, region, lifetime);
  if (!confinedPtr_) return false;
  zwp_confined_pointer_v1_add_listener(confinedPtr_, &kListener_, this);
  return true;
}

void PointerConfine::unconfine() {
  if (confinedPtr_) {
    zwp_confined_pointer_v1_destroy(confinedPtr_);
    confinedPtr_ = nullptr;
  }
}

void PointerConfine::cleanup() {
  unconfine();
  stateCb_ = nullptr;
}

void PointerConfine::confined_tramp(void* data, zwp_confined_pointer_v1* /*cp*/) {
  auto* self = static_cast<PointerConfine*>(data);
  if (self->stateCb_) self->stateCb_(true);
}

void PointerConfine::unconfined_tramp(void* data, zwp_confined_pointer_v1* /*cp*/) {
  auto* self = static_cast<PointerConfine*>(data);
  if (self->stateCb_) self->stateCb_(false);
}

} // namespace eh::wayland
