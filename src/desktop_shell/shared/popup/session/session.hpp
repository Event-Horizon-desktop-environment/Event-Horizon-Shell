#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include <wayland-client.h>

#include <cstdint>
#include <string>

enum class AppPopupAction : int32_t {
  CloseOne = 1,
  CloseAll = 2,
  PinToggle = 3,
  MinimizeOne = 4,
  MinimizeAll = 5,
  ToggleMaximize = 6,
  ToggleFullscreen = 7,
};

extern int g_dock_appmenu_trace_last_zone;

[[nodiscard]] bool dock_popup_pointer_on_any_popup_surface(const DockApp& app);

[[nodiscard]] inline bool dock_popup_kind_uses_app_drawer_ui(DockApp::PopupKind k) {
  return k == DockApp::PopupKind::AppMenu;
}

void launch_exec_command(const std::string& execLine);

void popup_close(DockApp& app);
void dock_destroy_app_menu_host_surfaces(DockApp& app);

void popup_open_for_tray(DockApp& app, const DockApp::TrayItem& ti, int anchorX, int anchorY, uint32_t serial);
void popup_open_spotlight(DockApp& app, int anchorX, uint32_t serial);
void popup_open_control_center(DockApp& app, int anchorX, uint32_t serial);
void popup_open_power_confirm(DockApp& app, int powerIdx);
void popup_open_app_menu(DockApp& app, int anchorX, uint32_t serial);
void popup_open_for_trash(DockApp& app, int anchorX, int anchorY, uint32_t serial);
void popup_open_calendar(DockApp& app, int anchorX, uint32_t serial);
void popup_open_weather(DockApp& app, int anchorX, const std::string& instanceId, uint32_t serial);
void popup_open_volume_mixer(DockApp& app, int anchorX, uint32_t serial);
void popup_open_media_player(DockApp& app, int anchorX, uint32_t serial);
void popup_open_vpn(DockApp& app, int anchorX, uint32_t serial);
void popup_open_battery(DockApp& app, int anchorX, uint32_t serial);
void popup_open_bluetooth(DockApp& app, int anchorX, uint32_t serial);
void popup_open_for_app(DockApp& app, const std::string& appKey, zwlr_foreign_toplevel_handle_v1* chosenHandle,
                        int anchorX, int anchorY, uint32_t serial);

void popup_draw_surface(DockApp& app);

[[nodiscard]] bool dock_popup_create_layer_surface_ex(DockApp& app, int anchorLocalX, wl_output* output,
                                                      int marginLeft, int marginBottom);
