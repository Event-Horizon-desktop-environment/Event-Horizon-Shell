#pragma once

namespace eh::shell_log {

template<typename... Ts>
void mpris_dbus(Ts&&...) {}

template<typename... Ts>
void dock(Ts&&...) {}

template<typename... Ts>
void dock_mpris(Ts&&...) {}

template<typename... Ts>
void panel_mpris(Ts&&...) {}

template<typename... Ts>
void dbus_tray(Ts&&...) {}

template<typename... Ts>
void notifications(Ts&&...) {}

template<typename... Ts>
void notif_verbose(Ts&&...) {}

}
