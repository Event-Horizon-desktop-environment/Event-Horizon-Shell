#include "desktop_shell/Overview/overview_input.hpp"

#include "desktop_shell/Overview/overview_host.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "configuration/shell_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client.h>

namespace eh::shell::overview {

namespace {

const wl_pointer_listener kPointerListener = {
  .enter              = Host::pointer_enter,
  .leave              = Host::pointer_leave,
  .motion             = Host::pointer_motion,
  .button             = Host::pointer_button,
  .axis               = Host::pointer_axis,
  .frame              = Host::pointer_frame,
  .axis_source        = Host::pointer_axis_source,
  .axis_stop          = Host::pointer_axis_stop,
  .axis_discrete      = Host::pointer_axis_discrete,
  .axis_value120      = nullptr,
  .axis_relative_direction = nullptr,
#ifdef EH_HAVE_POINTER_WARP
  .warp = [](void* data, wl_pointer* ptr, wl_fixed_t sx, wl_fixed_t sy) {
    Host::pointer_motion(data, ptr, 0, sx, sy);
  },
#endif
};

const wl_keyboard_listener kKeyboardListener = {
  .keymap      = Host::keyboard_keymap,
  .enter       = Host::keyboard_enter,
  .leave       = Host::keyboard_leave,
  .key         = Host::keyboard_key,
  .modifiers   = Host::keyboard_modifiers,
  .repeat_info = Host::keyboard_repeat_info,
};

} // namespace

Input::Input(Host& host) : host_(host) {
  xkbCtx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

Input::~Input() {
  destroy_seat();
  if (xkbState_) { xkb_state_unref(xkbState_); xkbState_ = nullptr; }
  if (xkbKeymap_) { xkb_keymap_unref(xkbKeymap_); xkbKeymap_ = nullptr; }
  if (xkbCtx_) { xkb_context_unref(xkbCtx_); xkbCtx_ = nullptr; }
}

void Input::destroy_seat() {
  if (keyboard_) { wl_keyboard_release(keyboard_); keyboard_ = nullptr; }
  if (pointer_) { wl_pointer_release(pointer_); pointer_ = nullptr; }
  pointer_focus_ = false;
  pointer_focus_surface_ = nullptr;
  seat_ = nullptr;
}

void Input::setup_seat(wl_seat* seat) {
  seat_ = seat;
  if (!pointer_) {
    pointer_ = wl_seat_get_pointer(seat);
    if (pointer_) {
      wl_pointer_set_user_data(pointer_, &host_);
      wl_pointer_add_listener(pointer_, &kPointerListener, &host_);
    }
  }
  if (!keyboard_) {
    keyboard_ = wl_seat_get_keyboard(seat);
    if (keyboard_) {
      wl_keyboard_set_user_data(keyboard_, &host_);
      wl_keyboard_add_listener(keyboard_, &kKeyboardListener, &host_);
    }
  }
}

bool Input::owns_surface(const wl_surface* s) const noexcept {
  return s && (s == host_.surface() || s == host_.backdrop_surface());
}

bool Input::needs_frame() const {
  return host_.open() && !host_.has_frame_cb();
}

void Input::on_seat_capabilities(wl_seat* seat, uint32_t capabilities) {
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !pointer_) {
    pointer_ = wl_seat_get_pointer(seat);
    if (pointer_) {
      wl_pointer_set_user_data(pointer_, &host_);
      wl_pointer_add_listener(pointer_, &kPointerListener, &host_);
    }
  } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && pointer_) {
    wl_pointer_release(pointer_);
    pointer_ = nullptr;
    pointer_focus_ = false;
  }
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard_) {
    keyboard_ = wl_seat_get_keyboard(seat);
    if (keyboard_) {
      wl_keyboard_set_user_data(keyboard_, &host_);
      wl_keyboard_add_listener(keyboard_, &kKeyboardListener, &host_);
    }
  } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard_) {
    if (xkbState_) { xkb_state_unref(xkbState_); xkbState_ = nullptr; }
    if (xkbKeymap_) { xkb_keymap_unref(xkbKeymap_); xkbKeymap_ = nullptr; }
    wl_keyboard_release(keyboard_);
    keyboard_ = nullptr;
  }
  seat_ = seat;
}

