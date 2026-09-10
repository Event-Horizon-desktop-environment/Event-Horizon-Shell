#include "desktop_shell/desktop/widgets/world_clock/desktop_world_clock_settings.hpp"

#include "desktop_shell/desktop/core/desktop_app.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"
#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/paint/glass_card_style.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"

#include "configuration/shell_config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace eh::shell::desktop {

using eh::shell::shared::path_rounded_rect;

namespace {

constexpr int kVisibleTzItems = 9;
constexpr double kTzItemH = 22.0;

std::string world_clock_config_path() {
  if (const char* h = std::getenv("HOME"))
    return std::string(h) + "/.local/state/event-horizon/WorldClock";
  return "/tmp/event-horizon/WorldClock";
}

// Mirrors the widget's loader: reads city_N_tz / city_N_label pairs.
void load_world_clock_config(std::vector<std::string>& labels, std::vector<std::string>& tzs) {
  labels.clear();
  tzs.clear();
  try {
    toml::table tbl = toml::parse_file(world_clock_config_path());
    for (int idx = 1;; idx++) {
      const auto* tzVal = tbl.get_as<std::string>("city_" + std::to_string(idx) + "_tz");
      if (!tzVal) break;
      const std::string tz = tzVal->get();
      const auto* labelVal = tbl.get_as<std::string>("city_" + std::to_string(idx) + "_label");
      std::string label;
      if (labelVal) {
        label = labelVal->get();
      } else {
        auto pos = tz.find('/');
        label = (pos != std::string::npos) ? tz.substr(pos + 1) : tz;
        if (label.size() > 8) label = label.substr(0, 8);
      }
      labels.push_back(label);
      tzs.push_back(tz);
    }
  } catch (const toml::parse_error&) {
  }
}

void save_world_clock_config(const std::vector<std::string>& labels, const std::vector<std::string>& tzs) {
  try {
    std::filesystem::create_directories(
        std::filesystem::path(world_clock_config_path()).parent_path());
    std::ofstream ofs(world_clock_config_path());
    if (!ofs) return;
    const size_t n = std::min(labels.size(), tzs.size());
    for (size_t i = 0; i < n; i++) {
      ofs << "city_" << (i + 1) << "_tz = \"" << tzs[i] << "\"\n";
      ofs << "city_" << (i + 1) << "_label = \"" << labels[i] << "\"\n";
    }
  } catch (...) {
  }
}

// Enumerate /usr/share/zoneinfo entries so every place is available. Skips the
// posix/ and right/ duplicate subtrees, hidden files and the special localtime
// file, but includes bare single names (UTC, GMT, CET, ...).
void enumerate_zoneinfo(std::vector<std::string>& out) {
  out.clear();
  const fs::path root("/usr/share/zoneinfo");
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
    const fs::directory_entry& de = *it;
    if (!de.is_regular_file(ec)) continue;
    std::string rel = fs::relative(de.path(), root, ec).generic_string();
    if (ec) continue;
    if (rel.rfind("posix/", 0) == 0 || rel.rfind("right/", 0) == 0) continue;
    if (rel == "localtime" || rel[0] == '.') continue;
    out.push_back(std::move(rel));
  }
  std::sort(out.begin(), out.end());
}

const std::vector<std::string>& active_tz_list(const DesktopApp& app) {
  if (!app.worldClockSettingsSearch.empty()) return app.worldClockSettingsTzFiltered;
  return app.worldClockSettingsTzList;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                     [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                     }) != hay.end();
}

void rebuild_tz_filter(DesktopApp& app) {
  app.worldClockSettingsTzFiltered.clear();
  if (app.worldClockSettingsSearch.empty()) return;
  for (const auto& tz : app.worldClockSettingsTzList)
    if (contains_ci(tz, app.worldClockSettingsSearch)) app.worldClockSettingsTzFiltered.push_back(tz);
}

struct SettingsLayout {
  double dlgX = 0, dlgY = 0;
  double dlgW = 0, dlgH = 0;
  double rowH = 0;
  double rowsY = 0;
  double searchX = 0, searchY = 0, searchW = 0, searchH = 0;
  double listX = 0, listY = 0, listW = 0, listH = 0;
  double boxW = 0, boxH = 0;
  int visibleItems = 0;
  int maxScroll = 0;
};

