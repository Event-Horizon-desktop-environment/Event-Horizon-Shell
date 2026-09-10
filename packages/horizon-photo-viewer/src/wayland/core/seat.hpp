#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

struct xkb_state;
struct xkb_keymap;

namespace hpv {

struct KeyEvent {
    xkb_keysym_t sym = XKB_KEY_NoSymbol;
    uint32_t keycode = 0;
    uint32_t state = 0;   // WL_KEYBOARD_KEY_STATE_PRESSED or RELEASED
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool super = false;
    char utf8[8] = {};
    int utf8_len = 0;
};

struct ScrollEvent {
    uint32_t time = 0;
    double delta_x = 0;
    double delta_y = 0;
    int32_t discrete_x = 0;
    int32_t discrete_y = 0;
};

struct PointerEvent {
    uint32_t time = 0;
    int x = 0;          // surface-local coordinates
    int y = 0;
    uint32_t button = 0; // Linux button code (BTN_LEFT = 0x110)
    uint32_t state = 0;  // WL_POINTER_BUTTON_STATE_PRESSED or RELEASED
    bool entered = false; // true if cursor is inside the surface
};

class Seat {
public:
    Seat() = default;
    ~Seat();

    void init(wl_seat* seat, wl_display* display);
    void set_key_callback(std::function<void(const KeyEvent&)> cb) {
        key_cb_ = std::move(cb);
    }
    void set_pointer_callback(std::function<void(const PointerEvent&)> cb) {
        pointer_cb_ = std::move(cb);
    }
    void set_scroll_callback(std::function<void(const ScrollEvent&)> cb) {
        scroll_cb_ = std::move(cb);
    }
    void set_motion_callback(std::function<void(int x, int y)> cb) {
        motion_cb_ = std::move(cb);
    }

    const PointerEvent& pointer_state() const { return pointer_state_; }

private:
    // Keyboard handlers
    static void handle_keymap(void* data, wl_keyboard* keyboard, uint32_t format,
                              int32_t fd, uint32_t size);
    static void handle_key_enter(void* data, wl_keyboard* keyboard, uint32_t serial,
                                 wl_surface* surface, wl_array* keys);
    static void handle_key_leave(void* data, wl_keyboard* keyboard, uint32_t serial,
                                 wl_surface* surface);
    static void handle_key(void* data, wl_keyboard* keyboard, uint32_t serial,
                           uint32_t time, uint32_t key, uint32_t state);
    static void handle_modifiers(void* data, wl_keyboard* keyboard, uint32_t serial,
                                 uint32_t mods_depressed, uint32_t mods_latched,
                                 uint32_t mods_locked, uint32_t group);
    static void handle_repeat_info(void* data, wl_keyboard* keyboard,
                                   int32_t rate, int32_t delay);

    static constexpr wl_keyboard_listener keyboard_listener_ = {
        .keymap = handle_keymap,
        .enter = handle_key_enter,
        .leave = handle_key_leave,
        .key = handle_key,
        .modifiers = handle_modifiers,
        .repeat_info = handle_repeat_info,
    };

    // Pointer handlers
    static void handle_pointer_enter(void* data, wl_pointer* pointer,
                                     uint32_t serial, wl_surface* surface,
                                     wl_fixed_t sx, wl_fixed_t sy);
    static void handle_pointer_leave(void* data, wl_pointer* pointer,
                                     uint32_t serial, wl_surface* surface);
    static void handle_pointer_motion(void* data, wl_pointer* pointer,
                                      uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
    static void handle_pointer_button(void* data, wl_pointer* pointer,
                                      uint32_t serial, uint32_t time,
                                      uint32_t button, uint32_t state);
    static void handle_pointer_axis(void* data, wl_pointer* pointer,
                                    uint32_t time, uint32_t axis,
                                    wl_fixed_t value);
    static void handle_pointer_frame(void* data, wl_pointer* pointer);

    // Stubs for newer pointer events
    static void handle_pointer_axis_source(void* data, wl_pointer* pointer,
                                           uint32_t axis_source);
    static void handle_pointer_axis_stop(void* data, wl_pointer* pointer,
                                         uint32_t time, uint32_t axis);
    static void handle_pointer_axis_discrete(void* data, wl_pointer* pointer,
                                             uint32_t axis, int32_t discrete);
    static void handle_pointer_axis_value120(void* data, wl_pointer* pointer,
                                             uint32_t axis, int32_t value120);
    static void handle_pointer_axis_relative_direction(void* data, wl_pointer* pointer,
                                                       uint32_t axis, uint32_t direction);

    static constexpr wl_pointer_listener pointer_listener_ = {
        .enter = handle_pointer_enter,
        .leave = handle_pointer_leave,
        .motion = handle_pointer_motion,
        .button = handle_pointer_button,
        .axis = handle_pointer_axis,
        .frame = handle_pointer_frame,
        .axis_source = handle_pointer_axis_source,
        .axis_stop = handle_pointer_axis_stop,
        .axis_discrete = handle_pointer_axis_discrete,
        .axis_value120 = handle_pointer_axis_value120,
        .axis_relative_direction = handle_pointer_axis_relative_direction,
    };

    wl_seat* seat_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_pointer* pointer_ = nullptr;
    xkb_keymap* xkb_keymap_ = nullptr;
    xkb_state* xkb_state_ = nullptr;
    xkb_context* xkb_context_ = nullptr;
    wl_display* display_ = nullptr;
    std::function<void(const KeyEvent&)> key_cb_;
    std::function<void(const PointerEvent&)> pointer_cb_;
    std::function<void(const ScrollEvent&)> scroll_cb_;
    std::function<void(int x, int y)> motion_cb_;
    PointerEvent pointer_state_;
    ScrollEvent scroll_state_;
};

}
