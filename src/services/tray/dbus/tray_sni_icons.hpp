#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>

namespace eh::shell::dock::tray::sni {

void dock_update_item_from_proxy(sdbus::IProxy& proxy, std::string& id, std::string& title, std::string& iconName,
                                 int& pixW, int& pixH, std::vector<uint8_t>& pixData,
                                 std::vector<uint8_t>& pixDataCairo, cairo_surface_t*& pixSurface);

inline void update_item_from_proxy(sdbus::IProxy& proxy, std::string& id, std::string& title, std::string& iconName,
                                   int& pixW, int& pixH, std::vector<uint8_t>& pixData,
                                   std::vector<uint8_t>& pixDataCairo, cairo_surface_t*& pixSurface) {
   
  dock_update_item_from_proxy(proxy, id, title, iconName, pixW, pixH, pixData, pixDataCairo, pixSurface);
}

}

namespace eh::tray::sni {
using namespace eh::shell::dock::tray::sni;
}
