#include "desktop_shell/shared/system/input/shell_input.hpp"

#include "desktop_shell/dock/paint/dock_anim.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/core/dock_settings.hpp"
#include "desktop_shell/dock/input/dock_input_region.hpp"
#include "desktop_shell/dock/input/dock_pick.hpp"
#include "desktop_shell/dock/input/dock_slot_dispatch.hpp"
#include "desktop_shell/dock/tooltip/dock_tooltip.hpp"
#include "desktop_shell/dock/pinned/dock_pinned.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/dock/core/dock_bar.h"
#include "desktop_shell/shared/popup/chrome/chrome.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/shared/layout/strip_geometry.hpp"
#include "desktop_shell/shared/popup/dispatch/popup_dispatch.hpp"
#include "desktop_shell/shared/popup/dispatch/popup_items.hpp"
#include "desktop_shell/controlcenter/input/control_center_dispatch.hpp"
#include "desktop_shell/dashboard/dashboard_dispatch.hpp"
#include "desktop_shell/spotlight/paint/spotlight_paint.hpp"
#include "desktop_shell/spotlight/search/spotlight_query.hpp"
#include "desktop_shell/spotlight/search/spotlight_keyboard.hpp"
#include "desktop_shell/widgets/app_drawer/input/app_drawer_keyboard.hpp"
#include "desktop_shell/widgets/app_drawer/input/app_drawer_dispatch.hpp"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/dock/layout/dock_layout_shared.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/trace/app_drawer_trace.hpp"
#include "desktop_shell/dock/launch/dock_launch_feedback.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/time/mono_time.hpp"
#include "ux/settings/common/embed/settings_embed.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"
#include "desktop_shell/common/log/verbose_log.hpp"

#include "wl/core/protocols.hpp"

#include <xkbcommon/xkbcommon.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <sys/mman.h>
#include <unistd.h>

namespace {
constexpr double kDockHoverLiftMax = 5.0;

bool shell_input_trace_enabled() noexcept {
  static const bool k = eh::debug_profile::env_bool("EH_DOCK_INPUT_TRACE");
  return k;
}
} // namespace

using eh::shell::now_mono_ms;

namespace {
void shell_pointer_enter(void* data, wl_pointer*  , uint32_t  , wl_surface* surface, wl_fixed_t sx,
                          wl_fixed_t sy) {
   
  auto& app = *static_cast<DockApp*>(data);
  app.pointerSurface = surface;
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);
  if (shell_input_trace_enabled()) {
    std::cerr << "[shell-input] enter surf=" << static_cast<const void*>(surface)
              << " is_dock_layer=" << (dock_layer_from_surface(app, surface) != nullptr ? 1 : 0) << '\n';
  }
  // Dashboard owns two surfaces: the trigger strip (pure toggle on enter)
  // and the panel. Route both straight to it so the dock auto-hide /
  // hover-lift paths below never see either surface.
  if (surface == app.dash.trigSurface) {
    eh::shell::dashboard::dashboard_trigger_enter(app);
    return;
  }
  if (surface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_enter(app);
    return;
  }
  if (DockOutputLayer* dockL = dock_layer_from_surface(app, surface)) {
    for (size_t i = 0; i < app.dockLayers.size(); ++i) {
      if (app.dockLayers[i].get() == dockL) {
        app.pointerDockLayerIdx = i;
        break;
      }
    }
  }
  if (surface == eh::settings::embed_settings_surface() ||
      surface == eh::settings::embed_widget_picker_surface() ||
      surface == eh::settings::embed_default_app_picker_surface()) {
    eh::settings::embed_dispatch_pointer_motion(surface, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
  }
  if (app.popupSurface && surface == app.popupSurface) {
    app.dockHoverSlot = -1;
    app.dockHoverLiftTarget = 0.0;
    dock_anim_start_hover_lift(app);
    if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
      popup_draw_surface(app);
      wl_display_flush(app.display);
    }
  }
  if (!app.autoHide) return;
  app.reveal = true;
  app.animTargetPx = 0.0;
  sync_input_region(app);
  for (auto& up : app.dockLayers) {
    if (up && up->layer) zwlr_layer_surface_v1_set_exclusive_zone(up->layer, app.dockHeight + dock_exclusive_zone_gap_px(app.settings));
  }
  if (app.dockLayers.empty() && app.layerSurface) zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, app.dockHeight + dock_exclusive_zone_gap_px(app.settings));
  dock_anim_start_slide(app);
}

