#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/desktop/icons/desktop_icons.hpp"
#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/core/desktop_marquee.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/widgets/shared/desktop_widgets_preferences.hpp"
#include "desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.hpp"
#include "ux/settings/data/settings_desktop_widgets_data.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/settings_serialize.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/output/dock_layer_outputs.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "wl/surface/layer_surface.hpp"
#include "wl/core/protocols.hpp"
#include "wl/core/connection.hpp"

#include <sys/mman.h>
#include <cstdlib>
#include <iostream>
#include <time.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

namespace eh::shell::desktop {

void eh_desktop_log(const char*, ...) {}

static void desktop_app_pointer_enter(void* data, wl_pointer*, uint32_t,
                                        wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app) return;
  desktop_pointer_enter(*app, surface, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

static void desktop_app_pointer_leave(void* data, wl_pointer*, uint32_t,
                                        wl_surface*) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app) return;
  desktop_pointer_leave(*app);
}

static void desktop_app_pointer_motion(void* data, wl_pointer*, uint32_t,
                                         wl_fixed_t sx, wl_fixed_t sy) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app) return;
  desktop_pointer_motion(*app, wl_fixed_to_double(sx), wl_fixed_to_double(sy));
}

static void desktop_app_pointer_button(void* data, wl_pointer*, uint32_t,
                                          uint32_t, uint32_t button, uint32_t state) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app) return;
  desktop_pointer_button(*app, button, state);
}

static void desktop_app_pointer_axis(void* data, wl_pointer*, uint32_t,
                                       uint32_t axis, wl_fixed_t value) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app) return;
  if (axis == 0) { // vertical scroll
    const double dy = wl_fixed_to_double(value);
    if (open_with_handle_scroll(*app, 0, dy))
      return;
    if (world_clock_settings_handle_scroll(*app, dy))
      return;
  }
}
static void desktop_app_pointer_frame(void*, wl_pointer*) {}
static void desktop_app_pointer_axis_source(void*, wl_pointer*, uint32_t) {}
static void desktop_app_pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void desktop_app_pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}
static void desktop_app_pointer_axis_value120(void*, wl_pointer*, uint32_t, int32_t) {}
static void desktop_app_pointer_axis_relative_direction(void*, wl_pointer*, uint32_t, uint32_t) {}
#ifdef EH_HAVE_POINTER_WARP
static void desktop_app_pointer_warp(void* data, wl_pointer* p, wl_fixed_t sx, wl_fixed_t sy) {
  desktop_app_pointer_motion(data, p, 0, sx, sy);
}
#endif

static constexpr wl_pointer_listener kDesktopPointerListener = {
  .enter = desktop_app_pointer_enter,
  .leave = desktop_app_pointer_leave,
  .motion = desktop_app_pointer_motion,
  .button = desktop_app_pointer_button,
  .axis = desktop_app_pointer_axis,
  .frame = desktop_app_pointer_frame,
  .axis_source = desktop_app_pointer_axis_source,
  .axis_stop = desktop_app_pointer_axis_stop,
  .axis_discrete = desktop_app_pointer_axis_discrete,
  .axis_value120 = desktop_app_pointer_axis_value120,
  .axis_relative_direction = desktop_app_pointer_axis_relative_direction,
#ifdef EH_HAVE_POINTER_WARP
  .warp = desktop_app_pointer_warp,
#endif
};

static void desktop_app_keyboard_keymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app || !app->xkbContext || format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || fd < 0) {
    if (fd >= 0) close(fd);
    return;
  }
  char* map_str = static_cast<char*>(mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);
  if (map_str == MAP_FAILED) return;
  if (app->xkbKeymap) xkb_keymap_unref(app->xkbKeymap);
  if (app->xkbState) xkb_state_unref(app->xkbState);
  app->xkbKeymap = xkb_keymap_new_from_buffer(app->xkbContext, map_str, size > 0 ? size - 1 : 0,
                                              XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, static_cast<size_t>(size));
  app->xkbState = app->xkbKeymap ? xkb_state_new(app->xkbKeymap) : nullptr;
}

static void desktop_app_keyboard_modifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                           uint32_t latched, uint32_t locked, uint32_t group) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app || !app->xkbState) return;
  xkb_state_update_mask(app->xkbState, depressed, latched, locked, static_cast<xkb_layout_index_t>(group),
                        static_cast<xkb_layout_index_t>(group), static_cast<xkb_layout_index_t>(group));
}

