#pragma once

#include <string>
#include <functional>

struct wl_seat;
struct wl_display;
struct wl_registry;

namespace hs::core {

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
