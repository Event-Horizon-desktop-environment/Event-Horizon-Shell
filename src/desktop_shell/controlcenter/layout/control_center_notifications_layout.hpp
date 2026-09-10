#pragma once

#include <algorithm>
#include <cstdint>

namespace eh::shell::dock::control_center {

[[nodiscard]] inline int notifications_tail_height_px(bool has_section, bool expanded, int active_count) {
  if (!has_section) {
    return 0;
  }
  constexpr int row_gap = 12;
  constexpr int header_card = 72;
  if (!expanded) {
    return row_gap + header_card;
  }
  const int rows = std::clamp(std::max(active_count, 1), 1, 8);
  const int panel_body = 40 + rows * 44 + 44;
  return row_gap + header_card + 8 + panel_body;
}

}
