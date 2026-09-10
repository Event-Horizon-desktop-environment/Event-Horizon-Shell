#include "ux/settings/utils/dropdown/settings_dropdown.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/utils/helpers/settings_slider_appliers.hpp"

namespace eh::settings {

void SettingsDropdown::popup_geom(int scrollPx, int windowW, int windowH,
                                  int* ox, int* oy, int* ow, int* oh, bool* flipped) const {
  const int ph = std::min(maxPopupH_, std::max(count(), 0) * rowH_);
  const int pw = std::max(aw_, 0);
  const int x = std::clamp(ax_ - (pw - aw_) / 2, 8, std::max(8, windowW - pw - 8));
  int below = ay_ + ah_ + 2 - scrollPx;
  bool up = false;
  if (below + ph > windowH - 8) {
    const int above = ay_ - ph - 2 - scrollPx;
    if (above >= 8) {
      up = true;
      below = above;
    } else {
      below = std::max(8, windowH - ph - 8);
    }
  }
  *ox = x;
  *oy = below;
  *ow = pw;
  *oh = ph;
  *flipped = up;
}

bool SettingsDropdown::hit_trigger(int px, int py, int scrollPx) const {
  return point_in_rect(static_cast<double>(px), static_cast<double>(py), ax_, ay_ - scrollPx, aw_, ah_);
}

int SettingsDropdown::hit_row(int px, int py, int scrollPx, int windowW, int windowH) const {
  if (!open_) return -1;
  int ox = 0;
  int oy = 0;
  int ow = 0;
  int oh = 0;
  bool flipped = false;
  popup_geom(scrollPx, windowW, windowH, &ox, &oy, &ow, &oh, &flipped);
  if (!point_in_rect(static_cast<double>(px), static_cast<double>(py), ox, oy, ow, oh)) return -1;
  const int r = (py - oy) / rowH_;
  if (r < 0 || r >= count()) return -1;
  return r;
}

void SettingsDropdown::paint_trigger(App& app, cairo_t* cr, double glassOv, int scrollPx) const {
  settings_paint_combo_closed(app, cr, ax_, ay_, aw_, ah_, glassOv, display_text(), open_, scrollPx);
}

void SettingsDropdown::paint_popup(App& app, cairo_t* cr, int scrollPx, int windowW, int windowH,
                                   double glassOv) const {
  if (!open_ || labels_.empty()) return;
  int ox = 0;
  int oy = 0;
  int ow = 0;
  int oh = 0;
  bool flipped = false;
  popup_geom(scrollPx, windowW, windowH, &ox, &oy, &ow, &oh, &flipped);
  std::vector<const char*> ptrs;
  ptrs.reserve(labels_.size());
  for (const auto& l : labels_) ptrs.push_back(l.c_str());
  settings_paint_combo_list_popup(app, cr, ox, oy, ow, rowH_, count(), ptrs.data(), selected_,
                                  hoverRow_, glassOv);
  (void)flipped;
}

}  // namespace eh::settings
