#pragma once

#include <wayland-client.h>

#include <cstdint>
#include <functional>

namespace hpv::sc {

class WaylandSeat {
public:
  using PointerMotionCb = std::function<void(wl_surface* surface, double x, double y)>;
  using PointerButtonCb = std::function<void(uint32_t button, uint32_t state)>;
  using PointerAxisVerticalCb = std::function<void(double delta_px)>;
  using PointerLeaveCb = std::function<void()>;
  using PointerEnterCb = std::function<void(double x, double y)>;

  WaylandSeat() = default;
  WaylandSeat(const WaylandSeat&) = delete;
  WaylandSeat& operator=(const WaylandSeat&) = delete;
  WaylandSeat(WaylandSeat&&) = delete;
  WaylandSeat& operator=(WaylandSeat&&) = delete;
  ~WaylandSeat();

  void bind(wl_seat* seat);
  void unbind();

  void set_pointer_motion_cb(PointerMotionCb cb) { ptrMotionCb_ = std::move(cb); }
  void set_pointer_button_cb(PointerButtonCb cb) { ptrButtonCb_ = std::move(cb); }
  void set_pointer_axis_vertical_cb(PointerAxisVerticalCb cb) { ptrAxisVertCb_ = std::move(cb); }
  void set_pointer_leave_cb(PointerLeaveCb cb) { ptrLeaveCb_ = std::move(cb); }
  void set_pointer_enter_cb(PointerEnterCb cb) { ptrEnterCb_ = std::move(cb); }

  [[nodiscard]] wl_pointer* pointer() const { return pointer_; }
  [[nodiscard]] wl_surface* pointer_focus_surface() const { return ptrFocusSurface_; }

private:
  static void seat_caps(void* data, wl_seat* seat, uint32_t caps);
  static void seat_name(void* data, wl_seat* seat, const char* name);
  static constexpr wl_seat_listener kSeatListener_ = {.capabilities = seat_caps, .name = seat_name};

  static void ptr_enter(void* data, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t sx, wl_fixed_t sy);
  static void ptr_leave(void* data, wl_pointer*, uint32_t, wl_surface*);
  static void ptr_motion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy);
  static void ptr_button(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state);
  static void ptr_axis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value);
  static void ptr_frame(void*, wl_pointer*) {}
  static void ptr_axis_source(void*, wl_pointer*, uint32_t) {}
  static void ptr_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
  static void ptr_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
  static void ptr_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
  static void ptr_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}
  static constexpr wl_pointer_listener kPointerListener_ = {
      .enter = ptr_enter,
      .leave = ptr_leave,
      .motion = ptr_motion,
      .button = ptr_button,
      .axis = ptr_axis,
      .frame = ptr_frame,
      .axis_source = ptr_axis_source,
      .axis_stop = ptr_axis_stop,
      .axis_discrete = ptr_axis_discrete,
      .axis_value120 = ptr_axis_value120,
      .axis_relative_direction = ptr_axis_relative_direction,
  };

  wl_seat* seat_ = nullptr;
  wl_pointer* pointer_ = nullptr;
  wl_surface* ptrFocusSurface_ = nullptr;
  uint32_t lastPointerButtonSerial_ = 0;

  PointerMotionCb ptrMotionCb_{};
  PointerButtonCb ptrButtonCb_{};
  PointerAxisVerticalCb ptrAxisVertCb_{};
  PointerLeaveCb ptrLeaveCb_{};
  PointerEnterCb ptrEnterCb_{};
};

}
