#include "screenshot/ui/layout.hpp"

#include "core/screenshot/logging.hpp"

#include <algorithm>

namespace hpv::sc {

Layout compute_layout(int window_w, int window_h)
{
  SC_LOG("compute_layout: enter w=%d h=%d", window_w, window_h);
  Layout l;

  int sidebar_fixed = 320;
  l.sidebar_w = sidebar_fixed;
  l.sidebar_x = window_w - l.sidebar_w;
  l.sidebar_y = 0;
  l.sidebar_h = window_h;
  l.sidebar_content_x = l.sidebar_x + 16;
  l.sidebar_content_w = l.sidebar_w - 32;

  l.preview_x = 0;
  l.preview_y = 0;
  l.preview_w = l.sidebar_x;
  l.preview_h = window_h;

  int preview_margin = 32;
  int preview_area = (std::min)(l.preview_w, l.preview_h) - preview_margin * 2;
  l.preview_img_size = (std::max)(preview_area, 100);
  l.preview_cx = l.preview_x + l.preview_w / 2;
  l.preview_cy = l.preview_y + l.preview_h / 2;

  l.title_y = l.sidebar_y + 12;

  l.grid_y = l.title_y + l.title_h + 20;
  l.grid_cell_w = (l.sidebar_content_w - 12) / 2;
  l.grid_cell_h = 46;
  l.grid_h = l.grid_cell_h * 2 + 12;

  l.list_y = l.grid_y + l.grid_h + 8;
  l.list_h = 0;

  l.export_y = l.sidebar_y + l.sidebar_h - l.export_h - 16;
  l.export_btn_w = (l.sidebar_content_w - 24) / 3;

  return l;
}

}
