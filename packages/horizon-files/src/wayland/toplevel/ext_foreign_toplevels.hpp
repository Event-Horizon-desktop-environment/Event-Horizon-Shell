#pragma once
// Stub for standalone file browser

#include "wayland/core/protocols.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ext_foreign_toplevel_list_v1;
struct wl_display;

namespace eh::wayland {

class ExtForeignToplevels {
public:
  struct Toplevel {
    void* handle = nullptr;
    std::string appId;
    std::string title;
    std::string identifier;
    uint64_t serial = 0;
  };

  ExtForeignToplevels() = default;
  ExtForeignToplevels(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels& operator=(const ExtForeignToplevels&) = delete;
  ExtForeignToplevels(ExtForeignToplevels&&) = delete;
  ExtForeignToplevels& operator=(ExtForeignToplevels&&) = delete;
  ~ExtForeignToplevels() = default;

  void bind(ext_foreign_toplevel_list_v1*, wl_display*) {}
  void shutdown() {}

  [[nodiscard]] size_t size() const { return 0; }
  [[nodiscard]] const std::vector<Toplevel>& list() const { static std::vector<Toplevel> empty; return empty; }

  auto begin() { return toplevels_.begin(); }
  auto end() { return toplevels_.end(); }
  auto begin() const { return toplevels_.begin(); }
  auto end() const { return toplevels_.end(); }

private:
  std::vector<Toplevel> toplevels_;
};

}
