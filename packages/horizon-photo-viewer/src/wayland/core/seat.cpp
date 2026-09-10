#include "wayland/core/seat.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

namespace hpv {

Seat::~Seat() {
    if (pointer_) wl_pointer_destroy(pointer_);
    if (xkb_state_) xkb_state_unref(xkb_state_);
    if (xkb_keymap_) xkb_keymap_unref(xkb_keymap_);
    if (xkb_context_) xkb_context_unref(xkb_context_);
    if (keyboard_) wl_keyboard_destroy(keyboard_);
}

void Seat::init(wl_seat* seat, wl_display* display) {
    seat_ = seat;
    display_ = display;

    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context_) {
        std::cerr << "Failed to create xkb_context\n";
    }

    keyboard_ = wl_seat_get_keyboard(seat_);
    if (keyboard_) {
        wl_keyboard_add_listener(keyboard_, &keyboard_listener_, this);
    } else {
        std::cerr << "No wl_keyboard available\n";
    }

    pointer_ = wl_seat_get_pointer(seat_);
    if (pointer_) {
        wl_pointer_add_listener(pointer_, &pointer_listener_, this);
        std::cout << "seat: pointer available\n";
    }
}

// --- Keyboard handlers ---

void Seat::handle_keymap(void* data, wl_keyboard* /*keyboard*/, uint32_t format,
                          int32_t fd, uint32_t size) {
    auto* self = static_cast<Seat*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ,
                                             MAP_PRIVATE, fd, 0));
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    auto* keymap = xkb_keymap_new_from_buffer(self->xkb_context_, map_str,
                                               size - 1, XKB_KEYMAP_FORMAT_TEXT_V1,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (!keymap) {
        std::cerr << "Failed to compile xkb keymap\n";
        return;
    }

    auto* state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }

    if (self->xkb_state_) xkb_state_unref(self->xkb_state_);
    if (self->xkb_keymap_) xkb_keymap_unref(self->xkb_keymap_);
    self->xkb_keymap_ = keymap;
    self->xkb_state_ = state;
}

void Seat::handle_key_enter(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                             wl_surface* /*surface*/, wl_array* /*keys*/) {}

void Seat::handle_key_leave(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                             wl_surface* /*surface*/) {}

void Seat::handle_key(void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                       uint32_t /*time*/, uint32_t key, uint32_t state) {
    auto* self = static_cast<Seat*>(data);
    if (!self->xkb_state_ || !self->key_cb_) return;

    KeyEvent ev;
    ev.keycode = key;
    ev.state = state;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(self->xkb_state_, key + 8);
    ev.sym = sym;
    ev.utf8_len = xkb_state_key_get_utf8(self->xkb_state_, key + 8,
                                          ev.utf8, sizeof(ev.utf8));

    // Modifiers
    xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(self->xkb_keymap_, XKB_MOD_NAME_CTRL);
    xkb_mod_index_t alt_idx = xkb_keymap_mod_get_index(self->xkb_keymap_, XKB_MOD_NAME_ALT);
    xkb_mod_index_t shift_idx = xkb_keymap_mod_get_index(self->xkb_keymap_, XKB_MOD_NAME_SHIFT);
    xkb_mod_index_t super_idx = xkb_keymap_mod_get_index(self->xkb_keymap_, XKB_MOD_NAME_LOGO);

    if (ctrl_idx != XKB_MOD_INVALID)
        ev.ctrl = xkb_state_mod_index_is_active(self->xkb_state_, ctrl_idx,
                                                 XKB_STATE_MODS_EFFECTIVE) > 0;
    if (alt_idx != XKB_MOD_INVALID)
        ev.alt = xkb_state_mod_index_is_active(self->xkb_state_, alt_idx,
                                                XKB_STATE_MODS_EFFECTIVE) > 0;
    if (shift_idx != XKB_MOD_INVALID)
        ev.shift = xkb_state_mod_index_is_active(self->xkb_state_, shift_idx,
                                                  XKB_STATE_MODS_EFFECTIVE) > 0;
    if (super_idx != XKB_MOD_INVALID)
        ev.super = xkb_state_mod_index_is_active(self->xkb_state_, super_idx,
                                                  XKB_STATE_MODS_EFFECTIVE) > 0;

    self->key_cb_(ev);
}

