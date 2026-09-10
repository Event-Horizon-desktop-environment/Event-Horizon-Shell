#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>
#include <array>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace hpv::sc::dialog {

class DialogBase {
public:
  struct Result {
    int response = -1;
    std::vector<std::string> uris;
  };

  DialogBase() = default;
  virtual ~DialogBase() = default;

  DialogBase(const DialogBase&) = delete;
  DialogBase& operator=(const DialogBase&) = delete;
  DialogBase(DialogBase&&) = delete;
  DialogBase& operator=(DialogBase&&) = delete;

  virtual Result run() = 0;
  virtual void cancel() {}

  void set_visible(bool v) { visible_ = v; }
  bool visible() const { return visible_; }

protected:
  bool visible_ = false;
};

inline std::string file_uri_to_local_path(const std::string& uri)
{
  if (uri.rfind("file://", 0) == 0) {
    std::string path = uri.substr(7);
    for (size_t i = 0; i < path.size(); ++i) {
      if (path[i] == '%' && i + 2 < path.size()) {
        auto htoi = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return 0;
        };
        path[i] = static_cast<char>((htoi(path[i + 1]) << 4) | htoi(path[i + 2]));
        path.erase(i + 1, 2);
      }
    }
    return path;
  }
  return uri;
}

}
