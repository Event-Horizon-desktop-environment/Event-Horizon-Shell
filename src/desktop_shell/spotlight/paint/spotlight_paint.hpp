#pragma once

#include <cairo/cairo.h>
#include <string>

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/popup/geometry/layout.hpp"
#include "desktop_shell/common/fs/string_util.hpp"

namespace eh::shell::dock {

inline void spotlight_truncate_to_width(cairo_t* cr, const std::string& text, double maxAdvance, std::string* out) {
  *out = text;
  for (;;) {
    cairo_text_extents_t ex{};
    cairo_text_extents(cr, out->c_str(), &ex);
    if (ex.x_advance <= maxAdvance || out->size() < 4) break;
    eh::shell::str::utf8_pop_back(*out);
  }
}

inline int spotlight_pick_row_index(const DockApp& app, double localX, double localY) {
  (void)localX;
  if (localY < static_cast<double>(kSpotlightSearchOuterH())) return -1;
  const double rel = localY - static_cast<double>(kSpotlightSearchOuterH());
  const int idx = static_cast<int>(rel / static_cast<double>(kSpotlightRowPx()));
  if (idx < 0 || idx >= static_cast<int>(app.spotlightHits.size())) return -1;
  return idx;
}

}
