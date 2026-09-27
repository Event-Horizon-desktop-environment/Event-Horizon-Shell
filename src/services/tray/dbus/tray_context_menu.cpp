#include "services/tray/dbus/tray_context_menu.hpp"

#include <cstdint>
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

std::string tray_menu_path_interactive(sdbus::IConnection& bus,
                                       const std::string& service,
                                       const std::string& itemPath) {
  try {
    auto item = sdbus::createProxy(bus, sdbus::ServiceName{service},
                                   sdbus::ObjectPath{itemPath});
    sdbus::Variant v;
    item->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withTimeout(kTrayMenuCallTimeout)
        .withArguments(std::string{"org.kde.StatusNotifierItem"}, std::string{"Menu"})
        .storeResultsTo(v);
    try {
      return v.get<sdbus::ObjectPath>();
    } catch (const sdbus::Error&) {
    }
    try {
      return v.get<std::string>();
    } catch (const sdbus::Error&) {
    }
  } catch (const std::exception&) {
  }
  return {};
}

void tray_menu_about_to_show(sdbus::IProxy& menu) {
  try {
    menu.callMethod("AboutToShow")
        .onInterface("com.canonical.dbusmenu")
        .withTimeout(kTrayAboutToShowTimeout)
        .withArguments(int32_t{0});
  } catch (const std::exception&) {
  }
}

void tray_context_menu_fallback(sdbus::IConnection& bus,
                                const std::string& service,
                                const std::string& itemPath,
                                int x, int y) {
  try {
    auto proxy = sdbus::createProxy(bus, sdbus::ServiceName{service},
                                    sdbus::ObjectPath{itemPath});
    proxy->callMethod("ContextMenu")
        .onInterface("org.kde.StatusNotifierItem")
        .withTimeout(kTrayMenuCallTimeout)
        .withArguments(static_cast<int32_t>(x), static_cast<int32_t>(y));
  } catch (const std::exception&) {
  }
}

}
