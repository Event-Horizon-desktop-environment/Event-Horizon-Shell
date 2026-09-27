#pragma once

// Shared text-measure fast path for m3 Label/Glyph.
//
// measureExtents() used to create AND destroy a 1x1 cairo surface + context
// per call (Button::setGeometry + drawContent measure every label twice per
// frame). This keeps one thread-local scratch context and a small bounded
// extent cache for short static labels, so repeats are a hash lookup.

#include <cairo/cairo.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace m3::measure {

struct Scratch {
  cairo_surface_t* surf = nullptr;
  cairo_t* cr = nullptr;
  ~Scratch() {
    if (cr) cairo_destroy(cr);
    if (surf) cairo_surface_destroy(surf);
  }
};

inline cairo_t* scratch_cr() {
  thread_local Scratch s;
  if (!s.surf) {
    s.surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    s.cr = cairo_create(s.surf);
  }
  return s.cr;
}

class ExtentCache {
 public:
  static ExtentCache& instance() {
    static ExtentCache c;
    return c;
  }

  bool lookup(const std::string& key, float& w, float& h) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = map_.find(key);
    if (it == map_.end()) return false;
    w = it->second.first;
    h = it->second.second;
    return true;
  }

  void store(const std::string& key, float w, float h) {
    std::lock_guard<std::mutex> lk(mu_);
    if (map_.size() >= 512) map_.clear();
    map_[key] = {w, h};
  }

 private:
  std::mutex mu_;
  std::unordered_map<std::string, std::pair<float, float>> map_;
};

}  // namespace m3::measure