SettingsLayout compute_layout(const DesktopApp& app, double us) {
  SettingsLayout l;
  const size_t n = app.worldClockSettingsCities.size();
  l.dlgW = 400.0 * us;
  l.boxW = 210.0 * us;
  l.boxH = 26.0 * us;
  l.rowH = 34.0 * us;
  const int tzCount = static_cast<int>(active_tz_list(app).size());
  l.visibleItems = std::min(kVisibleTzItems, tzCount);
  const double listExtra = (app.worldClockSettingsOpenDropdown >= 0 && l.visibleItems > 0)
                               ? (static_cast<double>(l.visibleItems) * kTzItemH + 6.0 * us + 8.0 * us)
                               : 0.0;
  const double searchH = 30.0 * us;
  l.searchY = l.dlgY + 42.0 * us;
  l.searchH = searchH;
  l.rowsY = l.dlgY + 42.0 * us + searchH + 10.0 * us;
  l.dlgH = (l.rowsY - l.dlgY) + static_cast<double>(n) * l.rowH + 12.0 * us + listExtra + 44.0 * us;
  if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
    const DesktopLayer& L = *app.layers[app.worldClockSettingsLayerIdx];
    l.dlgX = (static_cast<double>(L.configuredWidth) - l.dlgW) * 0.5;
    l.dlgY = (static_cast<double>(L.configuredHeight) - l.dlgH) * 0.5;
  }
  l.searchX = l.dlgX + 16.0 * us;
  l.searchW = l.dlgW - 32.0 * us;
  l.listX = l.dlgX + 16.0 * us;
  l.listW = l.dlgW - 32.0 * us;
  l.listY = l.rowsY + static_cast<double>(n) * l.rowH + 12.0 * us;
  l.listH = static_cast<double>(l.visibleItems) * kTzItemH + 6.0 * us;
  l.maxScroll = std::max(0, tzCount - l.visibleItems);
  return l;
}

// 1+i : dropdown box of row i; 10000+idx : tz list item idx; -2 : Done; -3 : close X; -4 : search box.
int hit_item(const DesktopApp& app, double lx, double ly, const SettingsLayout& l, double us) {
  const size_t n = app.worldClockSettingsCities.size();

  const double closeX = l.dlgX + l.dlgW - 34.0 * us;
  const double closeY = l.dlgY + 8.0 * us;
  if (lx >= closeX && lx < closeX + 26.0 * us && ly >= closeY && ly < closeY + 26.0 * us)
    return -3;

  if (lx >= l.searchX && lx < l.searchX + l.searchW && ly >= l.searchY && ly < l.searchY + l.searchH)
    return -4;

  for (size_t i = 0; i < n; i++) {
    const double rowY = l.rowsY + static_cast<double>(i) * l.rowH;
    const double boxX = l.dlgX + l.dlgW - 16.0 * us - l.boxW;
    const double boxY = rowY + (l.rowH - l.boxH) * 0.5;
    if (lx >= boxX && lx < boxX + l.boxW && ly >= boxY && ly < boxY + l.boxH)
      return static_cast<int>(1 + i);
  }

  if (app.worldClockSettingsOpenDropdown >= 0 && l.visibleItems > 0) {
    if (lx >= l.listX && lx < l.listX + l.listW && ly >= l.listY && ly < l.listY + l.listH) {
      const int row = static_cast<int>((ly - l.listY) / kTzItemH);
      const int idx = app.worldClockSettingsDropdownScroll + row;
      if (row >= 0 && row < l.visibleItems && idx >= 0 &&
          idx < static_cast<int>(active_tz_list(app).size()))
        return 10000 + idx;
    }
  }

  const double btnW = 120.0 * us;
  const double btnH = 32.0 * us;
  const double btnY = l.dlgY + l.dlgH - 12.0 * us - btnH;
  const double btnX = l.dlgX + (l.dlgW - btnW) * 0.5;
  if (lx >= btnX && lx < btnX + btnW && ly >= btnY && ly < btnY + btnH)
    return -2;

  return -1;
}

}  // namespace

