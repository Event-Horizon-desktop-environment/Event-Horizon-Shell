#pragma once

#include "wayland/core/protocols.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct wl_seat;
struct wl_display;
struct wl_registry;

namespace eh::wayland {

struct DataControlOps {
  const char* managerInterfaceName = nullptr;

  void* (*bindManager)(wl_registry* registry, uint32_t name, uint32_t version) = nullptr;
  void (*destroyManager)(void* manager) = nullptr;

  void* (*getDataDevice)(void* manager, wl_seat* seat) = nullptr;
  void (*destroyDevice)(void* device) = nullptr;

  void* (*createDataSource)(void* manager) = nullptr;
  void (*destroySource)(void* source) = nullptr;
  void (*sourceOffer)(void* source, const char* mimeType) = nullptr;

  void (*deviceSetSelection)(void* device, void* source) = nullptr;
  void (*deviceSetPrimarySelection)(void* device, void* source) = nullptr;

  void (*destroyOffer)(void* offer) = nullptr;
  void (*offerReceive)(void* offer, const char* mimeType, int fd) = nullptr;
};

[[nodiscard]] const DataControlOps* ext_data_control_ops();
[[nodiscard]] const DataControlOps* wlr_data_control_ops();

struct ClipboardFiles {
  bool is_cut = false;
  std::vector<std::string> paths;
};

class ClipboardService {
public:
  using ChangedCb = std::function<void()>;

  ClipboardService() = default;
  ClipboardService(const ClipboardService&) = delete;
  ClipboardService& operator=(const ClipboardService&) = delete;
  ClipboardService(ClipboardService&&) = delete;
  ClipboardService& operator=(ClipboardService&&) = delete;
  ~ClipboardService();

  bool bind(void* manager, const DataControlOps* ops, wl_seat* seat, wl_display* display = nullptr);
  void cleanup();

  [[nodiscard]] bool is_available() const noexcept;
  void set_changed_cb(ChangedCb cb) { changedCb_ = std::move(cb); }

  [[nodiscard]] bool owns_selection() const noexcept { return outgoingSource_ != nullptr; }

  void set_on_source_send(std::function<void()> cb) { onSourceSend_ = std::move(cb); }

  bool copy_data(std::string mime_type, std::string data);
  bool copy_text(std::string text);
  bool copy_files(bool cut, const std::vector<std::string>& abs_paths);

  // Set multiple MIME types at once (clears previous data)
  bool copy_data_multi(std::vector<std::pair<std::string, std::string>> items);

  [[nodiscard]] std::string read_selection_text(wl_display* display = nullptr);
  [[nodiscard]] std::string read_data(const std::string& mime_type, wl_display* display = nullptr);
  [[nodiscard]] ClipboardFiles read_files(wl_display* display = nullptr);

  [[nodiscard]] bool selection_has_mime(const std::string& mime_type) const;

  // Protocol callback entrypoints used by the C-style listeners
  void handle_data_offer(void* offer);
  void handle_offer_mime_type(const char* mime);
  void handle_selection(void* offer);
  void handle_device_finished();
  void handle_source_send(const char* mime, int fd);
  void handle_source_cancelled();

private:
  static std::string file_uri_for_path(const std::string& abs_path);
  static std::string path_from_file_uri(const std::string& uri);
  static std::string canonical_abs_path(const std::string& path);

  void build_source_and_set_selection();

  // ext-data-control-v1 listeners
  static void ext_data_offer(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer);
  static void ext_selection(void* data, ext_data_control_device_v1*, ext_data_control_offer_v1* offer);
  static void ext_finished(void* data, ext_data_control_device_v1*);
  static void ext_primary_selection(void*, ext_data_control_device_v1*, ext_data_control_offer_v1*) {}
  static constexpr ext_data_control_device_v1_listener kExtDeviceListener_ = {
      .data_offer = ext_data_offer,
      .selection = ext_selection,
      .finished = ext_finished,
      .primary_selection = ext_primary_selection,
  };
  static void ext_offer_offer(void* data, ext_data_control_offer_v1*, const char* mime);
  static constexpr ext_data_control_offer_v1_listener kExtOfferListener_ = {
      .offer = ext_offer_offer,
  };
  static void ext_source_send(void* data, ext_data_control_source_v1*, const char* mime, int32_t fd);
  static void ext_source_cancelled(void* data, ext_data_control_source_v1*);
  static constexpr ext_data_control_source_v1_listener kExtSourceListener_ = {
      .send = ext_source_send,
      .cancelled = ext_source_cancelled,
  };

  // wlr-data-control-unstable-v1 listeners
  static void wlr_data_offer(void* data, zwlr_data_control_device_v1*, zwlr_data_control_offer_v1* offer);
  static void wlr_selection(void* data, zwlr_data_control_device_v1*, zwlr_data_control_offer_v1* offer);
  static void wlr_finished(void* data, zwlr_data_control_device_v1*);
  static void wlr_primary_selection(void*, zwlr_data_control_device_v1*, zwlr_data_control_offer_v1*) {}
  static constexpr zwlr_data_control_device_v1_listener kWlrDeviceListener_ = {
      .data_offer = wlr_data_offer,
      .selection = wlr_selection,
      .finished = wlr_finished,
      .primary_selection = wlr_primary_selection,
  };
  static void wlr_offer_offer(void* data, zwlr_data_control_offer_v1*, const char* mime);
  static constexpr zwlr_data_control_offer_v1_listener kWlrOfferListener_ = {
      .offer = wlr_offer_offer,
  };
  static void wlr_source_send(void* data, zwlr_data_control_source_v1*, const char* mime, int32_t fd);
  static void wlr_source_cancelled(void* data, zwlr_data_control_source_v1*);
  static constexpr zwlr_data_control_source_v1_listener kWlrSourceListener_ = {
      .send = wlr_source_send,
      .cancelled = wlr_source_cancelled,
  };

  void on_offer_mime(std::string_view mime);
  void notify_changed();

  [[nodiscard]] bool selection_supports_text() const;
  [[nodiscard]] std::string receive_offer_as_mime(const std::string& mime, wl_display* display) const;
  [[nodiscard]] std::string receive_offer_as_text(wl_display* display) const;

  wl_display* display_ = nullptr;
  wl_seat* seat_ = nullptr;
  void* manager_ = nullptr;
  const DataControlOps* ops_ = nullptr;
  void* device_ = nullptr;

  // For dispatching C thunks to the correct protocol listener
  ext_data_control_device_v1* extDev_ = nullptr;
  zwlr_data_control_device_v1* wlrDev_ = nullptr;

  void* selectionOffer_ = nullptr;
  std::vector<std::string> selectionMimes_{};

  std::unordered_map<std::string, std::vector<char>> outgoingData_{};
  void* outgoingSource_ = nullptr;

  ChangedCb changedCb_{};
  std::function<void()> onSourceSend_{};
};

}
