#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

struct ext_foreign_toplevel_handle_v1;
struct ext_foreign_toplevel_image_capture_source_manager_v1;
struct ext_image_copy_capture_manager_v1;
struct ext_output_image_capture_source_manager_v1;
struct wl_display;
struct wl_output;
struct wl_shm;
struct wp_color_manager_v1;
struct zwlr_screencopy_manager_v1;
struct zwp_linux_dmabuf_v1;

namespace hpv::sc {

struct BatchedCaptureOutput {
  wl_output* output = nullptr;
  int logical_x = 0, logical_y = 0;
  int logical_w = 0, logical_h = 0;
  int native_w = 0, native_h = 0;
  std::vector<unsigned char> rgba_pixels;
  bool captured = false;
  bool is_hdr = false;
  uint32_t max_lum = 0;
  std::vector<float> hdr_linear_rgb;
};

[[nodiscard]] bool screencopy_output_to_png(wl_display* display, zwlr_screencopy_manager_v1* screencopy_mgr,
                                             wl_shm* shm, wl_output* output, bool overlay_cursor,
                                             std::string_view png_path);

[[nodiscard]] bool screencopy_output_region_to_png(wl_display* display, zwlr_screencopy_manager_v1* screencopy_mgr,
                                                    wl_shm* shm, wl_output* output, int32_t region_x, int32_t region_y,
                                                    int32_t region_w, int32_t region_h, bool overlay_cursor,
                                                    std::string_view png_path);

[[nodiscard]] bool ext_capture_output_to_png(wl_display* display,
                                              ext_image_copy_capture_manager_v1* copy_mgr,
                                              ext_output_image_capture_source_manager_v1* source_mgr,
                                              wl_shm* shm, wl_output* output, bool overlay_cursor,
                                              wp_color_manager_v1* color_mgr, std::string_view png_path);

[[nodiscard]] bool ext_capture_toplevel_to_png(wl_display* display,
                                                 ext_image_copy_capture_manager_v1* copy_mgr,
                                                 ext_foreign_toplevel_image_capture_source_manager_v1* toplevel_src_mgr,
                                                 ext_foreign_toplevel_handle_v1* toplevel,
                                                 wl_shm* shm, bool overlay_cursor,
                                                 std::string_view png_path);

[[nodiscard]] bool batch_capture_outputs(
    wl_display* display,
    zwlr_screencopy_manager_v1* screencopy_mgr,
    wl_shm* shm,
    std::vector<BatchedCaptureOutput>& outputs,
    bool overlay_cursor = false,
    wp_color_manager_v1* color_mgr = nullptr,
    zwp_linux_dmabuf_v1* linux_dmabuf = nullptr);


struct OutputColorInfo {
  bool is_hdr = false;
  bool is_10bit = false;
  uint32_t max_lum = 0;
};

[[nodiscard]] OutputColorInfo query_output_color_info(wl_display* display, wp_color_manager_v1* mgr, wl_output* output);

#ifdef EH_HAVE_LIBJXL
[[nodiscard]] bool write_jxl_hdr(const float* rgb, int w, int h, const char* path, uint32_t max_lum);
#endif
#ifdef EH_HAVE_LIBPNG
[[nodiscard]] bool write_png16_from_linear(const float* rgb, int w, int h, const char* path);
#endif

[[nodiscard]] bool batch_capture_outputs_ext(
    wl_display* display,
    ext_image_copy_capture_manager_v1* copy_mgr,
    ext_output_image_capture_source_manager_v1* source_mgr,
    wl_shm* shm,
    std::vector<BatchedCaptureOutput>& outputs,
    bool overlay_cursor = false,
    wp_color_manager_v1* color_mgr = nullptr);

}
