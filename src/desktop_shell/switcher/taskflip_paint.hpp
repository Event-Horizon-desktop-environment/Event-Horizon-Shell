#pragma once

#include <cairo.h>

#include <cstddef>
#include <vector>

namespace eh::icons {
class IconCache;
}

namespace eh::shell::taskflip {

struct Entry;

struct PaintStyle {
  double textR = 1.0, textG = 1.0, textB = 1.0;
  double dimR = 1.0, dimG = 1.0, dimB = 1.0;
  double accentR = 0.35, accentG = 0.55, accentB = 1.0;
  double cardR = 0.12, cardG = 0.12, cardB = 0.13, cardA = 0.96;
  double fontSize = 13.0;
};

void paint_gallery(cairo_t* cr, const std::vector<Entry>& entries, std::size_t selected,
                    double w, double h, eh::icons::IconCache& icons, const PaintStyle& st);

void paint_rail(cairo_t* cr, const std::vector<Entry>& entries, std::size_t selected,
                   double w, double h, eh::icons::IconCache& icons, const PaintStyle& st);

}