void shell_pointer_leave(void* data, wl_pointer*  , uint32_t  , wl_surface* surface) {
   
  auto& app = *static_cast<DockApp*>(data);
  if (surface == eh::settings::embed_settings_surface()) {
    eh::settings::embed_dispatch_pointer_leave();
  } else if (surface == eh::settings::embed_widget_picker_surface()) {
    eh::settings::embed_dispatch_widget_picker_pointer_leave();
  } else if (surface == eh::settings::embed_default_app_picker_surface()) {
    eh::settings::embed_dispatch_default_app_picker_pointer_leave();
  }
  const bool onDashTrig = (surface == app.dash.trigSurface);
  const bool onDashLeave = (surface == app.dash.panelSurface);
  if (surface && app.pointerSurface == surface) app.pointerSurface = nullptr;

  if (onDashTrig) return;  // strip toggle happens on enter; leaves never act
  if (onDashLeave) {
    // Mid-drag leaves are parked (pendingLeave) and cleared on release.
    eh::shell::dashboard::dashboard_pointer_leave(app);
    return;
  }

  if (surface && dock_layer_from_surface(app, surface)) {
    app.dockHoverSlot = -1;
    app.dockHoverLiftTarget = 0.0;
    dock_anim_start_hover_lift(app);
  }

  if (app.popupOpen && surface == app.popupSurface &&
      (app.popupKind == DockApp::PopupKind::App || app.popupKind == DockApp::PopupKind::Tray ||
       app.popupKind == DockApp::PopupKind::Trash)) {
    popup_close(app);
  }

  if (!app.autoHide) return;
  app.reveal = false;
  app.animTargetPx = static_cast<double>(app.dockHeight - app.triggerHeight);
  sync_input_region(app);
  for (auto& up : app.dockLayers) {
    if (up && up->layer) zwlr_layer_surface_v1_set_exclusive_zone(up->layer, -1);
  }
  if (app.dockLayers.empty() && app.layerSurface) zwlr_layer_surface_v1_set_exclusive_zone(app.layerSurface, -1);
  dock_anim_start_slide(app);
}

