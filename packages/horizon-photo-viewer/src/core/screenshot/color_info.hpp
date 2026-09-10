#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct wl_output;
struct wl_display;
struct wp_color_manager_v1;

namespace hpv::sc {

struct OutputColorInfo {
  bool is_hdr = false;
  bool is_10bit = false;
  uint32_t max_lum = 0;
};

OutputColorInfo query_output_color_info(wl_display* display, wp_color_manager_v1* mgr, wl_output* output);

}
