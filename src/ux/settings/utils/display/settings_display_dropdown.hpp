#pragma once

// Shared display-output dropdown helpers for Dock / Panel / Taskbar /
// Desktop Widgets settings tabs.
//
// Options are always: Auto, All, then one entry per connected display.
// Saved values: "" (empty) = Auto, "all" = All, otherwise the output name.
// Matching is case-insensitive and trims whitespace so legacy values keep
// working after renames.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/dropdown/settings_dropdown.hpp"
#include "ux/settings/utils/scroll/settings_scroll.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"
#include "wl/core/connection.hpp"

extern void draw(App& app);
extern void save_settings(const struct Settings& s);

namespace eh::settings::display {

inline std::string trim_name(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

inline bool is_auto(const std::string& s) {
  std::string t = trim_name(s);
  if (t.empty()) return true;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "auto";
}

inline bool is_all(const std::string& s) {
  std::string t = trim_name(s);
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "all";
}

inline bool names_match(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) return false;
  if (a == b) return true;
  std::string aa = a, bb = b;
  for (char& c : aa) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (char& c : bb) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return aa == bb;
}

inline std::vector<std::string> build_labels(
    const std::vector<eh::wayland::LogicalOutputInfo>& outs) {
  std::vector<std::string> labels;
  labels.reserve(2 + outs.size());
  labels.emplace_back("Auto");
  labels.emplace_back("All");
  for (const auto& o : outs) labels.push_back(o.name);
  return labels;
}

inline int selected_index(const std::string& assign,
                          const std::vector<eh::wayland::LogicalOutputInfo>& outs) {
  if (is_auto(assign)) return 0;
  if (is_all(assign)) return 1;
  for (size_t i = 0; i < outs.size(); ++i) {
    if (names_match(assign, outs[i].name)) return static_cast<int>(i) + 2;
  }
  return 0;
}

inline std::string assign_for_index(
    int idx, const std::vector<eh::wayland::LogicalOutputInfo>& outs) {
  if (idx <= 0) return std::string{};
  if (idx == 1) return std::string{"all"};
  const size_t oi = static_cast<size_t>(idx - 2);
  if (oi < outs.size()) return outs[oi].name;
  return std::string{};
}

// Combo geometry: right-aligned combo in a kDockVisRowPitch-tall band.
inline void combo_geom(int contentX, int contentW, int bandTop, int* cbx, int* cby,
                       int* cbw, int* cbh) {
  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  *cbx = static_cast<int>(cardX + cardW - kCardPad - static_cast<double>(kSettingsComboW));
  *cby = bandTop + (kDockVisRowPitch - kSettingsComboH) / 2;
  *cbw = kSettingsComboW;
  *cbh = kSettingsComboH;
}

// Feed labels/selection/anchor once per frame; call from paint, popup
// painting and hit tests so geometry is never stale.
inline void sync_dropdown(App& app, eh::settings::SettingsDropdown& dd,
                          const std::string& assign, int contentX, int contentW,
                          int bandTop) {
  app.wl.sync_logical_outputs_from_cache();
  const auto& outs = app.wl.logical_outputs();
  dd.set_labels(build_labels(outs));
  dd.set_selected(selected_index(assign, outs));
  dd.set_row_h(kSettingsDdRowH);
  int cbx = 0, cby = 0, cbw = 0, cbh = 0;
  combo_geom(contentX, contentW, bandTop, &cbx, &cby, &cbw, &cbh);
  dd.set_anchor(cbx, cby, cbw, cbh);
}

// Pointer-down handling for an open/closed dropdown. Handles row selection,
// trigger toggle and outside-close on the down press itself so a single
// click both opens and (on the next press) selects. Returns true when the
// event is consumed.
inline bool handle_pointer_down(App& app, eh::settings::SettingsDropdown& dd,
                                std::string& assignField, int contentX,
                                int contentW, int bandTop) {
  // Refresh the output list when opening so hot-plugged displays show up
  // immediately (mirrors the old UI Layout page behavior).
  bool wasOpen = dd.open();
  if (!wasOpen) {
    app.wl.refresh_logical_outputs();
  }
  sync_dropdown(app, dd, assignField, contentX, contentW, bandTop);
  const int scr = settings_scroll_px_int(app);
  const int sx = static_cast<int>(app.pointerX);
  const int sy = static_cast<int>(app.pointerY);
  if (dd.open()) {
    const int row = dd.hit_row(sx, sy, scr, app.width, app.height);
    if (row >= 0) {
      app.wl.sync_logical_outputs_from_cache();
      assignField = assign_for_index(row, app.wl.logical_outputs());
      save_settings(app.settings);
      dd.close();
      draw(app);
      return true;
    }
    if (dd.hit_trigger(sx, sy, scr)) {
      dd.close();
      draw(app);
      return true;
    }
    // Click outside the popup: close without changing the value.
    dd.close();
    draw(app);
    return true;
  }
  if (dd.hit_trigger(sx, sy, scr)) {
    dd.open_popup();
    draw(app);
    return true;
  }
  return false;
}

// Commit a pointer-up on an open dropdown. Selects the row under the
// release point (supports press-drag-release). Returns true when the
// dropdown was open (event consumed). Never closes on a miss: the opening
// click's release lands on the trigger (row == -1) and must keep the popup
// open; closing on misses is handled by handle_pointer_down (trigger toggle
// and outside-close) and by the outside branch below.
inline bool commit_pointer_up(App& app, eh::settings::SettingsDropdown& dd,
                              std::string& assignField, int contentX, int contentW,
                              int bandTop, float px, float py) {
  if (!dd.open()) return false;
  sync_dropdown(app, dd, assignField, contentX, contentW, bandTop);
  const int scr = settings_scroll_px_int(app);
  const int row = dd.hit_row(static_cast<int>(px), static_cast<int>(py), scr,
                             app.width, app.height);
  if (row >= 0) {
    app.wl.sync_logical_outputs_from_cache();
    assignField = assign_for_index(row, app.wl.logical_outputs());
    save_settings(app.settings);
    dd.close();
    draw(app);
    return true;
  }
  // Miss on release: keep the popup open (opening click releases on the
  // trigger) but swallow the event so toggles underneath don't fire.
  // Outside-release close is handled by the next pointer-down.
  return true;
}

// Hover tracking for the open popup (call from on_pointer_motion).
// Returns true when the hover row changed (caller should redraw).
inline bool update_hover(App& app, eh::settings::SettingsDropdown& dd,
                         int contentX, int contentW, const std::string& assign,
                         int bandTop) {
  if (!dd.open()) return false;
  sync_dropdown(app, dd, assign, contentX, contentW, bandTop);
  const int scr = settings_scroll_px_int(app);
  const int row = dd.hit_row(static_cast<int>(app.pointerX),
                             static_cast<int>(app.pointerY), scr, app.width,
                             app.height);
  if (row != dd.hover_row()) {
    dd.set_hover_row(row);
    return true;
  }
  return false;
}

}  // namespace eh::settings::display
