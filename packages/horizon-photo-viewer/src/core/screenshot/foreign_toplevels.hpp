#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ext_foreign_toplevel_list_v1;
struct ext_foreign_toplevel_handle_v1;
struct wl_display;

namespace hpv::sc {

class ExtForeignToplevels {
public:
  struct Toplevel {
    ext_foreign_toplevel_handle_v1* handle = nullptr;
    std::string appId{};
    std::string title{};
    std::string identifier{};
  };

  ExtForeignToplevels() = default;
  ExtForeignToplevels(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels& operator=(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels(ExtForeignToplevels&&) = delete;
  ExtForeignToplevels& operator=(ExtForeignToplevels&&) = delete;
  ~ExtForeignToplevels();

  void bind(ext_foreign_toplevel_list_v1* list, wl_display* display);
  void shutdown();

  [[nodiscard]] size_t size() const { return toplevels_.size(); }
  [[nodiscard]] const std::vector<Toplevel>& list() const { return toplevels_; }
  [[nodiscard]] std::vector<Toplevel>& list() { return toplevels_; }
  auto begin() { return toplevels_.begin(); }
  auto end() { return toplevels_.end(); }
  auto begin() const { return toplevels_.begin(); }
  auto end() const { return toplevels_.end(); }

private:
  static void on_toplevel(void* data, ext_foreign_toplevel_list_v1*, ext_foreign_toplevel_handle_v1* handle);
  static void on_finished(void* data, ext_foreign_toplevel_list_v1*);
  static void on_closed(void* data, ext_foreign_toplevel_handle_v1* handle);
  static void on_done(void* data, ext_foreign_toplevel_handle_v1* handle);
  static void on_title(void* data, ext_foreign_toplevel_handle_v1* handle, const char* title);
  static void on_app_id(void* data, ext_foreign_toplevel_handle_v1* handle, const char* app_id);
  static void on_identifier(void* data, ext_foreign_toplevel_handle_v1* handle, const char* identifier);

  ext_foreign_toplevel_list_v1* list_ = nullptr;
  wl_display* display_ = nullptr;
  std::vector<Toplevel> toplevels_{};
  bool initialSyncDone_ = false;
};

}