void Seat::handle_modifiers(void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                             uint32_t mods_depressed, uint32_t mods_latched,
                             uint32_t mods_locked, uint32_t group) {
    auto* self = static_cast<Seat*>(data);
    if (!self->xkb_state_) return;
    xkb_state_update_mask(self->xkb_state_, mods_depressed, mods_latched,
                          mods_locked, 0, 0, group);
}

void Seat::handle_repeat_info(void* /*data*/, wl_keyboard* /*keyboard*/,
                               int32_t /*rate*/, int32_t /*delay*/) {}

// --- Pointer handlers ---

void Seat::handle_pointer_enter(void* data, wl_pointer* /*pointer*/,
                                 uint32_t /*serial*/, wl_surface* /*surface*/,
                                 wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Seat*>(data);
    self->pointer_state_.entered = true;
    self->pointer_state_.x = wl_fixed_to_int(sx);
    self->pointer_state_.y = wl_fixed_to_int(sy);
}

void Seat::handle_pointer_leave(void* data, wl_pointer* /*pointer*/,
                                 uint32_t /*serial*/, wl_surface* /*surface*/) {
    auto* self = static_cast<Seat*>(data);
    self->pointer_state_.entered = false;
}

void Seat::handle_pointer_motion(void* data, wl_pointer* /*pointer*/,
                                  uint32_t /*time*/, wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Seat*>(data);
    int nx = wl_fixed_to_int(sx);
    int ny = wl_fixed_to_int(sy);
    if (self->motion_cb_ && (nx != self->pointer_state_.x || ny != self->pointer_state_.y)) {
        self->motion_cb_(nx, ny);
    }
    self->pointer_state_.x = nx;
    self->pointer_state_.y = ny;
}

void Seat::handle_pointer_button(void* data, wl_pointer* /*pointer*/,
                                  uint32_t /*serial*/, uint32_t time,
                                  uint32_t button, uint32_t state) {
    auto* self = static_cast<Seat*>(data);
    if (!self->pointer_cb_) return;

    self->pointer_state_.time = time;
    self->pointer_state_.button = button;
    self->pointer_state_.state = state;
    self->pointer_cb_(self->pointer_state_);
}

void Seat::handle_pointer_axis(void* data, wl_pointer* /*pointer*/,
                                uint32_t time, uint32_t axis,
                                wl_fixed_t value) {
    auto* self = static_cast<Seat*>(data);
    self->scroll_state_.time = time;
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        self->scroll_state_.delta_x += wl_fixed_to_double(value);
    } else {
        self->scroll_state_.delta_y += wl_fixed_to_double(value);
    }
}

void Seat::handle_pointer_frame(void* data, wl_pointer* /*pointer*/) {
    auto* self = static_cast<Seat*>(data);
    bool has_scroll = self->scroll_state_.delta_x != 0.0 ||
                      self->scroll_state_.delta_y != 0.0 ||
                      self->scroll_state_.discrete_x != 0 ||
                      self->scroll_state_.discrete_y != 0;
    if (has_scroll && self->scroll_cb_) {
        self->scroll_cb_(self->scroll_state_);
        self->scroll_state_ = ScrollEvent{};
    }
}

void Seat::handle_pointer_axis_source(void* /*data*/, wl_pointer* /*pointer*/,
                                       uint32_t /*axis_source*/) {}
void Seat::handle_pointer_axis_stop(void* /*data*/, wl_pointer* /*pointer*/,
                                     uint32_t /*time*/, uint32_t /*axis*/) {}
void Seat::handle_pointer_axis_discrete(void* data, wl_pointer* /*pointer*/,
                                         uint32_t axis, int32_t discrete) {
    auto* self = static_cast<Seat*>(data);
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        self->scroll_state_.discrete_x = discrete;
    } else {
        self->scroll_state_.discrete_y = discrete;
    }
}
void Seat::handle_pointer_axis_value120(void* /*data*/, wl_pointer* /*pointer*/,
                                         uint32_t /*axis*/, int32_t /*value120*/) {}
void Seat::handle_pointer_axis_relative_direction(void* /*data*/, wl_pointer* /*pointer*/,
                                                   uint32_t /*axis*/, uint32_t /*direction*/) {}

}
