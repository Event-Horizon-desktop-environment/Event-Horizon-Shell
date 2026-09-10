#pragma once

#include <array>
#include <cairo/cairo.h>

#include <string>
#include <vector>

struct DesktopApp;

namespace eh::shell::desktop {

struct OpenWithEntry {
  std::string desktop_id;
  std::string name;
};

struct OpenWithState {
  bool open = false;
  size_t layerIdx = 0;
  double x = 0, y = 0, w = 0, h = 0;
  int hoverIdx = -1;
  int selectedIdx = -1;
  int scrollOffset = 0;
  int exactCount = 0;
  int familyCount = 0;
  bool setDefault = false;
  std::string filePath;
  std::string mimeType;
  std::vector<OpenWithEntry> apps;

  // Hit rectangles for interactive elements
  std::array<double, 4> hitClose{};
  std::array<double, 4> hitCancel{};
  std::array<double, 4> hitOpen{};
  std::array<double, 4> hitDefault{};
};

void open_with_open(DesktopApp& app, const std::string& abs_path);
void open_with_close(DesktopApp& app);

void paint_open_with(DesktopApp& app, cairo_t* cr);

bool open_with_pointer_motion(DesktopApp& app);
bool open_with_handle_left_press(DesktopApp& app);
bool open_with_handle_scroll(DesktopApp& app, double dx, double dy);

}