static void desktop_app_keyboard_key(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t keycode, uint32_t state) {
  auto* app = static_cast<DesktopApp*>(data);
  if (!app || state != WL_KEYBOARD_KEY_STATE_PRESSED || !app->worldClockSettingsOpen) return;
  xkb_keysym_t sym = XKB_KEY_NoSymbol;
  char utf8[128] = {0};
  int len = 0;
  if (app->xkbState) {
    sym = xkb_state_key_get_one_sym(app->xkbState, keycode + 8);
    len = xkb_state_key_get_utf8(app->xkbState, keycode + 8, utf8, sizeof(utf8) - 1);
    if (len < 0) len = 0;
    utf8[static_cast<size_t>(len)] = '\0';
  }
  if (world_clock_settings_key(*app, sym, utf8, static_cast<size_t>(len)) && app->display)
    wl_display_flush(app->display);
}

static constexpr wl_keyboard_listener kDesktopKeyboardListener = {
  .keymap = desktop_app_keyboard_keymap,
  .enter = [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {},
  .leave = [](void*, wl_keyboard*, uint32_t, wl_surface*) {},
  .key = desktop_app_keyboard_key,
  .modifiers = desktop_app_keyboard_modifiers,
  .repeat_info = [](void*, wl_keyboard*, int32_t, int32_t) {},
};

static void desktop_app_free_xkb(DesktopApp& app) {
  if (app.xkbState) {
    xkb_state_unref(app.xkbState);
    app.xkbState = nullptr;
  }
  if (app.xkbKeymap) {
    xkb_keymap_unref(app.xkbKeymap);
    app.xkbKeymap = nullptr;
  }
  if (app.xkbContext) {
    xkb_context_unref(app.xkbContext);
    app.xkbContext = nullptr;
  }
}

bool desktop_init_on_display(DesktopApp& app) {
   
  app.wl = std::make_unique<eh::wayland::WaylandConnection>();
  if (!app.wl->connect(false)) {
    std::cerr << "[desktop] Failed to connect own wl_display\n";
    app.wl.reset();
    return false;
  }

  app.display = app.wl->display();
  app.compositor = app.wl->compositor();
  app.shm = app.wl->shm();
  app.seat = app.wl->seat();
  app.layerShell = app.wl->layer_shell();

  if (app.seat) {
    app.pointer = wl_seat_get_pointer(app.seat);
    if (app.pointer)
      wl_pointer_add_listener(app.pointer, &kDesktopPointerListener, &app);
    app.keyboard = wl_seat_get_keyboard(app.seat);
    if (app.keyboard) {
      if (!app.xkbContext) app.xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
      wl_keyboard_add_listener(app.keyboard, &kDesktopKeyboardListener, &app);
    }
  }

  (void)desktop_create_layers(app);

  app.wlError = false;
  return true;
}

void desktop_cleanup(DesktopApp& app) {
    
  if (app.display && wl_display_get_error(app.display)) {
    app.display = nullptr;
    desktop_icons_purge_raster_cache_for(app);
    app.wl.reset();
    app.compositor = nullptr;
    app.shm = nullptr;
    app.seat = nullptr;
    app.pointer = nullptr;
    app.keyboard = nullptr;
    app.layerShell = nullptr;
    desktop_app_free_xkb(app);
    return;
  }
  desktop_clear_layers(app);
  desktop_icons_purge_raster_cache_for(app);
  app.wl.reset();
  app.display = nullptr;
  app.compositor = nullptr;
  app.shm = nullptr;
  app.seat = nullptr;
  app.pointer = nullptr;
  app.keyboard = nullptr;
  app.layerShell = nullptr;
  desktop_app_free_xkb(app);
}

void desktop_pointer_enter(DesktopApp& app, wl_surface* surface, double sx, double sy) {
   
  app.pointerSurface = surface;
  app.pointerX = sx;
  app.pointerY = sy;
  (void)pointer_enter_desktop(app, surface);
}

void desktop_pointer_leave(DesktopApp&) {}

static std::string pointer_output_name(const DesktopApp& app) {
  if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx] &&
      app.layers[app.pointerLayerIdx]->wlOut && app.wl) {
    auto bounds = app.wl->logical_output_bounds();
    for (const auto& b : bounds)
      if (b.output == app.layers[app.pointerLayerIdx]->wlOut) return b.name;
  }
  return {};
}

