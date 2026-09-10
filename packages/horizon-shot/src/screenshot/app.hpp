#pragma once

#include "core/shell_config.hpp"
#include "core/icon_cache.hpp"
#include "core/shm_buffer.hpp"
#include "core/wayland_connection.hpp"
#include "core/wayland_seat.hpp"
#include "core/clipboard.hpp"
#include "screenshot/capture/capture.hpp"

#include "ext-foreign-toplevel-list-v1-client-protocol.h"

#include <cstdint>
#include <string>
#include <vector>

struct wl_buffer;
struct wl_compositor;
struct wl_shm;
struct wl_surface;
struct xdg_surface;
struct xdg_toplevel;

namespace hs::screenshot {

enum class Source : uint8_t {
  Focused = 0,
  Window = 1,
  Screen = 2,
  Selection = 3,
};

struct WindowEntry {
  ext_foreign_toplevel_handle_v1* handle = nullptr;
  void* wlr_handle = nullptr;
  std::string appId;
  std::string title;
  std::string identifier;
};

struct FrameSettings {
  int inset = 32;
  int cornerRadius = 24;
  int shadow = 16;
  bool showBorder = false;
  bool includeCursor = false;
  bool hideChrome = false;
};

struct CapturedImage {
  std::vector<unsigned char> pixels;
  int width = 0;
  int height = 0;
  bool valid = false;
};

struct AppOptions {
  enum Mode { Gui, Select, Focused, Screen, Window };
  Mode mode = Gui;
  bool copy = false;
  std::string output_path;
  std::string output_name;
  FrameSettings frame;
  std::string window_selector;
  bool list_windows = false;
  bool capture_hdr = false;
};

struct AppState {
  AppState();
  ~AppState();

  hs::core::WaylandConnection wl{};
  hs::core::WaylandSeat seat{};

  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;

  int width = 960;
  int height = 640;
  int minWidth = 800;
  int minHeight = 540;
  bool running = true;
  bool pendingRedraw = false;

  hs::core::ShmBuffer buf[2];
  int paint_buf = 0;

  hs::config::ChromePaintColors chrome{};
  hs::icons::IconCache iconCache;
  bool haveMatugen = false;

  Source source = Source::Screen;
  int selected_window_idx = -1;
  std::vector<WindowEntry> window_list;
  int selected_output_idx = -1;
  std::vector<OutputInfo> output_list;
  bool capture_all_screens = false;

  FrameSettings frame;

  CapturedImage captured;
  HdrData captured_hdr;
  cairo_surface_t* cached_img = nullptr;

  double pointer_x = 0;
  double pointer_y = 0;
  int hovered_item = -1;
  int hovered_area = 0;
  int pressed_item = -1;
  int pressed_area = 0;

  double zoom = 1.0;
  double pan_x = 0.0;
  double pan_y = 0.0;
  bool dragging = false;
  double drag_start_x = 0.0;
  double drag_start_y = 0.0;
  double drag_pan_x = 0.0;
  double drag_pan_y = 0.0;

  hs::core::ClipboardService clipboard;

  std::string status;
  std::string last_capture_path;

  bool toplevel_available = false;
  int refresh_counter = 0;
};

int run_screenshot_app(bool select_on_launch = false);
int run_screenshot_cli(const AppOptions& opts);

}
