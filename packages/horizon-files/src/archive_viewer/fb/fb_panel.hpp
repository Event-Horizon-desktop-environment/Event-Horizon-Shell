#pragma once

#include "archive_viewer/fb/fb_icons.hpp"

#include <cairo/cairo.h>

#include <cstdint>
#include <string>
#include <vector>

namespace archive_viewer {

enum class FileType {
  Folder,
  Image,
  Audio,
  Video,
  Text,
  Markdown,
  Code,
  Document,
  Font,
  Archive,
  Executable,
  Web,
  File,
};

struct FbEntry {
  std::string name;
  std::string path;
  uint64_t size = 0;
  int64_t mtime = 0;
  bool is_dir = false;
  bool is_hidden = false;
  FileType type = FileType::File;
};

struct FbPanel {
  int x = 0, y = 0, w = 0, h = 0;
  bool active = false;

  // Browser state
  std::string current_path;
  std::vector<FbEntry> entries;
  std::vector<int> visible;
  int scroll_px = 0;
  int content_h = 0;
  int hover_idx = -1;
  int selected_idx = -1;

  // Picker mode
  bool pick_mode = false;
  bool pick_file = false; // true = file picker, false = directory picker
  std::string result;

  // Layout (computed during paint)
  int sidebar_w = 200;
  int top_bar_h = 40;
  int status_h = 36;
  int pick_bar_h = 44;
  int row_h = 32;

  // Hover for buttons
  bool back_hover = false;
  bool select_hover = false;
  bool cancel_hover = false;
  int side_hover_idx = -1;

  // Mouse
  double mouse_x = 0;
  double mouse_y = 0;

  // Icon theme loader
  IconLoader icons;

  // Opacity
  double bg_opacity = 0.85;
  double panel_opacity = 0.92;

  void reset();
  void init();
  void navigate_to(const std::string& path);
  void navigate_up();
  void reload();

  // Draw into cr at (x,y) with (w,h)
  void paint(cairo_t* cr);

  // Events (coords relative to panel's x,y)
  void on_motion(int mx, int my);
  void on_click(int mx, int my, int button);
  void on_scroll(int mx, int my, double dx, double dy);
  void on_key(uint32_t sym);
};

} // namespace archive_viewer