void shell_pointer_motion(void* data, wl_pointer*  , uint32_t  , wl_fixed_t sx, wl_fixed_t sy) {
   
  auto& app = *static_cast<DockApp*>(data);
  app.pointerX = wl_fixed_to_double(sx);
  app.pointerY = wl_fixed_to_double(sy);

  if (app.pointerSurface == app.dash.panelSurface) {
    eh::shell::dashboard::dashboard_pointer_motion(app);
    return;
  }
  if (app.pointerSurface == app.dash.trigSurface) return;  // 8px strip: nothing hoverable

  if (eh::shell::popup::popup_handle_motion(app)) return;

  if (eh::shell::control_center::handle_motion(app)) return;

  if (app.pointerSurface == eh::settings::embed_settings_surface() ||
      app.pointerSurface == eh::settings::embed_widget_picker_surface() ||
      app.pointerSurface == eh::settings::embed_default_app_picker_surface()) {
    eh::settings::embed_dispatch_pointer_motion(app.pointerSurface, app.pointerX, app.pointerY);
    return;
  }

  if (app.configured && dock_pointer_on_any_dock_layer(app)) {
    if (app.pinDragging || app.pinDragCandidate) {
      // A pin press/drag owns hover: no lift animation and no tooltip — the
      // tooltip's create() does a blocking wl_display_roundtrip that would
      // stutter the drag.
      if (app.dockHoverSlot != -1 || app.tooltipHoverSlot >= 0 || app.tooltipShownSlot >= 0) {
        app.dockHoverSlot = -1;
        app.dockHoverLiftTarget = 0.0;
        app.dockHoverLiftPx = 0.0;
        app.shellAnim.cancel(app.dockHoverLiftAnimId);
        app.dockHoverLiftAnimId = 0;
        dock_tooltip_cancel(app);
      }
    } else {
      const DockPickResult pr = dock_pick_at(app, app.pointerX, app.pointerY);
      int next = -1;
      if (pr.idx >= 0)
        next = pr.idx;
      if (next != app.dockHoverSlot) {
        app.dockHoverSlot = next;

        if (next >= 0) {
          app.dockHoverLiftPx = 0.0;
          app.dockHoverLiftTarget = kDockHoverLiftMax;
          dock_tooltip_start_timer(app, next);
        } else {
          app.dockHoverLiftTarget = 0.0;
          dock_tooltip_cancel(app);
        }
        dock_anim_start_hover_lift(app);
      }
    }
  }

  eh::shell::dock::app_drawer::app_drawer_handle_pointer_motion(app);
  eh::shell::popup::popup_handle_items_motion(app);

  dock_pinned_pointer_motion(app);
}
void shell_pointer_button(void* data, wl_pointer*  , uint32_t serial, uint32_t  ,
                           uint32_t button, uint32_t state) {
   
  auto& app = *static_cast<DockApp*>(data);
  const bool onDockSurface = dock_pointer_on_any_dock_layer(app);
  const bool onPopupSurface = dock_popup_pointer_on_any_popup_surface(app);
  const bool onDashSurface = (app.pointerSurface != nullptr && app.pointerSurface == app.dash.panelSurface);
  const bool onTrigSurface = (app.pointerSurface != nullptr && app.pointerSurface == app.dash.trigSurface);
  if (app.pointerSurface == eh::settings::embed_settings_surface() ||
      app.pointerSurface == eh::settings::embed_widget_picker_surface() ||
      app.pointerSurface == eh::settings::embed_default_app_picker_surface()) {
    eh::settings::embed_dispatch_pointer_button(app.pointerSurface, button, state, serial);
    return;
  }
  const bool left = (button == 0x110  );

  const bool right = (button == 0x111  ) || (button == 0x112  ) || (button == 0x113  );
  if (!left && !right) return;
  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    app.popupDismissedThisPress = DockApp::PopupKind::None;
    if (left && eh::shell::control_center::handle_button_release(app)) {
      // CC drag ended, fall through to handle other release events
    }
    if (left && eh::shell::dashboard::dashboard_button_release(app)) {
      // Dashboard slider drag ended, fall through
    }
    if (left && app.dockPressedSlot >= 0) {
      app.dockPressedSlot = -1;
      dock_draw(app);
      wl_display_flush(app.display);
    }

    if (left && (app.pinDragCandidate || app.pinDragging)) {
      const bool wasDragging = app.pinDragging;
      const std::string key = app.pinDragKey;
      auto* h = app.pinClickHandle;
      const bool dragDirty = app.pinDragDirty;

      if (wasDragging && dragDirty && !app.pinDragPaintOrder.empty()) {
        app.settings.pinnedApps = app.pinDragPaintOrder;
      }
      app.pinDragPinsSnapshot.clear();
      app.pinDragPaintOrder.clear();

      app.pinDragCandidate = false;
      app.pinDragging = false;
      app.pinDragDirty = false;
      app.pinDragInsertIdx = -1;
      app.pinDragLayerOnlyNextDraw = false;
      app.pinDragKey.clear();
      app.pinClickHandle = nullptr;
      app.shellAnim.cancel(app.pinDragShiftAnimId);
      app.pinDragShiftAnimId = 0;
      app.pinDragShiftFrom.clear();
      app.pinDragShiftT = 1.0;
      app.pinDragGeometryValid = false;

      if (eh_dock_pin_drag_perf_enabled() && wasDragging) {
        std::cerr << "[dock-pin-perf] drag_end dirty=" << (dragDirty ? 1 : 0)
                  << " insert_mutations=" << app.pinDragPerfInsertChanges
                  << " ghost_coalesce_paths=" << app.pinDragPerfGhostCoalesce
                  << " schedule_noop_frame_pending=" << app.pinDragPerfScheduleNoop << "\n";
      }
      app.pinDragPerfInsertChanges = 0;
      app.pinDragPerfGhostCoalesce = 0;
      app.pinDragPerfScheduleNoop = 0;

      if (!wasDragging && !key.empty()) {

        if (h) {
          zwlr_foreign_toplevel_handle_v1_activate(h, app.seat);
          wl_display_flush(app.display);
          dock_start_launch_bounce(app, key, false);
        } else {
          if (auto desktop = find_desktop_file_for_appid(key)) {
            if (auto info = read_desktop_entry_info(*desktop)) {
              launch_exec_command(info->exec);
              dock_start_launch_bounce(app, key, true);
            }
          }
        }
      }

      dock_draw(app);
      wl_display_flush(app.display);

      if (wasDragging && dragDirty) {

        (void)dock_save_dock_settings(app.settings);
        app.settingsMtime = eh::config::aggregate_config_source_mtime();
        app.sizeDirty = true;
      }
    }
    return;
  }
  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;

  if (shell_input_trace_enabled()) {
    std::cerr << "[shell-input] press left=" << (left ? 1 : 0) << " onDock=" << (onDockSurface ? 1 : 0)
              << " onPop=" << (onPopupSurface ? 1 : 0)

              << " ptrSurf=" << static_cast<const void*>(app.pointerSurface) << " xy=" << app.pointerX << ','
              << app.pointerY << '\n';
  }

  if (app.popupOpen && (app.popupKind == DockApp::PopupKind::App || app.popupKind == DockApp::PopupKind::Tray ||
                           app.popupKind == DockApp::PopupKind::Trash ||
                           app.popupKind == DockApp::PopupKind::Calendar ||
                           app.popupKind == DockApp::PopupKind::Weather ||
                             app.popupKind == DockApp::PopupKind::VolumeMixer ||
                             app.popupKind == DockApp::PopupKind::MediaPlayer ||
                             app.popupKind == DockApp::PopupKind::Vpn ||
                              app.popupKind == DockApp::PopupKind::Battery ||
                              app.popupKind == DockApp::PopupKind::Bluetooth) &&
      !onPopupSurface) {
    const auto dismissedKind = app.popupKind;
    popup_close(app);
    app.popupDismissedThisPress = dismissedKind;
  }

  if (app.pointerSurface != nullptr && !onDockSurface && !onPopupSurface && !onDashSurface && !onTrigSurface) {
    if (shell_input_trace_enabled()) {
      std::cerr << "[shell-input] press DROPPED: wl_seat focus surface is not dock/popup (see enter lines)\n";
    }
    return;
  }

  if (eh_verbose_enabled())
    std::cout << "[input] click: button=" << (left ? "left" : "right") << " x=" << app.pointerX << " y=" << app.pointerY << "\n";
  if (!app.seat) return;

  if (onDashSurface) {
    if (left) eh::shell::dashboard::dashboard_pointer_press(app, serial);
    return;
  }

  if (app.popupOpen && dock_popup_pointer_on_any_popup_surface(app)) {
    if (eh::shell::dock::app_drawer::app_drawer_handle_click(app, left, right)) return;
    if (eh::shell::dock::spotlight::spotlight_handle_click(app)) return;
    if (eh::shell::control_center::handle_button_press(app, serial)) return;
    if (eh::shell::popup::popup_handle_click(app, app.pointerX, app.pointerY, serial)) return;
    if (eh::shell::popup::popup_handle_items_click(app)) return;
  }

  eh::shell::dock::dock_handle_slot_press(app, serial, left, right, onDockSurface);
}

