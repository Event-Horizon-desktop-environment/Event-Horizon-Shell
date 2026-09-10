#include "desktop_shell/shared/popup/popup_position.hpp"

#include <algorithm>
#include <cmath>

PopupPositionOutput compute_popup_position(const PopupPositionInput& in) {
   
  PopupPositionOutput out{};
  out.margin_bottom = in.bottom_clearance;

  const int want_left = in.anchor_x - in.popup_w / 2;
  const int max_from_layer = std::max(0, in.layer_w - in.popup_w);
  const int max_from_output =
      in.output_w > 0 ? std::max(0, in.output_w - in.popup_w - in.layer_origin_x) : max_from_layer;
  const int max_want = std::max(max_from_layer, max_from_output);
  out.margin_left = in.layer_origin_x + std::clamp(want_left, 0, max_want);

  return out;
}