void set_popup_keyboard_interactivity(DesktopApp& app, bool grab) {
  if (app.worldClockSettingsLayerIdx >= app.layers.size() || !app.layers[app.worldClockSettingsLayerIdx])
    return;
  DesktopLayer& L = *app.layers[app.worldClockSettingsLayerIdx];
  if (!L.menuLayer || !L.menuSurface) return;
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      L.menuLayer, grab ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
                        : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
  wl_surface_commit(L.menuSurface);
  if (app.display) wl_display_flush(app.display);
}

void world_clock_settings_open(DesktopApp& app) {
  if (app.pointerLayerIdx >= app.layers.size() || !app.layers[app.pointerLayerIdx]) return;
  load_world_clock_config(app.worldClockSettingsCities, app.worldClockSettingsTimezones);
  if (app.worldClockSettingsCities.empty()) {
    app.worldClockSettingsCities = {"Cupertino", "Tokyo", "Sydney", "Paris"};
    app.worldClockSettingsTimezones = {"America/Los_Angeles", "Asia/Tokyo", "Australia/Sydney",
                                       "Europe/Paris"};
  }
  if (app.worldClockSettingsTzList.empty()) enumerate_zoneinfo(app.worldClockSettingsTzList);
  app.worldClockSettingsOpen = true;
  app.worldClockSettingsLayerIdx = app.pointerLayerIdx;
  app.worldClockSettingsHoverItem = -1;
  app.worldClockSettingsArmItem = -1;
  app.worldClockSettingsOpenDropdown = -1;
  app.worldClockSettingsDropdownScroll = 0;
  app.worldClockSettingsDropdownHover = -1;
  app.worldClockSettingsSearch.clear();
  app.worldClockSettingsSearchFocus = false;
  app.worldClockSettingsTzFiltered.clear();
  set_popup_keyboard_interactivity(app, true);
  paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
  if (app.display) wl_display_flush(app.display);
}

void world_clock_settings_close(DesktopApp& app) {
  if (!app.worldClockSettingsOpen) {
    set_popup_keyboard_interactivity(app, false);
    return;
  }
  app.worldClockSettingsOpen = false;
  app.worldClockSettingsHoverItem = -1;
  app.worldClockSettingsArmItem = -1;
  app.worldClockSettingsOpenDropdown = -1;
  app.worldClockSettingsDropdownScroll = 0;
  app.worldClockSettingsDropdownHover = -1;
  app.worldClockSettingsSearch.clear();
  app.worldClockSettingsSearchFocus = false;
  app.worldClockSettingsTzFiltered.clear();
  set_popup_keyboard_interactivity(app, false);
}

bool world_clock_settings_hit(DesktopApp& app, double lx, double ly) {
  if (!app.worldClockSettingsOpen) return false;
  return lx >= app.worldClockSettingsX && ly >= app.worldClockSettingsY &&
         lx <= app.worldClockSettingsX + app.worldClockSettingsW &&
         ly <= app.worldClockSettingsY + app.worldClockSettingsH;
}