void desktop_pointer_motion(DesktopApp& app, double sx, double sy) {
   
  app.pointerX = sx;
  app.pointerY = sy;

  if (app.leftButtonDown && app.widgetDragIdx >= 0) {
    const int newX = static_cast<int>(std::round(sx - app.widgetDragOffX));
    const int newY = static_cast<int>(std::round(sy - app.widgetDragOffY));
    app.widgetHost.set_widget_position(app.widgetDragIdx, std::max(0, newX), std::max(0, newY));
    paint_all_layers(app);
    wl_display_flush(app.display);
    return;
  }

  if (app.widgetResizeIdx >= 0) {
    const double newScale = app.widgetResizeStartScale + (sx - app.widgetResizeStartX) / 200.0;
    const double clamped = std::clamp(newScale, 0.25, 4.0);
    app.widgetHost.set_widget_scale(app.widgetResizeIdx, clamped);
    paint_all_layers(app);
    wl_display_flush(app.display);
    return;
  }

  if (app.widgetHost.handle_motion(sx, sy, pointer_output_name(app)) ||
      app.widgetHost.hovered_widget() >= 0) {
    paint_all_layers(app);
    wl_display_flush(app.display);
  }
  (void)pointer_motion_marquee(app);
}

void desktop_pointer_button(DesktopApp& app, uint32_t button, uint32_t state) {
   
  const bool left = (button == 0x110);
  const bool right = (button == 0x111);

  if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
    if (left) {
      app.leftButtonDown = false;
      if (app.widgetDragIdx >= 0) {
        app.widgetDragIdx = -1;
        desktop_widgets_prefs_save(app.widgetHost.configs());
        {
          auto settings = load_settings();
          settings.desktopWidgets = app.widgetHost.configs();
          save_settings(settings);
        }
        wl_display_flush(app.display);
        return;
      }
      (void)pointer_button_left_release_marquee(app, true);
    }
    if (right && app.widgetResizeIdx >= 0) {
      app.widgetResizeIdx = -1;
      desktop_widgets_prefs_save(app.widgetHost.configs());
      {
        auto settings = load_settings();
        settings.desktopWidgets = app.widgetHost.configs();
        save_settings(settings);
      }
      wl_display_flush(app.display);
      return;
    }
    return;
  }

  if (state != WL_POINTER_BUTTON_STATE_PRESSED) return;
  if (!pointer_on_desktop_layer(app)) return;

  if (right) {
    if (app.worldClockSettingsOpen) {
      world_clock_settings_close(app);
      paint_all_layers(app);
      wl_display_flush(app.display);
      return;
    }
    const std::string outName = pointer_output_name(app);
    const int hitIdx = app.widgetHost.hit_test(app.pointerX, app.pointerY, outName);
    if (hitIdx >= 0) {
      app.widgetResizeIdx = hitIdx;
      app.widgetResizeStartX = app.pointerX;
      app.widgetResizeStartScale = app.widgetHost.config_scale(hitIdx);
      return;
    }
    if (!pointer_on_desktop_layer(app)) return;
    app.leftButtonDown = false;
    desktop_workspace_right_press(app);
    return;
  }

  if (left) {
    app.leftButtonDown = true;

    if (app.worldClockSettingsOpen) {
      desktop_left_press_marquee(app);
      wl_display_flush(app.display);
      return;
    }

    const std::string outName = pointer_output_name(app);
    const int hitIdx = app.widgetHost.hit_test(app.pointerX, app.pointerY, outName);
    if (hitIdx >= 0) {
      // Give the widget first chance to consume the click
      if (app.widgetHost.widget_click(hitIdx, app.pointerX, app.pointerY)) {
        wl_display_flush(app.display);
        return;
      }
      const auto& cfgs = app.widgetHost.configs();
      if (static_cast<size_t>(hitIdx) < cfgs.size()) {
        app.widgetDragIdx = hitIdx;
        app.widgetDragOffX = app.pointerX - static_cast<double>(cfgs[static_cast<size_t>(hitIdx)].posX);
        app.widgetDragOffY = app.pointerY - static_cast<double>(cfgs[static_cast<size_t>(hitIdx)].posY);
        wl_display_flush(app.display);
        return;
      }
    }

    desktop_left_press_marquee(app);
    wl_display_flush(app.display);
  }
}

bool desktop_create_layers(DesktopApp& app) { return create_layers(app); }
void desktop_clear_layers(DesktopApp& app) { clear_layers(app); }

}

