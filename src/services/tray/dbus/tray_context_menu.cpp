#include "services/tray/dbus/tray_context_menu.hpp"

#include <map>

namespace eh::shell::dock::tray_menu {

std::string dock_get_menu_object_path(sdbus::IProxy& p) {
   
  constexpr auto kItemIface = "org.kde.StatusNotifierItem";

  try {
    const sdbus::Variant v = p.getProperty("Menu").onInterface(kItemIface);
    try {
      return v.get<std::string>();
    } catch (const sdbus::Error&) {
    }
    try {
      return v.get<sdbus::ObjectPath>();
    } catch (const sdbus::Error&) {
    }
  } catch (const sdbus::Error&) {
  }

  try {
    std::map<std::string, sdbus::Variant> all;
    p.callMethod("GetAll")
      .onInterface("org.freedesktop.DBus.Properties")
      .withArguments(std::string(kItemIface))
      .storeResultsTo(all);
    const auto it = all.find("Menu");
    if (it == all.end()) return {};
    try {
      return it->second.get<std::string>();
    } catch (const sdbus::Error&) {
    }
    try {
      return it->second.get<sdbus::ObjectPath>();
    } catch (const sdbus::Error&) {
    }
  } catch (const sdbus::Error&) {
  }

  return {};
}

}