void shell_pointer_axis(void* data, wl_pointer*  , uint32_t  , uint32_t axis, wl_fixed_t value) {
   
  auto& app = *static_cast<DockApp*>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  const double dv = wl_fixed_to_double(value);
  const double deltaPx = std::max(-300.0, std::min(300.0, dv * 20.0));
  if (eh::settings::embed_window_visible() &&
      (app.pointerSurface == eh::settings::embed_settings_surface() ||
       app.pointerSurface == eh::settings::embed_default_app_picker_surface())) {
    eh::settings::embed_dispatch_pointer_axis_vertical(-deltaPx);
    return;
  }
  if (eh::shell::dashboard::dashboard_axis(app, deltaPx)) return;
  if (eh::shell::control_center::handle_axis(app, deltaPx)) return;
  if (eh::shell::dock::app_drawer::app_drawer_handle_axis(app, deltaPx)) return;
}
void shell_pointer_frame(void*  , wl_pointer*  ) {}
void shell_pointer_axis_source(void*  , wl_pointer*  , uint32_t  ) {}
void shell_pointer_axis_stop(void*  , wl_pointer*  , uint32_t  , uint32_t  ) {}
void shell_pointer_axis_discrete(void*  , wl_pointer*  , uint32_t  , int32_t  ) {}
void shell_pointer_axis_value120(void*  , wl_pointer*  , uint32_t  , int32_t  ) {}
void shell_pointer_axis_relative_direction(void*  , wl_pointer*  , uint32_t  , uint32_t  ) {}
#ifdef EH_HAVE_POINTER_WARP
void shell_pointer_warp(void* data, wl_pointer* p, wl_fixed_t sx, wl_fixed_t sy) {
  shell_pointer_motion(data, p, 0, sx, sy);
}
#endif

const wl_pointer_listener g_shell_pointer_listener = {
  .enter = shell_pointer_enter,
  .leave = shell_pointer_leave,
  .motion = shell_pointer_motion,
  .button = shell_pointer_button,
  .axis = shell_pointer_axis,
  .frame = shell_pointer_frame,
  .axis_source = shell_pointer_axis_source,
  .axis_stop = shell_pointer_axis_stop,
  .axis_discrete = shell_pointer_axis_discrete,
  .axis_value120 = shell_pointer_axis_value120,
  .axis_relative_direction = shell_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
  .warp = shell_pointer_warp,
#endif
};

