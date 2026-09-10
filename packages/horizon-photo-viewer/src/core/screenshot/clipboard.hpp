#pragma once

#include <string>

struct wl_seat;
struct wl_display;
struct ext_data_control_source_v1;
struct zwlr_data_control_source_v1;

namespace hpv::sc {

class ClipboardService {
public:
  ClipboardService() = default;
  ClipboardService(const ClipboardService&) = delete;
  ClipboardService& operator=(const ClipboardService&) = delete;
  ClipboardService(ClipboardService&&) = delete;
  ClipboardService& operator=(ClipboardService&&) = delete;
  ~ClipboardService();

  bool bind_ext(void* ext_data_control_mgr, wl_seat* seat, wl_display* display);
  bool bind_wlr(void* wlr_data_control_mgr, wl_seat* seat, wl_display* display);
  void cleanup();

  [[nodiscard]] bool is_available() const noexcept;

  bool copy_data(std::string mime_type, std::string data);

  struct SourceUserData {
    std::string data;
    void** source_ptr = nullptr;
  };

  void destroy_source();
  static void ext_source_send(void* user_data, ext_data_control_source_v1*,
                               const char* mime_type, int32_t fd);
  static void ext_source_cancelled(void* user_data, ext_data_control_source_v1* src);
  static void wlr_source_send(void* user_data, zwlr_data_control_source_v1*,
                               const char* mime_type, int32_t fd);
  static void wlr_source_cancelled(void* user_data, zwlr_data_control_source_v1* src);

private:
  void* manager_ = nullptr;
  void* device_ = nullptr;
  void* source_ = nullptr;
  wl_display* display_ = nullptr;
  wl_seat* seat_ = nullptr;
  bool is_ext_ = false;
  bool available_ = false;
};

}
