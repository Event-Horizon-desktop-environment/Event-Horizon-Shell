#pragma once

#include <cairo/cairo.h>
#include <sdbus-c++/sdbus-c++.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eh::tray {

struct TrayItem {
  std::string service;
  std::string path;
  std::string id;
  std::string title;
  std::string iconName;
  int pixW = 0;
  int pixH = 0;
  std::vector<uint8_t> pixData{};
  std::vector<uint8_t> pixDataCairo{};
  cairo_surface_t* pixSurface = nullptr;
  bool active = true;
  std::shared_ptr<sdbus::IProxy> proxy;
};

}
