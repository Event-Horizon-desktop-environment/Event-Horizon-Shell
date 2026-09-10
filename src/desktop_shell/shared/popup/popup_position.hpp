#pragma once

struct PopupPositionInput {
  int anchor_x = 0;
  int popup_w = 0;
  int popup_h = 0;
  int output_w = 0;
  int layer_w = 0;
  int layer_origin_x = 0;
  int bottom_clearance = 0;
};

struct PopupPositionOutput {
  int margin_left = 0;
  int margin_bottom = 0;
};

[[nodiscard]] PopupPositionOutput compute_popup_position(const PopupPositionInput& in);