bool world_clock_settings_handle_left_press(DesktopApp& app) {
  if (!app.worldClockSettingsOpen) return false;
  double lx = 0, ly = 0;
  const bool hasLocal = desktop_pointer_local_xy(app, app.worldClockSettingsLayerIdx, &lx, &ly);
  if (!hasLocal || !world_clock_settings_hit(app, lx, ly)) {
    world_clock_settings_close(app);
    if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
      paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const SettingsLayout l = compute_layout(app, us);
  app.worldClockSettingsArmItem = hit_item(app, lx, ly, l, us);
  app.worldClockSettingsSearchFocus = (app.worldClockSettingsArmItem == -4);
  return true;
}

bool world_clock_settings_left_release(DesktopApp& app) {
  if (!app.worldClockSettingsOpen) return false;

  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.worldClockSettingsLayerIdx, &lx, &ly)) {
    world_clock_settings_close(app);
    if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
      paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }

  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const SettingsLayout l = compute_layout(app, us);
  const int armed = app.worldClockSettingsArmItem;
  app.worldClockSettingsArmItem = -1;
  const int item = hit_item(app, lx, ly, l, us);
  const bool inside = world_clock_settings_hit(app, lx, ly);
  const bool dropdownWasOpen = app.worldClockSettingsOpenDropdown >= 0;

  const auto repaint = [&app]() {
    if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
      paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
  };

  // Release on empty space: dismiss the open dropdown, never the popup.
  if (item == -1 && inside) {
    if (dropdownWasOpen) {
      app.worldClockSettingsOpenDropdown = -1;
      app.worldClockSettingsDropdownHover = -1;
    }
    repaint();
    return true;
  }

  // Cancelled click (released elsewhere than pressed) or released outside the
  // popup: dismiss only for outside releases.
  if (item != armed || !inside) {
    if (!inside) world_clock_settings_close(app);
    repaint();
    return true;
  }

  if (item == -3) {
    world_clock_settings_close(app);
  } else if (item >= 10000) {
    const int idx = item - 10000;
    const int row = app.worldClockSettingsOpenDropdown;
    const std::vector<std::string>& tzList = active_tz_list(app);
    if (row >= 0 && row < static_cast<int>(app.worldClockSettingsTimezones.size()) &&
        idx < static_cast<int>(tzList.size())) {
      const std::string& picked = tzList[static_cast<size_t>(idx)];
      app.worldClockSettingsTimezones[static_cast<size_t>(row)] = picked;
      auto pos = picked.find('/');
      std::string city = (pos != std::string::npos) ? picked.substr(pos + 1) : picked;
      std::replace(city.begin(), city.end(), '_', ' ');
      app.worldClockSettingsCities[static_cast<size_t>(row)] = city;
    }
    app.worldClockSettingsOpenDropdown = -1;
    app.worldClockSettingsDropdownHover = -1;
  } else if (item >= 1) {
    const int row = item - 1;
    if (app.worldClockSettingsOpenDropdown == row) {
      app.worldClockSettingsOpenDropdown = -1;
      app.worldClockSettingsDropdownHover = -1;
    } else {
      app.worldClockSettingsOpenDropdown = row;
      app.worldClockSettingsDropdownScroll = 0;
    }
  } else if (item == -2) {
    save_world_clock_config(app.worldClockSettingsCities, app.worldClockSettingsTimezones);
    world_clock_settings_close(app);
  } else {
    // search box (-4): focus already taken on press; dismiss any open dropdown.
    if (dropdownWasOpen) {
      app.worldClockSettingsOpenDropdown = -1;
      app.worldClockSettingsDropdownHover = -1;
    }
  }

  if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
    paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
    if (app.display) wl_display_flush(app.display);
  }
  return true;
}

bool world_clock_settings_key(DesktopApp& app, xkb_keysym_t sym, const char* utf8, size_t utf8Len) {
  if (!app.worldClockSettingsOpen) return false;

  bool printable = false;
  if (utf8 && utf8Len > 0) {
    printable = true;
    for (size_t i = 0; i < utf8Len; i++) {
      if (static_cast<unsigned char>(utf8[i]) < 0x20) {
        printable = false;
        break;
      }
    }
  }

  if (!app.worldClockSettingsSearchFocus) {
    if (sym == XKB_KEY_Escape) {
      world_clock_settings_close(app);
      if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
        paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
        if (app.display) wl_display_flush(app.display);
      }
      return true;
    }
    if (!printable) return false;
    app.worldClockSettingsSearchFocus = true;
  }

  bool changed = false;
  if (sym == XKB_KEY_Escape) {
    if (!app.worldClockSettingsSearch.empty()) {
      app.worldClockSettingsSearch.clear();
      changed = true;
    } else if (app.worldClockSettingsOpenDropdown >= 0) {
      app.worldClockSettingsOpenDropdown = -1;
      app.worldClockSettingsDropdownHover = -1;
      changed = true;
    } else {
      world_clock_settings_close(app);
      changed = true;
    }
  } else if (sym == XKB_KEY_BackSpace) {
    if (!app.worldClockSettingsSearch.empty()) {
      std::string& s = app.worldClockSettingsSearch;
      size_t n = s.size();
      while (n > 0 && (s[n - 1] & 0xC0) == 0x80) --n;
      if (n > 0) --n;
      s.resize(n);
      changed = true;
    }
  } else if (printable) {
    app.worldClockSettingsSearch.append(utf8, utf8Len);
    changed = true;
  }

  if (changed) {
    if (!app.worldClockSettingsOpen) {
      if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
        paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
        if (app.display) wl_display_flush(app.display);
      }
      return true;
    }
    rebuild_tz_filter(app);
    if (!app.worldClockSettingsSearch.empty() && app.worldClockSettingsOpenDropdown < 0)
      app.worldClockSettingsOpenDropdown = 0;
    app.worldClockSettingsDropdownScroll = 0;
    app.worldClockSettingsDropdownHover = -1;
    if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx]) {
      paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
  }
  return true;
}

