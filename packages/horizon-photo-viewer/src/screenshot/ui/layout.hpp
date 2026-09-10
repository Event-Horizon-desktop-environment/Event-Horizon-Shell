#pragma once

#include <cstdint>

namespace hpv::sc {

struct Layout {
  int preview_x = 0;
  int preview_y = 0;
  int preview_w = 0;
  int preview_h = 0;
  int preview_cx = 0;
  int preview_cy = 0;
  int preview_img_size = 0;

  int sidebar_x = 0;
  int sidebar_y = 0;
  int sidebar_w = 0;
  int sidebar_h = 0;
  int sidebar_content_x = 0;
  int sidebar_content_w = 0;

  int title_y = 0;
  int title_h = 48;

  int grid_y = 0;
  int grid_h = 0;
  int grid_cell_w = 0;
  int grid_cell_h = 0;

  int list_y = 0;
  int list_h = 0;
  int list_item_h = 44;

  int export_y = 0;
  int export_h = 62;
  int export_btn_w = 0;

  int status_y = 0;
  int status_h = 28;
};

Layout compute_layout(int window_w, int window_h);

}
