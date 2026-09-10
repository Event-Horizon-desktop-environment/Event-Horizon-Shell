#pragma once

// Self-contained settings dropdown/combobox component.
//
// Owns EVERYTHING a dropdown needs so tabs stop hand-rolling popup hit tests:
//   • labels + selected index + hover row
//   • anchor geometry (set by the tab in logical/content space)
//   • popup geometry in screen space (auto flips above the anchor when it
//     would overflow the window, and honours the page scroll offset)
//   • hit tests for both the trigger and the popup rows
//   • painting for both the trigger and the popup list
//
// Tabs only provide labels, the selected index and the anchor rectangle.
// Position, hover and hit data are fully dynamic and computed here from the
// live scroll offset, so they can never go stale or drift out of sync.
//
// The heavy methods (geometry, hit tests, painting) are defined in
// settings_dropdown.cpp — they need the settings paint helpers, which in turn
// need the complete App type, so keeping them out of this header avoids the
// App/settings_common.hpp include cycle.

#include <algorithm>
#include <string>
#include <vector>

#include <cairo/cairo.h>

struct App;

namespace eh::settings {

class SettingsDropdown {
 public:
  void set_labels(std::vector<std::string> l) { labels_ = std::move(l); }
  void set_labels(const char* const* labels, int count) {
    labels_.clear();
    labels_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) labels_.emplace_back(labels[i]);
  }
  void set_selected(int s) { selected_ = s; }
  void set_row_h(int h) { rowH_ = std::max(16, h); }
  void set_max_popup_h(int h) { maxPopupH_ = std::max(0, h); }
  void set_anchor(int x, int y, int w, int h) {
    ax_ = x;
    ay_ = y;
    aw_ = w;
    ah_ = h;
  }

  bool open() const { return open_; }
  void open_popup() { open_ = true; }
  void close() { open_ = false; hoverRow_ = -1; }
  int selected() const { return selected_; }
  int hover_row() const { return hoverRow_; }
  void set_hover_row(int r) { hoverRow_ = r; }

  int count() const { return static_cast<int>(labels_.size()); }
  const char* display_text() const {
    if (labels_.empty()) return "";
    const int s = std::clamp(selected_, 0, count() - 1);
    return labels_[static_cast<size_t>(s)].c_str();
  }

  // Geometry (screen space).
  // scrollPx = the page's animated scroll offset (content -> screen).
  // Fills the popup list box, flipping above the anchor when needed.
  void popup_geom(int scrollPx, int windowW, int windowH,
                  int* ox, int* oy, int* ow, int* oh, bool* flipped) const;

  // Hit testing (screen space).
  bool hit_trigger(int px, int py, int scrollPx) const;

  // Returns the popup row under (px, py) or -1 when outside the list.
  int hit_row(int px, int py, int scrollPx, int windowW, int windowH) const;

  // Painting.
  // Trigger is painted in logical (already translated) space.
  void paint_trigger(App& app, cairo_t* cr, double glassOv, int scrollPx) const;

  // Popup is painted in screen space (outside the page clip).
  void paint_popup(App& app, cairo_t* cr, int scrollPx, int windowW, int windowH, double glassOv) const;

 private:
  std::vector<std::string> labels_{};
  int selected_ = 0;
  int hoverRow_ = -1;
  bool open_ = false;
  int rowH_ = 26;
  int maxPopupH_ = 360;
  int ax_ = 0;
  int ay_ = 0;
  int aw_ = 200;
  int ah_ = 28;
};

}  // namespace eh::settings