bool world_clock_settings_pointer_motion(DesktopApp& app) {
  if (!app.worldClockSettingsOpen) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.worldClockSettingsLayerIdx, &lx, &ly)) return false;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const SettingsLayout l = compute_layout(app, us);
  const int prev = app.worldClockSettingsHoverItem;
  const int cur = hit_item(app, lx, ly, l, us);
  app.worldClockSettingsHoverItem = cur;
  if (app.worldClockSettingsOpenDropdown >= 0) {
    const int dprev = app.worldClockSettingsDropdownHover;
    app.worldClockSettingsDropdownHover = (cur >= 10000) ? (cur - 10000) : -1;
    if (dprev == app.worldClockSettingsDropdownHover && prev == cur) return true;
  } else if (prev == cur) {
    return true;
  }
  if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx])
    paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

bool world_clock_settings_handle_scroll(DesktopApp& app, double dy) {
  if (!app.worldClockSettingsOpen || app.worldClockSettingsOpenDropdown < 0) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.worldClockSettingsLayerIdx, &lx, &ly)) return false;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const SettingsLayout l = compute_layout(app, us);
  if (lx < l.listX || lx >= l.listX + l.listW || ly < l.listY || ly >= l.listY + l.listH)
    return false;
  double px = dy;
  if (std::abs(dy) >= 60.0) px = dy / 8.0;  // high-res wheels tick 120px ≈ one notch
  int steps = static_cast<int>(std::lround(px / kTzItemH));
  steps = std::clamp(steps, -2, 2);
  if (steps == 0 && std::abs(dy) >= 8.0) steps = (dy > 0.0) ? 1 : -1;
  if (steps == 0) return true;
  app.worldClockSettingsDropdownScroll =
      std::clamp(app.worldClockSettingsDropdownScroll - steps, 0, l.maxScroll);
  if (app.worldClockSettingsLayerIdx < app.layers.size() && app.layers[app.worldClockSettingsLayerIdx])
    paint_layer(app, *app.layers[app.worldClockSettingsLayerIdx]);
  if (app.display) wl_display_flush(app.display);
  return true;
}

