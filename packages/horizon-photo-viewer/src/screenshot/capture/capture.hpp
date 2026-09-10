#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct wl_display;
struct wl_output;
struct ext_foreign_toplevel_handle_v1;

namespace hpv {
class WaylandConnection;
struct LogicalOutputBounds;
}

namespace hpv::sc {

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

struct CaptureResult {
  std::vector<unsigned char> pixels;
  int width = 0;
  int height = 0;
};

struct OutputInfo {
  wl_output* output = nullptr;
  std::string name;
  int global_x = 0, global_y = 0;
  int width = 0, height = 0;
  bool is_hdr = false;
  bool is_10bit = false;
  uint32_t max_lum = 0;
};

struct HdrData {
  bool valid = false;
  std::vector<float> linear_rgb;
  int width = 0;
  int height = 0;
  uint32_t max_lum = 0;
};

std::vector<OutputInfo> list_outputs(hpv::WaylandConnection& wl);

[[nodiscard]] bool capture_focused_window(
    hpv::WaylandConnection& wl,
    const std::string& out_path);

[[nodiscard]] bool capture_window_by_handle(
    hpv::WaylandConnection& wl,
    ext_foreign_toplevel_handle_v1* handle,
    const std::string& out_path);

[[nodiscard]] bool capture_screen(
    hpv::WaylandConnection& wl,
    const std::string& out_path);

[[nodiscard]] bool capture_output(
    hpv::WaylandConnection& wl,
    wl_output* output,
    const std::string& out_path,
    HdrData* out_hdr = nullptr);

[[nodiscard]] bool capture_all_screens(
    hpv::WaylandConnection& wl,
    const std::string& out_path,
    HdrData* out_hdr = nullptr);

[[nodiscard]] bool capture_selection_interactive(
    hpv::WaylandConnection& wl,
    const std::vector<hpv::LogicalOutputBounds>& all_bounds,
    const std::string& out_path);

CapturedImage load_capture(const std::string& path);

bool write_preview_from_hdr(const HdrData& hdr, const std::string& out_path);

bool save_composed_png(const CapturedImage& img, const FrameSettings& frame, const std::string& out_path);

}
