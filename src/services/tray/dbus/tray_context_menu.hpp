#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <string>

namespace eh::shell::dock::tray_menu {

std::string dock_get_menu_object_path(sdbus::IProxy& statusNotifierItemProxy);

// Bounds for interactive (input-thread) tray menu I/O. Right-click menu
// fetches run on the Wayland input thread, so each D-Bus round-trip stalls
// all input handling while in flight. Steam's StatusNotifierItem is
// notoriously slow here (its DBusMenu typically needs AboutToShow first and
// its Menu property read can stall), which turned a right-click into a
// multi-second freeze when the connection-wide 2s timeout applied to each of
// several sequential calls. Interactive paths must use these short per-call
// timeouts and never hold tray locks across I/O.
inline constexpr std::chrono::milliseconds kTrayMenuCallTimeout{750};
inline constexpr std::chrono::milliseconds kTrayAboutToShowTimeout{400};

// Short-timeout read of the SNI "Menu" property via an explicit Get call so
// the per-call timeout applies (IProxy::getProperty has no timeout knob and
// would use the connection-wide timeout).
std::string tray_menu_path_interactive(sdbus::IConnection& bus,
                                       const std::string& service,
                                       const std::string& itemPath);

// Best-effort AboutToShow(0) before GetLayout, per the dbusmenu spec. Steam
// and other lazy menus only populate after this; failures are ignored.
void tray_menu_about_to_show(sdbus::IProxy& menu);

// Per-spec right-click fallback: ask the item to open its own context menu
// at (x, y). Used when the DBusMenu can't be read (e.g. a Steam stall).
void tray_context_menu_fallback(sdbus::IConnection& bus,
                                const std::string& service,
                                const std::string& itemPath,
                                int x, int y);

}