void world_clock_settings_paint(DesktopApp& app, cairo_t* cr) {
  if (!app.worldClockSettingsOpen) return;
  if (app.worldClockSettingsLayerIdx >= app.layers.size() || !app.layers[app.worldClockSettingsLayerIdx])
    return;
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const SettingsLayout l = compute_layout(app, us);
  app.worldClockSettingsX = l.dlgX;
  app.worldClockSettingsY = l.dlgY;
  app.worldClockSettingsW = l.dlgW;
  app.worldClockSettingsH = l.dlgH;

  const eh::config::ShellConfig& sc = eh::config::shell_config_snapshot();
  const eh::config::ChromePaintColors mc = eh::config::derived_chrome_colors(sc.appearance);

  cairo_save(cr);

  cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
  cairo_paint(cr);

  const double radius = 10.0 * us;
  eh::shell::shared::paint_glass_card(cr, l.dlgX, l.dlgY, l.dlgW, l.dlgH, radius, mc, 1.0);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15.0 * us);
  cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
  cairo_move_to(cr, l.dlgX + 16.0 * us, l.dlgY + 26.0 * us);
  cairo_show_text(cr, "World Clock Settings");

  const double closeX = l.dlgX + l.dlgW - 34.0 * us;
  const double closeY = l.dlgY + 8.0 * us;
  const bool closeHover = app.worldClockSettingsHoverItem == -3;
  if (closeHover) {
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
    path_rounded_rect(cr, closeX, closeY, 26.0 * us, 26.0 * us, 6.0 * us);
    cairo_fill(cr);
  }
  cairo_set_source_rgba(cr, 0.85, 0.88, 0.92, 0.9);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_width(cr, 2.0 * us);
  cairo_move_to(cr, closeX + 8.0 * us, closeY + 8.0 * us);
  cairo_line_to(cr, closeX + 18.0 * us, closeY + 18.0 * us);
  cairo_move_to(cr, closeX + 18.0 * us, closeY + 8.0 * us);
  cairo_line_to(cr, closeX + 8.0 * us, closeY + 18.0 * us);
  cairo_stroke(cr);

  const bool searchFocus = app.worldClockSettingsSearchFocus;
  cairo_set_source_rgba(cr, 0.10, 0.13, 0.18, searchFocus ? 0.55 : 0.35);
  path_rounded_rect(cr, l.searchX, l.searchY, l.searchW, l.searchH, 6.0 * us);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, searchFocus ? mc.accentR : 0.55, searchFocus ? mc.accentG : 0.58,
                        searchFocus ? mc.accentB : 0.62, searchFocus ? 0.9 : 0.25);
  cairo_set_line_width(cr, 1);
  path_rounded_rect(cr, l.searchX + 0.5 * us, l.searchY + 0.5 * us, l.searchW - 1.0 * us,
                    l.searchH - 1.0 * us, 5.5 * us);
  cairo_stroke(cr);

  cairo_set_source_rgba(cr, 0.75, 0.78, 0.82, 0.9);
  cairo_set_line_width(cr, 1.5 * us);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  const double icx = l.searchX + 15.0 * us;
  const double icy = l.searchY + l.searchH * 0.5;
  cairo_arc(cr, icx, icy - 1.0 * us, 5.0 * us, 0, 2.0 * M_PI);
  cairo_stroke(cr);
  cairo_move_to(cr, icx + 3.5 * us, icy + 2.5 * us);
  cairo_line_to(cr, icx + 9.0 * us, icy + 8.0 * us);
  cairo_stroke(cr);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12.0 * us);
  const double textX = l.searchX + 28.0 * us;
  const double textY = l.searchY + l.searchH * 0.5 + 4.0 * us;
  if (!app.worldClockSettingsSearch.empty()) {
    cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, app.worldClockSettingsSearch.c_str());
    if (searchFocus) {
      cairo_text_extents_t te{};
      cairo_text_extents(cr, app.worldClockSettingsSearch.c_str(), &te);
      cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 0.9);
      cairo_rectangle(cr, textX + te.x_advance + 2.0 * us, l.searchY + 7.0 * us, 1.5 * us,
                      l.searchH - 14.0 * us);
      cairo_fill(cr);
    }
  } else {
    cairo_set_source_rgba(cr, 0.6, 0.64, 0.70, 0.8);
    cairo_move_to(cr, textX, textY);
    cairo_show_text(cr, "Search timezone...");
  }

  const size_t n = app.worldClockSettingsCities.size();
  for (size_t i = 0; i < n; i++) {
    const double rowY = l.rowsY + static_cast<double>(i) * l.rowH;

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.0 * us);
    cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
    cairo_move_to(cr, l.dlgX + 16.0 * us, rowY + 22.0 * us);
    cairo_show_text(cr, app.worldClockSettingsCities[i].c_str());

    const double boxX = l.dlgX + l.dlgW - 16.0 * us - l.boxW;
    const double boxY = rowY + (l.rowH - l.boxH) * 0.5;
    const bool hov = app.worldClockSettingsHoverItem == static_cast<int>(1 + i);
    cairo_set_source_rgba(cr, hov ? 0.25 : 0.15, hov ? 0.28 : 0.18, hov ? 0.35 : 0.24, 0.85);
    path_rounded_rect(cr, boxX, boxY, l.boxW, l.boxH, 4.0 * us);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.25);
    cairo_set_line_width(cr, 1);
    path_rounded_rect(cr, boxX + 0.5 * us, boxY + 0.5 * us, l.boxW - 1.0 * us, l.boxH - 1.0 * us,
                      3.5 * us);
    cairo_stroke(cr);

    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 1.0);
    std::string tz = app.worldClockSettingsTimezones[i];
    const double maxTextW = l.boxW - 26.0 * us;
    cairo_text_extents_t te{};
    cairo_text_extents(cr, tz.c_str(), &te);
    if (te.x_advance > maxTextW) {
      std::string shortTz = tz;
      while (!shortTz.empty()) {
        shortTz.pop_back();
        cairo_text_extents(cr, (shortTz + "...").c_str(), &te);
        if (te.x_advance <= maxTextW) {
          shortTz += "...";
          break;
        }
      }
      tz = shortTz;
    }
    cairo_move_to(cr, boxX + 10.0 * us, boxY + 18.0 * us);
    cairo_show_text(cr, tz.c_str());

    const char* chevron =
        (app.worldClockSettingsOpenDropdown == static_cast<int>(i)) ? "\xE2\x96\xB2" : "\xE2\x96\xBC";
    cairo_set_font_size(cr, 10.0 * us);
    cairo_set_source_rgba(cr, 0.6, 0.65, 0.7, 0.8);
    cairo_text_extents_t ce{};
    cairo_text_extents(cr, chevron, &ce);
    cairo_move_to(cr, boxX + l.boxW - 10.0 * us - ce.x_advance, boxY + 18.0 * us);
    cairo_show_text(cr, chevron);
  }

  if (app.worldClockSettingsOpenDropdown >= 0 && l.visibleItems > 0) {
    cairo_set_source_rgba(cr, 0.15, 0.18, 0.24, 0.95);
    path_rounded_rect(cr, l.listX, l.listY, l.listW, l.listH, 4.0 * us);
    cairo_fill(cr);
    cairo_save(cr);
    cairo_rectangle(cr, l.listX, l.listY, l.listW, l.listH);
    cairo_clip(cr);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    const std::vector<std::string>& tzList = active_tz_list(app);
    for (int r = 0; r < l.visibleItems; r++) {
      const int idx = app.worldClockSettingsDropdownScroll + r;
      if (idx < 0 || idx >= static_cast<int>(tzList.size())) break;
      const double iy = l.listY + 3.0 * us + static_cast<double>(r) * kTzItemH;
      const std::string& tz = tzList[static_cast<size_t>(idx)];
      if (app.worldClockSettingsDropdownHover == idx) {
        cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.25);
        cairo_rectangle(cr, l.listX + 2.0 * us, iy, l.listW - 4.0 * us, kTzItemH - 1.0 * us);
        cairo_fill(cr);
      }
      cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
      cairo_move_to(cr, l.listX + 10.0 * us, iy + 16.0 * us);
      cairo_show_text(cr, tz.c_str());
    }
    cairo_restore(cr);
  }

  const double btnW = 120.0 * us;
  const double btnH = 32.0 * us;
  const double btnY = l.dlgY + l.dlgH - 12.0 * us - btnH;
  const double btnX = l.dlgX + (l.dlgW - btnW) * 0.5;
  const bool btnHov = app.worldClockSettingsHoverItem == -2;
  cairo_set_source_rgba(cr, btnHov ? 0.25 : 0.15, btnHov ? 0.45 : 0.18, btnHov ? 0.85 : 0.24,
                        btnHov ? 0.9 : 0.85);
  path_rounded_rect(cr, btnX, btnY, btnW, btnH, 6.0 * us);
  cairo_fill(cr);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 12.0 * us);
  cairo_set_source_rgba(cr, 0.92, 0.93, 0.95, 1.0);
  cairo_text_extents_t te{};
  cairo_text_extents(cr, "Save", &te);
  cairo_move_to(cr, btnX + (btnW - te.x_advance) * 0.5, btnY + btnH * 0.5 + te.height * 0.3);
  cairo_show_text(cr, "Save");

  cairo_restore(cr);
}

}  // namespace eh::shell::desktop
