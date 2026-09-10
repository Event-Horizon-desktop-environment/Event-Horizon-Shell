#include "desktop_shell/shared/popup/dispatch/popup_dispatch.hpp"

#include "desktop_shell/shared/popup/session/session.hpp"
#include "desktop_shell/shared/popup/paint/finish.hpp"
#include "desktop_shell/shared/popup/caret/caret.hpp"
#include "desktop_shell/shared/popup/surface_fwd.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"
#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"
#include "desktop_shell/widgets/popup/volume_mixer/volume_mixer_popup.hpp"
#include "desktop_shell/widgets/popup/media_player/media_player_popup.hpp"
#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"
#include "desktop_shell/widgets/battery/battery_paint.hpp"
#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "desktop_shell/dock/input/dock_pick.hpp"

#include <wayland-client.h>

namespace eh::shell::popup {

bool popup_handle_click(DockApp& app, double x, double y, uint32_t serial) {
  switch (app.popupKind) {
    case DockApp::PopupKind::Calendar:
      eh::shell::dock::popup::calendar::dock_calendar_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::Calendar;
      break;
    case DockApp::PopupKind::Weather:
      eh::shell::dock::popup::weather::dock_weather_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::Weather;
      break;
    case DockApp::PopupKind::VolumeMixer:
      eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::VolumeMixer;
      break;
    case DockApp::PopupKind::MediaPlayer:
      eh::shell::dock::popup::media_player::dock_media_player_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::MediaPlayer;
      break;
    case DockApp::PopupKind::Vpn:
      eh::shell::dock::popup::vpn::dock_vpn_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::Vpn;
      break;
    case DockApp::PopupKind::Battery:
      eh::widgets::dock_battery_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::Battery;
      break;
    case DockApp::PopupKind::Bluetooth:
      eh::widgets::dock_bluetooth_popup_handle_click(app, x, y, serial);
      app.popupDismissedThisPress = DockApp::PopupKind::Bluetooth;
      break;
    default:
      return false;
  }
  popup_draw_surface(app);
  wl_display_flush(app.display);
  return true;
}

bool popup_handle_motion(DockApp& app) {
  if (!app.popupOpen) return false;
  if (app.popupKind == DockApp::PopupKind::PowerConfirm) {
    if (app.pointerSurface != app.popupSurface) return false;
    app.popupMotionDirty = true;
    eh::shell::dock::dock_popup_queue_followup_frame(app);
    wl_display_flush(app.display);
    return true;
  }
  if (app.popupKind != DockApp::PopupKind::VolumeMixer &&
      app.popupKind != DockApp::PopupKind::MediaPlayer) return false;
  if (app.pointerSurface != app.popupSurface) return false;
  popup_draw_surface(app);
  wl_display_flush(app.display);
  return true;
}

bool popup_handle_escape(DockApp& app) {
  switch (app.popupKind) {
    case DockApp::PopupKind::Tray:
    case DockApp::PopupKind::App:
    case DockApp::PopupKind::Calendar:
    case DockApp::PopupKind::Weather:
    case DockApp::PopupKind::VolumeMixer:
    case DockApp::PopupKind::MediaPlayer:
    case DockApp::PopupKind::Vpn:
    case DockApp::PopupKind::Battery:
    case DockApp::PopupKind::Bluetooth:
      popup_close(app);
      wl_display_flush(app.display);
      return true;
    default:
      return false;
  }
}

bool popup_keep_open_for_slot(DockApp::PopupKind kind, int slotKind, const std::string& slotKey) {
  using PK = DockApp::PopupKind;
  switch (kind) {
    case PK::ControlCenter:
      return slotKind == static_cast<int>(PickSlot::Kind::ControlCenter) ||
             slotKind == static_cast<int>(PickSlot::Kind::Weather);
    case PK::VolumeMixer:
      return slotKind == static_cast<int>(PickSlot::Kind::VolumeMixer);
    case PK::Vpn:
      return slotKind == static_cast<int>(PickSlot::Kind::Vpn);
    case PK::Battery:
      return slotKind == static_cast<int>(PickSlot::Kind::Battery);
    case PK::Bluetooth:
      return slotKind == static_cast<int>(PickSlot::Kind::Bluetooth);
    case PK::AppMenu:
      return slotKind == static_cast<int>(PickSlot::Kind::AppMenu) ||
             slotKind == static_cast<int>(PickSlot::Kind::AppDrawer) ||
             slotKind == static_cast<int>(PickSlot::Kind::Smenu) ||
             slotKey == kSlotKeyAppMenu || slotKey == kSlotKeyAppDrawer;
    case PK::Spotlight:
      return slotKind == static_cast<int>(PickSlot::Kind::Spotlight) ||
             slotKey == kSlotKeySpotlight;
    default:
      return false;
  }
}

bool popup_dispatch_paint(DockApp& app, cairo_t* cr) {
  const auto& sc = eh::config::shell_config_snapshot();
  switch (app.popupKind) {
    case DockApp::PopupKind::Calendar:
      eh::shell::dock::popup::calendar::dock_calendar_popup_paint(app, cr, sc);
      break;
    case DockApp::PopupKind::Weather:
      eh::shell::dock::popup::weather::dock_weather_popup_paint(app, cr, sc);
      break;
    case DockApp::PopupKind::VolumeMixer:
      eh::shell::dock::popup::volume_mixer::dock_volume_mixer_popup_paint(app, cr, sc);
      break;
    case DockApp::PopupKind::MediaPlayer:
      eh::shell::dock::popup::media_player::dock_media_player_popup_paint(app, cr, sc);
      break;
    case DockApp::PopupKind::Vpn:
      eh::shell::dock::popup::vpn::dock_vpn_popup_paint(app, cr, sc);
      break;
    case DockApp::PopupKind::Battery:
      eh::widgets::dock_battery_popup_paint(app.pointerX, app.pointerY, cr, sc);
      break;
    case DockApp::PopupKind::Bluetooth:
      eh::widgets::dock_bluetooth_popup_paint(app.pointerX, app.pointerY, cr, sc);
      break;
    default:
      return false;
  }
  cairo_restore(cr);
  popup_finish_draw(app, true, false, false);
  return true;
}

bool popup_dispatch_slot(DockApp& app, int slotKind, int slotCenterX, uint32_t serial) {
  using PK = PickSlot::Kind;
  auto k = static_cast<PK>(slotKind);
  switch (k) {
    case PK::VolumeMixer:
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::VolumeMixer) {
        popup_close(app);
        return true;
      }
      popup_open_volume_mixer(app, slotCenterX, serial);
      return true;
    case PK::Vpn:
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::Vpn) {
        popup_close(app);
        return true;
      }
      popup_open_vpn(app, slotCenterX, serial);
      return true;
    case PK::Battery:
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::Battery) {
        popup_close(app);
        return true;
      }
      popup_open_battery(app, slotCenterX, serial);
      return true;
    case PK::Bluetooth:
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::Bluetooth) {
        popup_close(app);
        return true;
      }
      popup_open_bluetooth(app, slotCenterX, serial);
      return true;
    case PK::ControlCenter:
      if (app.popupOpen && app.popupKind == DockApp::PopupKind::ControlCenter) {
        popup_close(app);
        return true;
      }
      popup_open_control_center(app, slotCenterX, serial);
      return true;
    default:
      return false;
  }
}

}
