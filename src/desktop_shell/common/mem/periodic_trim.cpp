#include "desktop_shell/common/mem/periodic_trim.hpp"

#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

namespace eh::shell::shared {

void PeriodicTrim::clear_font_cache() {
  if (PangoFontMap* fm = pango_cairo_font_map_get_default())
    pango_fc_font_map_cache_clear(PANGO_FC_FONT_MAP(fm));
}

}  // namespace eh::shell::shared
