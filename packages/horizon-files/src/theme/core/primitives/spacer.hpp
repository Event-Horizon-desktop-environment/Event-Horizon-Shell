#pragma once

#include <cstdint>

namespace m3 {

// Flexible empty space for layout. Occupies available space in Flex containers.
// Invisible; does not paint.
class Spacer {
public:
  Spacer() = default;

  void setMinSize(float w, float h) { minW_ = w; minH_ = h; }
  void setGeometry(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }

  [[nodiscard]] float x() const noexcept { return x_; }
  [[nodiscard]] float y() const noexcept { return y_; }
  [[nodiscard]] float width() const noexcept { return w_; }
  [[nodiscard]] float height() const noexcept { return h_; }
  [[nodiscard]] float minWidth() const noexcept { return minW_; }
  [[nodiscard]] float minHeight() const noexcept { return minH_; }

  void paint(cairo_t*) const {}

private:
  float minW_ = 0.0f, minH_ = 0.0f;
  float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace m3