void shell_keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto& app = *static_cast<DockApp*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !app.xkbCtx) {
    if (fd >= 0) close(fd);
    return;
  }
  char* map_str =
      static_cast<char*>(mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;

  if (app.xkbKeymap) xkb_keymap_unref(app.xkbKeymap);
  if (app.xkbState) xkb_state_unref(app.xkbState);

  const size_t map_len = size > 0 ? static_cast<size_t>(size) - 1 : 0;
  app.xkbKeymap =
      xkb_keymap_new_from_buffer(app.xkbCtx, map_str, map_len, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, static_cast<size_t>(size));
  app.xkbState = app.xkbKeymap ? xkb_state_new(app.xkbKeymap) : nullptr;
}

void shell_keyboard_enter(void* data, wl_keyboard*, uint32_t, wl_surface* surface, wl_array*) {
  auto& app = *static_cast<DockApp*>(data);
  // Escape-to-close only applies while the dashboard itself holds keyboard
  // focus (the layer surface requests focus on demand).
  app.dash.kbdFocus = (surface != nullptr && surface == app.dash.panelSurface);
}

void shell_keyboard_leave(void* data, wl_keyboard*, uint32_t, wl_surface*) {
  auto& app = *static_cast<DockApp*>(data);
  app.dash.kbdFocus = false;
}

void shell_keyboard_key(void* data, wl_keyboard*, uint32_t  , uint32_t  , uint32_t keycode,
                             uint32_t state) {
  auto& app = *static_cast<DockApp*>(data);
  if (!app.xkbState) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  if (eh::settings::embed_window_visible()) {
    if (eh::settings::embed_try_keyboard(keycode, state, app.xkbState)) {
      wl_display_flush(app.display);
      return;
    }
  }

  const xkb_keysym_t sym = xkb_state_key_get_one_sym(app.xkbState, keycode + 8);

  if (!app.popupOpen) {
    if (sym == XKB_KEY_Escape && app.dash.kbdFocus) eh::shell::dashboard::dashboard_close(app);
    return;
  }

  if (sym == XKB_KEY_Escape && eh::shell::popup::popup_handle_escape(app)) return;

  if (eh::shell::dock::spotlight::spotlight_handle_keyboard(app, sym, keycode, state)) return;
  if (eh::shell::control_center::handle_keyboard(app, sym, keycode)) return;
  eh::shell::dock::app_drawer::app_drawer_handle_keyboard(app, sym, keycode, state);
}

void shell_keyboard_modifiers(void* data, wl_keyboard*, uint32_t  , uint32_t depressed,
                                   uint32_t latched, uint32_t locked, uint32_t group) {
  auto& app = *static_cast<DockApp*>(data);
  if (!app.xkbState) return;
  const auto gl = static_cast<xkb_layout_index_t>(group);
  xkb_state_update_mask(app.xkbState, depressed, latched, locked, gl, gl, gl);
}

void shell_keyboard_repeat_info(void*, wl_keyboard*, int32_t, int32_t) {}

const wl_keyboard_listener g_shell_keyboard_listener = {
    .keymap = shell_keyboard_keymap,
    .enter = shell_keyboard_enter,
    .leave = shell_keyboard_leave,
    .key = shell_keyboard_key,
    .modifiers = shell_keyboard_modifiers,
    .repeat_info = shell_keyboard_repeat_info,
};

void shell_seat_capabilities(void* data, wl_seat* seat, uint32_t caps) {
  auto& app = *static_cast<DockApp*>(data);
  const bool hasPointer = (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
  const bool hasKeyboard = (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

  if (hasPointer && !app.pointer) {
    app.pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app.pointer, &g_shell_pointer_listener, &app);
  } else if (!hasPointer && app.pointer) {
    wl_pointer_destroy(app.pointer);
    app.pointer = nullptr;
  }

  if (hasKeyboard && !app.keyboard) {
    app.keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app.keyboard, &g_shell_keyboard_listener, &app);
  } else if (!hasKeyboard && app.keyboard) {
    wl_keyboard_destroy(app.keyboard);
    app.keyboard = nullptr;
  }
}
void shell_seat_name(void*  , wl_seat*  , const char*  ) {}
} // namespace
extern const wl_seat_listener g_shell_seat_listener = {
    .capabilities = shell_seat_capabilities,
    .name = shell_seat_name,
};