void Input::on_keymap(uint32_t format, int32_t fd, uint32_t size) {
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { ::close(fd); return; }
  char* map_str = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map_str == MAP_FAILED) { ::close(fd); return; }
  auto* km = xkb_keymap_new_from_string(xkbCtx_, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
                                         XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  ::close(fd);
  if (!km) return;
  if (xkbState_) xkb_state_unref(xkbState_);
  if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
  xkbKeymap_ = km;
  xkbState_ = xkb_state_new(km);
}

void Input::on_keyboard_modifiers(uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  if (xkbState_)
    xkb_state_update_mask(xkbState_, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

void Input::handle_pointer_enter(wl_surface* surface, double sx, double sy) {
  pointer_focus_ = owns_surface(surface);
  if (!pointer_focus_) return;
  pointer_focus_surface_ = surface;
  ptr_x_ = sx;
  ptr_y_ = sy;
  update_hover_from_pointer();
}

void Input::handle_pointer_leave() {
  pointer_focus_ = false;
  pointer_focus_surface_ = nullptr;
  bool changed = false;
  if (hovered_search) { hovered_search = false; changed = true; }
  if (hovered_ws >= 0) { hovered_ws = -1; changed = true; }
  if (hovered_win >= 0) { hovered_win = -1; changed = true; }
  if (hovered_qs >= 0) { hovered_qs = -1; changed = true; }
  if (hovered_app >= 0) { hovered_app = -1; changed = true; }
  if (close_hovered) { close_hovered = false; changed = true; }
  button_pressed = false;
  drag_active = false;
  drag_auto_scroll_dir = 0;
  press_win = -1;
  press_app_idx = -1;
  drop_target_ws = -1;
  drop_target_win = -1;
  drop_target_add = false;
  if (changed && host_.open() && !host_.has_frame_cb()) host_.schedule_frame();
}

void Input::handle_pointer_motion(double sx, double sy) {
  if (!pointer_focus_) return;
  ptr_x_ = sx;
  ptr_y_ = sy;

  if (button_pressed && !drag_active) {
    const double dx = sx - press_x;
    const double dy = sy - press_y;
    if (dx * dx + dy * dy > 64.0) {
      drag_active = true;
      drag_auto_scroll_dir = 0;
      drop_target_ws = -1;
      bool ok = false;
      const OverviewCardRect t =
          overview_tile_rect_for_flat(host_.layout(), host_.workspaces(), press_win, host_.scroll_pos(), &ok);
      if (ok && t.w > 0.0) {
        drag_grab_dx = press_x - t.x;
        drag_grab_dy = press_y - t.y;
      } else {
        drag_grab_dx = 0.0;
        drag_grab_dy = 0.0;
      }
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
  }

  if (drag_active) {
    constexpr double kEdgeZone = 64.0;
    drag_auto_scroll_dir = 0;
    if (host_.layout().axis == OverviewAxis::Horizontal) {
      if (ptr_x_ < kEdgeZone) drag_auto_scroll_dir = -1;
      else if (ptr_x_ > host_.layout().w - kEdgeZone) drag_auto_scroll_dir = 1;
    } else {
      if (ptr_y_ < kEdgeZone) drag_auto_scroll_dir = -1;
      else if (ptr_y_ > host_.layout().h - kEdgeZone) drag_auto_scroll_dir = 1;
    }

    drop_target_ws = pick_workspace_at(host_.layout(), ptr_x_, ptr_y_, host_.workspaces(), host_.scroll_pos());
    if (drop_target_ws == press_ws) drop_target_ws = -1;
    drop_target_win = -1;
    if (drop_target_ws < 0 && press_ws >= 0) {
      int flat = -1;
      if (pick_window_at(host_.layout(), host_.workspaces(), ptr_x_, ptr_y_, press_ws, host_.scroll_pos(), &flat) >= 0 &&
          flat >= 0 && flat != press_win) {
        drop_target_win = flat;
      }
    }
    // Dragging over the strip's add-workspace slot creates a new workspace
    // for the dragged window on release; it wins over card targets.
    drop_target_add = pick_quick_select_add_at(host_.layout().qs, ptr_x_, ptr_y_);
    if (drop_target_add) {
      drop_target_ws = -1;
      drop_target_win = -1;
      hovered_qs = kQuickSelectAddIdx;
      hovered_ws = -1;
    } else if (hovered_qs == kQuickSelectAddIdx) {
      hovered_qs = -1;
    }
    if (!host_.has_frame_cb()) host_.schedule_frame();
    return;
  }

  update_hover_from_pointer();
}

void Input::update_hover_from_pointer() {
  if (!pointer_focus_) return;
  const double sx = ptr_x_;
  const double sy = ptr_y_;
  bool changed = false;
  const auto& layout = host_.layout();
  const auto& workspaces = host_.workspaces();

  {
    const double cx = layout.w * 0.5;
    const double sx2 = cx - layout.searchW * 0.5;
    const bool inSearch = (sx >= sx2 && sx <= sx2 + layout.searchW &&
                           sy >= layout.searchY && sy <= layout.searchY + layout.searchH);
    if (inSearch != hovered_search) { hovered_search = inSearch; changed = true; }
  }

  {
    int newQs = pick_quick_select_at(layout.qs, layout, workspaces, sx, sy);
    if (newQs < 0 && pick_quick_select_add_at(layout.qs, sx, sy))
      newQs = kQuickSelectAddIdx;
    if (newQs != hovered_qs) { hovered_qs = newQs; changed = true; }
  }

  if (host_.show_apps()) {
    const int newApp =
        pick_app_at(layout.appGrid, sx, sy, static_cast<int>(host_.apps().size()),
                    host_.app_scroll_pos());
    if (newApp != hovered_app) { hovered_app = newApp; changed = true; }
    if (hovered_ws != -1) { hovered_ws = -1; changed = true; }
    if (hovered_win != -1) { hovered_win = -1; changed = true; }
    if (close_hovered) { close_hovered = false; changed = true; }
  } else {
    const int newWs = pick_workspace_at(layout, sx, sy, workspaces, host_.scroll_pos());
    int flat = -1;
    int newWin = -1;
    if (newWs >= 0) {
      newWin = pick_window_at(layout, workspaces, sx, sy, newWs, host_.scroll_pos(), &flat);
      if (newWin < 0) flat = -1;
    }
    if (newWs != hovered_ws) { hovered_ws = newWs; changed = true; }
    if (flat != hovered_win) { hovered_win = flat; changed = true; }

    bool newClose = false;
    if (newWs >= 0 && flat >= 0)
      newClose = pick_close_at(layout, workspaces, sx, sy, newWs, host_.scroll_pos(), nullptr);
    if (newClose != close_hovered) { close_hovered = newClose; changed = true; }

    if (hovered_app != -1) { hovered_app = -1; changed = true; }
  }

  if (changed && host_.open() && !host_.has_frame_cb()) host_.schedule_frame();
}

void Input::handle_pointer_button(uint32_t button, uint32_t state) {
  if (!host_.open() || !pointer_focus_ || !host_.surface()) return;
  if (button != 0x110) return;
  const auto& layout = host_.layout();
  const auto& workspaces = host_.workspaces();

  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    {
      const int qsIdx = pick_quick_select_at(layout.qs, layout, workspaces, ptr_x_, ptr_y_);
      if (qsIdx >= 0) {
        host_.select_workspace(qsIdx);
        return;
      }
    }

    const int wsIdx = pick_workspace_at(layout, ptr_x_, ptr_y_, workspaces, host_.scroll_pos());

    int closeFlat = -1;
    if (wsIdx >= 0 &&
        pick_close_at(layout, workspaces, ptr_x_, ptr_y_, wsIdx, host_.scroll_pos(), &closeFlat) &&
        closeFlat >= 0) {
      host_.close_window(closeFlat);
      return;
    }

    {
      const double cx = layout.w * 0.5;
      const double sx = cx - layout.searchW * 0.5;
      const bool inSearch = (ptr_x_ >= sx && ptr_x_ <= sx + layout.searchW &&
                             ptr_y_ >= layout.searchY && ptr_y_ <= layout.searchY + layout.searchH);
      if (inSearch) {
        host_.toggle_apps_mode();
        return;
      }
    }

    if (host_.show_apps()) {
      press_app_idx = pick_app_at(layout.appGrid, ptr_x_, ptr_y_,
                                  static_cast<int>(host_.apps().size()),
                                  host_.app_scroll_pos());
      if (press_app_idx >= 0) {
        button_pressed = true;
        press_x = ptr_x_;
        press_y = ptr_y_;
        drag_active = false;
        drag_auto_scroll_dir = 0;
        drop_target_ws = -1;
        return;
      }
      return;
    }

    if (wsIdx >= 0) {
      int flat = -1;
      if (pick_window_at(layout, workspaces, ptr_x_, ptr_y_, wsIdx, host_.scroll_pos(), &flat) >= 0 &&
          flat >= 0) {
        press_ws = wsIdx;
        press_win = flat;
        button_pressed = true;
        press_x = ptr_x_;
        press_y = ptr_y_;
        drag_active = false;
        drag_auto_scroll_dir = 0;
        drop_target_ws = -1;
        drop_target_win = -1;
        return;
      }
      host_.select_workspace(wsIdx);
      return;
    }

    host_.close();

  } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    if (!button_pressed) return;
    button_pressed = false;

    if (drag_active) {
      drag_active = false;
      drag_auto_scroll_dir = 0;
      if (host_.show_apps() && press_app_idx >= 0) {
        if (press_app_idx < static_cast<int>(host_.apps().size())) {
          launch_exec_command(host_.apps()[static_cast<size_t>(press_app_idx)].exec);
          host_.close();
        }
      } else if (press_win >= 0) {
        if (drop_target_add) {
          host_.move_window_to_new_workspace(press_win);
        } else if (drop_target_ws >= 0 && drop_target_ws != press_ws) {
          host_.move_window_to_workspace(press_win, drop_target_ws);
        } else if (drop_target_win >= 0) {
          host_.swap_windows_in_place(press_win, drop_target_win);
        } else {
          if (!host_.has_frame_cb()) host_.schedule_frame();
        }
      }
      press_ws = -1;
      press_win = -1;
      press_app_idx = -1;
      drop_target_ws = -1;
      drop_target_win = -1;
      drop_target_add = false;
    } else {
      if (host_.show_apps() && press_app_idx >= 0) {
        if (press_app_idx < static_cast<int>(host_.apps().size())) {
          launch_exec_command(host_.apps()[static_cast<size_t>(press_app_idx)].exec);
          host_.close();
        }
      } else if (press_win >= 0) {
        host_.activate_window(press_win);
      }
      press_ws = -1;
      press_win = -1;
      press_app_idx = -1;
    }
  }
}

void Input::handle_axis_discrete(uint32_t axis, int32_t discrete) {
  if (!host_.open() || !pointer_focus_) return;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  if (host_.show_apps()) {
    const double cellH = host_.layout().appGrid.cellH;
    if (discrete == 0 || cellH <= 0.0) return;
    host_.scroll_app_grid(static_cast<double>(discrete) * cellH);
    return;
  }
  if (discrete == 0 || host_.layout().pitch <= 0.0) return;
  host_.scroll_by(static_cast<double>(discrete) * host_.layout().pitch, pointer_focus_surface_);
}

void Input::handle_axis(double value) {
  if (!host_.open() || !pointer_focus_) return;
  if (host_.show_apps()) {
    if (value == 0.0) return;
    if (axis_source_ == WL_POINTER_AXIS_SOURCE_WHEEL ||
        axis_source_ == WL_POINTER_AXIS_SOURCE_WHEEL_TILT)
      return;
    host_.scroll_app_grid(value * 0.4);
    return;
  }
  if (value == 0.0 || host_.layout().pitch <= 0.0) return;
  if (axis_source_ == WL_POINTER_AXIS_SOURCE_WHEEL ||
      axis_source_ == WL_POINTER_AXIS_SOURCE_WHEEL_TILT)
    return;
  host_.scroll_by(value * 0.008 * host_.layout().pitch, pointer_focus_surface_);
  trackpad_settle_until_ms_ = now_mono_ms() + static_cast<uint64_t>(host_.scroll_event_delay_ms());
}

void Input::handle_axis_stop(uint32_t axis) {
  if (!host_.open()) return;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  trackpad_settle_until_ms_ = now_mono_ms() + static_cast<uint64_t>(host_.scroll_event_delay_ms());
}

void Input::handle_key(uint32_t key, uint32_t state) {
  if (!host_.open() || !xkbState_) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState_, key + 8);

  if (sym == XKB_KEY_Escape) {
    if (!host_.search_query().empty()) {
      host_.search_query().clear();
      host_.set_show_apps(false);
      host_.refresh_app_list();
      hovered_app = -1;
      hovered_ws = -1;
      hovered_win = -1;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    } else if (host_.show_apps()) {
      host_.set_show_apps(false);
      hovered_app = -1;
      app_hover_lift = 0.f;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    } else {
      host_.close();
    }
    return;
  }

  if (sym == XKB_KEY_BackSpace) {
    if (!host_.search_query().empty()) {
      host_.search_query().pop_back();
      if (host_.search_query().empty()) host_.set_show_apps(false);
      host_.refresh_app_list();
      hovered_app = -1;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
    return;
  }

  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (host_.show_apps() && !host_.apps().empty()) {
      const int idx = (hovered_app >= 0 && hovered_app < static_cast<int>(host_.apps().size()))
                          ? hovered_app : 0;
      launch_exec_command(host_.apps()[static_cast<size_t>(idx)].exec);
      host_.close();
    } else if (hovered_win >= 0 && hovered_win < static_cast<int>(host_.nav_windows().size())) {
      host_.activate_window(hovered_win);
    } else if (host_.selected_index() >= 0 &&
               host_.selected_index() < static_cast<int>(host_.workspaces().size())) {
      host_.select_workspace(host_.selected_index());
    }
    return;
  }

  if (host_.show_apps() && !host_.apps().empty()) {
    const int nApps = static_cast<int>(host_.apps().size());
    if (sym == XKB_KEY_Right || sym == XKB_KEY_Tab) {
      int sel = std::max(0, hovered_app);
      sel = (sel + 1) % nApps;
      hovered_app = sel;
      host_.make_app_visible(sel);
      if (!host_.has_frame_cb()) host_.schedule_frame();
      return;
    }
    if (sym == XKB_KEY_Left) {
      int sel = hovered_app < 0 ? 0 : hovered_app;
      sel = (sel - 1 + nApps) % nApps;
      hovered_app = sel;
      host_.make_app_visible(sel);
      if (!host_.has_frame_cb()) host_.schedule_frame();
      return;
    }
    if (sym == XKB_KEY_Down || sym == XKB_KEY_Page_Down ||
        sym == XKB_KEY_Up || sym == XKB_KEY_Page_Up) {
      const auto& grid = host_.layout().appGrid;
      const bool down = (sym == XKB_KEY_Down || sym == XKB_KEY_Page_Down);
      const bool page = (sym == XKB_KEY_Page_Down || sym == XKB_KEY_Page_Up);
      int step = std::max(grid.cols, 1);
      if (page) step *= std::max(grid.rows, 1);
      if (!down) step = -step;
      int sel = hovered_app < 0 ? 0 : hovered_app;
      sel = std::clamp(sel + step, 0, nApps - 1);
      hovered_app = sel;
      host_.make_app_visible(sel);
      if (!host_.has_frame_cb()) host_.schedule_frame();
      return;
    }
  }

  const int nWin = static_cast<int>(host_.nav_windows().size());
  if (sym == XKB_KEY_Tab || sym == XKB_KEY_Right) {
    if (nWin > 0) {
      int sel = std::max(0, hovered_win);
      sel = (sel + 1) % nWin;
      hovered_win = sel;
      hovered_ws = host_.nav_windows()[static_cast<size_t>(sel)].first;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
    return;
  }
  if (sym == XKB_KEY_Left) {
    if (nWin > 0) {
      int sel = hovered_win < 0 ? 0 : hovered_win;
      sel = (sel - 1 + nWin) % nWin;
      hovered_win = sel;
      hovered_ws = host_.nav_windows()[static_cast<size_t>(sel)].first;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
    return;
  }
  if (sym == XKB_KEY_Down) {
    if (nWin > 0) {
      int sel = std::max(0, hovered_win);
      sel = std::min(sel + 1, nWin - 1);
      hovered_win = sel;
      hovered_ws = host_.nav_windows()[static_cast<size_t>(sel)].first;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
    return;
  }
  if (sym == XKB_KEY_Up) {
    if (nWin > 0) {
      int sel = hovered_win < 0 ? 0 : hovered_win;
      sel = std::max(0, sel - 1);
      hovered_win = sel;
      hovered_ws = host_.nav_windows()[static_cast<size_t>(sel)].first;
      if (!host_.has_frame_cb()) host_.schedule_frame();
    }
    return;
  }

  std::array<char, 8> buf{};
  int len = xkb_state_key_get_utf8(xkbState_, key + 8, buf.data(), buf.size());
  if (len > 0 && buf[0] >= 32 && static_cast<unsigned char>(buf[0]) != 0x7f) {
    host_.search_query() += buf.data();
    if (!host_.show_apps()) {
      host_.set_show_apps(true);
      host_.refresh_app_list();
    } else {
      host_.refresh_app_list();
    }
    hovered_app = -1;
    if (!host_.has_frame_cb()) host_.schedule_frame();
  }
}

} // namespace eh::shell::overview
