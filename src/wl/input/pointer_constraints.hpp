#pragma once

#include "wl/core/protocols.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

struct wl_surface;
struct wl_pointer;
struct wl_region;

namespace eh::wayland {

class WaylandConnection;

// RAII wrapper around zwp_relative_pointer_v1.
// Gives relative motion deltas (dx, dy) for use alongside pointer lock.
class RelativePointer {
public:
  using MotionCallback = std::function<void(uint64_t time_us, double dx, double dy,
                                            double dx_unaccel, double dy_unaccel)>;

  RelativePointer() = default;
  ~RelativePointer();
  RelativePointer(RelativePointer&& other) noexcept;
  RelativePointer& operator=(RelativePointer&& other) noexcept;
  RelativePointer(const RelativePointer&) = delete;
  RelativePointer& operator=(const RelativePointer&) = delete;

  bool bind(zwp_relative_pointer_manager_v1* manager, wl_pointer* pointer);
  void cleanup();

  void set_motion_callback(MotionCallback cb) { motionCb_ = std::move(cb); }

  explicit operator bool() const { return relPtr_ != nullptr; }

private:
  static void motion_tramp(void* data, zwp_relative_pointer_v1* ptr,
                           uint32_t utime_hi, uint32_t utime_lo,
                           wl_fixed_t dx, wl_fixed_t dy,
                           wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel);

  zwp_relative_pointer_v1* relPtr_ = nullptr;
  MotionCallback motionCb_;

  static constexpr zwp_relative_pointer_v1_listener kListener_{
    .relative_motion = motion_tramp,
  };
};

// RAII wrapper around zwp_locked_pointer_v1.
// Locks the pointer to a surface, hiding the cursor and feeding relative
// motion through RelativePointer.
class PointerLock {
public:
  using LockStateCallback = std::function<void(bool locked)>;

  PointerLock() = default;
  ~PointerLock();
  PointerLock(PointerLock&& other) noexcept;
  PointerLock& operator=(PointerLock&& other) noexcept;
  PointerLock(const PointerLock&) = delete;
  PointerLock& operator=(const PointerLock&) = delete;

  bool lock(zwp_pointer_constraints_v1* constraints, wl_surface* surface,
            wl_pointer* pointer, wl_region* region = nullptr,
            uint32_t lifetime = 1);
  void unlock();
  void cleanup();

  void set_state_callback(LockStateCallback cb) { stateCb_ = std::move(cb); }

  explicit operator bool() const { return lockedPtr_ != nullptr; }

private:
  static void locked_tramp(void* data, zwp_locked_pointer_v1* lp);
  static void unlocked_tramp(void* data, zwp_locked_pointer_v1* lp);

  zwp_locked_pointer_v1* lockedPtr_ = nullptr;
  LockStateCallback stateCb_;

  static constexpr zwp_locked_pointer_v1_listener kListener_{
    .locked = locked_tramp,
    .unlocked = unlocked_tramp,
  };
};

// RAII wrapper around zwp_confined_pointer_v1.
// Keeps the pointer inside the surface bounds so it cannot wander off.
class PointerConfine {
public:
  using ConfineStateCallback = std::function<void(bool confined)>;

  PointerConfine() = default;
  ~PointerConfine();
  PointerConfine(PointerConfine&& other) noexcept;
  PointerConfine& operator=(PointerConfine&& other) noexcept;
  PointerConfine(const PointerConfine&) = delete;
  PointerConfine& operator=(const PointerConfine&) = delete;

  bool confine(zwp_pointer_constraints_v1* constraints, wl_surface* surface,
               wl_pointer* pointer, wl_region* region = nullptr,
               uint32_t lifetime = 1);
  void unconfine();
  void cleanup();

  void set_state_callback(ConfineStateCallback cb) { stateCb_ = std::move(cb); }

  explicit operator bool() const { return confinedPtr_ != nullptr; }

private:
  static void confined_tramp(void* data, zwp_confined_pointer_v1* cp);
  static void unconfined_tramp(void* data, zwp_confined_pointer_v1* cp);

  zwp_confined_pointer_v1* confinedPtr_ = nullptr;
  ConfineStateCallback stateCb_;

  static constexpr zwp_confined_pointer_v1_listener kListener_{
    .confined = confined_tramp,
    .unconfined = unconfined_tramp,
  };
};

} // namespace eh::wayland
