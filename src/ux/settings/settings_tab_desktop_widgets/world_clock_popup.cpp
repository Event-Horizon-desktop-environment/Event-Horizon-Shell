#include "ux/settings/settings_tab_desktop_widgets/world_clock_popup.hpp"

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/time/text_caret.hpp"

#include <toml++/toml.hpp>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern void draw(App& app);

namespace {

// M3 design constants (mirror the add-widget picker).
constexpr double kPad = 20.0;
constexpr double kHeaderH = 56.0;
constexpr double kSearchH = 44.0;
constexpr double kHdrGap = 12.0;   // header -> search
constexpr double kGridGap = 12.0;  // search -> rows
constexpr double kRowH = 44.0;
constexpr double kTzItemH = 24.0;
constexpr int kVisibleTzItems = 9;
constexpr double kDlgW = 480.0;
constexpr double kPanelR = 16.0;
constexpr double kCardR = 10.0;
constexpr double kIconBoxS = 38.0;
constexpr double kIconR = 10.0;
constexpr double kCloseS = 26.0;
constexpr double kBoxW = 250.0;
constexpr double kBoxH = 30.0;
constexpr double kClearW = 26.0;
constexpr double kClearH = 26.0;
constexpr double kListGap = 8.0;
constexpr double kDoneW = 132.0;
constexpr double kDoneH = 34.0;
constexpr double kDoneGap = 14.0;
constexpr double kFootGap = 16.0;
constexpr double kFootH = 18.0;

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
  const std::filesystem::path root("/usr/share/zoneinfo");
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) return;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec;
       it.increment(ec)) {
    const std::filesystem::directory_entry& de = *it;
    if (!de.is_regular_file(ec)) continue;
    std::string rel = std::filesystem::relative(de.path(), root, ec).generic_string();
    if (ec) continue;
    if (rel.rfind("posix/", 0) == 0 || rel.rfind("right/", 0) == 0) continue;
    if (rel == "localtime" || rel[0] == '.') continue;
    out.push_back(std::move(rel));
  }
  std::sort(out.begin(), out.end());
}

const std::vector<std::string>& active_tz_list(const App& app) {
  if (!app.worldClockSearch.empty()) return app.worldClockTzFiltered;
  return app.worldClockTzList;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
  if (needle.empty()) return true;
  return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                     [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                     }) != hay.end();
}

void rebuild_tz_filter(App& app) {
  app.worldClockTzFiltered.clear();
  if (app.worldClockSearch.empty()) return;
  for (const auto& tz : app.worldClockTzList)
    if (contains_ci(tz, app.worldClockSearch)) app.worldClockTzFiltered.push_back(tz);
}

// Picker-style paint helpers (mirror widget_picker.cpp).
static void popup_card_src(const App& app, float* r, float* g, float* b) {
  if (app.drawChromeMatugen) {
    *r = static_cast<float>(app.drawChrome.panelFillR);
    *g = static_cast<float>(app.drawChrome.panelFillG);
    *b = static_cast<float>(app.drawChrome.panelFillB);
  } else {
    *r = static_cast<float>(Theme::BgR) * 0.88f;
    *g = static_cast<float>(Theme::BgG) * 0.90f;
    *b = static_cast<float>(Theme::BgB) * 0.95f;
  }
}

static void popup_icon_box(App& app, cairo_t* cr, double x, double y, double sz,
                           const char* ligature) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  const float ir = std::min(1.0f, a_r * 0.45f + 0.42f);
  const float ig = std::min(1.0f, a_g * 0.45f + 0.42f);
  const float ib = std::min(1.0f, a_b * 0.45f + 0.42f);
  m3::Box ibox;
  ibox.setColor(ir, ig, ib, 0.85f);
  ibox.setRadius(static_cast<float>(kIconR));
  ibox.setGeometry(static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(sz), static_cast<float>(sz));
  ibox.setGlassy(true);
  ibox.paint(cr);
  material_symbols_draw_glyph(cr, x + sz * 0.5, y + sz * 0.5 + 0.5, sz * 0.6,
                              ligature, t_r, t_g, t_b, 0.95);
}

static int picker_text_width_px(const char* text, float fontSize, int weight) {
  if (!text || !text[0]) return 0;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return 0;
  }
  cairo_t* cr = cairo_create(surf);
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int tw = 0;
  int th = 0;
  pango_layout_get_pixel_size(layout, &tw, &th);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return tw;
}

struct PopupLayout {
  double dlgX = 0, dlgY = 0;
  double dlgW = 0, dlgH = 0;
  double rowH = 0;
  double rowsY = 0;
  double searchX = 0, searchY = 0, searchW = 0, searchH = 0;
  double clearX = 0, clearY = 0;
  double listX = 0, listY = 0, listW = 0, listH = 0;
  double boxW = 0, boxH = 0;
  double doneX = 0, doneY = 0, doneW = 0, doneH = 0;
  double footY = 0;
  int visibleItems = 0;
  int maxScroll = 0;
};

PopupLayout compute_layout(const App& app, int winW, int winH) {
  PopupLayout l;
  const size_t n = app.worldClockCities.size();
  l.dlgW = kDlgW;
  l.boxW = kBoxW;
  l.boxH = kBoxH;
  l.rowH = kRowH;
  const int tzCount = static_cast<int>(active_tz_list(app).size());
  l.visibleItems = std::min(kVisibleTzItems, tzCount);
  const double listExtra = (app.worldClockOpenDropdown >= 0 && l.visibleItems > 0)
                               ? (kListGap + static_cast<double>(l.visibleItems) * kTzItemH + 8.0)
                               : 0.0;
  const double rowsYRel = kPad + kHeaderH + kHdrGap + kSearchH + kGridGap;
  const double rowsBottomRel = rowsYRel + static_cast<double>(n) * l.rowH + listExtra;
  const double doneYRel = rowsBottomRel + kDoneGap;
  const double footYRel = doneYRel + kDoneH + kFootGap;
  l.dlgH = footYRel + kFootH * 0.5 + kPad;
  l.dlgX = (static_cast<double>(winW) - l.dlgW) * 0.5;
  l.dlgY = (static_cast<double>(winH) - l.dlgH) * 0.5;
  l.searchX = l.dlgX + kPad;
  l.searchY = l.dlgY + kPad + kHeaderH;
  l.searchW = l.dlgW - 2.0 * kPad;
  l.searchH = kSearchH;
  l.clearX = l.searchX + l.searchW - 14.0 - kClearW;
  l.clearY = l.searchY + (kSearchH - kClearH) * 0.5;
  l.rowsY = l.searchY + kSearchH + kGridGap;
  l.listX = l.dlgX + l.dlgW - kPad - l.boxW;
  l.listW = l.boxW;
  l.listY = l.rowsY + static_cast<double>(n) * l.rowH + kListGap;
  l.listH = static_cast<double>(l.visibleItems) * kTzItemH + 8.0;
  l.maxScroll = std::max(0, tzCount - l.visibleItems);
  l.doneW = kDoneW;
  l.doneH = kDoneH;
  l.doneX = l.dlgX + (l.dlgW - kDoneW) * 0.5;
  l.doneY = l.dlgY + doneYRel;
  l.footY = l.dlgY + footYRel;
  return l;
}

// -3 : close X; -5 : clear chip; -4 : search box; 1+i : dropdown box of row i;
// 10000+idx : tz list item; -2 : Done.
int hit_item(const App& app, double lx, double ly, const PopupLayout& l) {
  const size_t n = app.worldClockCities.size();

  const double closeX = l.dlgX + l.dlgW - kPad - kCloseS;
  const double closeY = l.dlgY + kPad - 2.0;
  if (lx >= closeX && lx < closeX + kCloseS && ly >= closeY && ly < closeY + kCloseS) return -3;

  if (!app.worldClockSearch.empty() && lx >= l.clearX && lx < l.clearX + kClearW &&
      ly >= l.clearY && ly < l.clearY + kClearH)
    return -5;

  if (lx >= l.searchX && lx < l.searchX + l.searchW && ly >= l.searchY && ly < l.searchY + l.searchH)
    return -4;

  for (size_t i = 0; i < n; i++) {
    const double rowY = l.rowsY + static_cast<double>(i) * l.rowH;
    const double boxX = l.dlgX + l.dlgW - kPad - l.boxW;
    const double boxY = rowY + (l.rowH - l.boxH) * 0.5;
    if (lx >= boxX && lx < boxX + l.boxW && ly >= boxY && ly < boxY + l.boxH)
      return static_cast<int>(1 + i);
  }

  if (app.worldClockOpenDropdown >= 0 && l.visibleItems > 0) {
    if (lx >= l.listX && lx < l.listX + l.listW && ly >= l.listY && ly < l.listY + l.listH) {
      const int row = static_cast<int>((ly - l.listY) / kTzItemH);
      const int idx = app.worldClockDropdownScroll + row;
      if (row >= 0 && row < l.visibleItems && idx >= 0 &&
          idx < static_cast<int>(active_tz_list(app).size()))
        return 10000 + idx;
    }
  }

  if (lx >= l.doneX && lx < l.doneX + l.doneW && ly >= l.doneY && ly < l.doneY + l.doneH) return -2;

  return -1;
}

// Ellipsize `text` to fit `maxW` px at 12px/400 Inter (the tz box / list font).
std::string ellipsize_text(const std::string& text, double maxW) {
  std::string out = text;
  while (!out.empty() && static_cast<double>(picker_text_width_px(out.c_str(), 12, 400)) > maxW)
    out.pop_back();
  if (out != text) out += "...";
  return out;
}

}  // namespace

// Search caret blink, frame-synced like the widget picker.
void world_clock_popup_caret_frame_done(void* data, wl_callback* cb, uint32_t) {
  auto& app = *static_cast<App*>(data);
  wl_callback_destroy(cb);
  app.worldClockCaretFrameCb = nullptr;
  if (!app.surface || !app.worldClockPopupOpen || !app.worldClockSearchFocus) return;
  constexpr uint64_t kHalfMs = 530;
  const uint64_t half = eh::shell::monotonic_ms() / kHalfMs;
  if (half != app.worldClockCaretBlinkHalf) {
    app.worldClockCaretBlinkHalf = half;
    draw(app);
    return;
  }
  world_clock_popup_queue_caret_frame(app);
}

void world_clock_popup_queue_caret_frame(App& app) {
  if (!app.surface || app.worldClockCaretFrameCb) return;
  if (!app.worldClockPopupOpen || !app.worldClockSearchFocus) return;
  static const wl_callback_listener kListener = {.done = world_clock_popup_caret_frame_done};
  app.worldClockCaretFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.worldClockCaretFrameCb, &kListener, &app);
}

bool world_clock_popup_visible(const App& app) { return app.worldClockPopupOpen; }

void world_clock_popup_open(App& app) {
  load_world_clock_config(app.worldClockCities, app.worldClockTimezones);
  if (app.worldClockCities.empty()) {
    app.worldClockCities = {"Cupertino", "Tokyo", "Sydney", "Paris"};
    app.worldClockTimezones = {"America/Los_Angeles", "Asia/Tokyo", "Australia/Sydney",
                               "Europe/Paris"};
  }
  if (app.worldClockTzList.empty()) enumerate_zoneinfo(app.worldClockTzList);
  app.worldClockPopupOpen = true;
  app.worldClockPopupJustOpened = true;
  app.worldClockHoverItem = -1;
  app.worldClockArmItem = -1;
  app.worldClockOpenDropdown = -1;
  app.worldClockDropdownScroll = 0;
  app.worldClockDropdownHover = -1;
  app.worldClockSearch.clear();
  app.worldClockSearchFocus = false;
  app.worldClockTzFiltered.clear();
  app.worldClockCaretBlinkHalf = eh::shell::monotonic_ms() / 530u;
  draw(app);
}

void world_clock_popup_close(App& app) {
  if (!app.worldClockPopupOpen) return;
  app.worldClockPopupOpen = false;
  app.worldClockPopupJustOpened = false;
  app.worldClockHoverItem = -1;
  app.worldClockArmItem = -1;
  app.worldClockOpenDropdown = -1;
  app.worldClockDropdownScroll = 0;
  app.worldClockDropdownHover = -1;
  app.worldClockSearch.clear();
  app.worldClockSearchFocus = false;
  app.worldClockTzFiltered.clear();
  if (app.worldClockCaretFrameCb) {
    wl_callback_destroy(app.worldClockCaretFrameCb);
    app.worldClockCaretFrameCb = nullptr;
  }
  draw(app);
}

bool world_clock_popup_consume_pointer_down(App& app) {
  if (!app.worldClockPopupOpen) return false;
  app.worldClockPopupJustOpened = false;
  const PopupLayout l = compute_layout(app, app.width, app.height);
  app.worldClockArmItem = hit_item(app, app.pointerX, app.pointerY, l);
  app.worldClockSearchFocus = (app.worldClockArmItem == -4);
  draw(app);
  return true;
}

void world_clock_popup_consume_pointer_up(App& app) {
  if (!app.worldClockPopupOpen) return;
  if (app.worldClockPopupJustOpened) {
    // The popup was opened by this very press (cog click) — swallow the
    // matching release so it doesn't get treated as a click outside.
    app.worldClockPopupJustOpened = false;
    return;
  }
  const PopupLayout l = compute_layout(app, app.width, app.height);
  const int armed = app.worldClockArmItem;
  app.worldClockArmItem = -1;
  const int item = hit_item(app, app.pointerX, app.pointerY, l);
  const bool inside = app.pointerX >= app.worldClockX && app.pointerY >= app.worldClockY &&
                      app.pointerX <= app.worldClockX + app.worldClockW &&
                      app.pointerY <= app.worldClockY + app.worldClockH;
  const bool dropdownWasOpen = app.worldClockOpenDropdown >= 0;

  // Release on empty space: dismiss the open dropdown, never the popup.
  if (item == -1 && inside) {
    if (dropdownWasOpen) {
      app.worldClockOpenDropdown = -1;
      app.worldClockDropdownHover = -1;
    }
    draw(app);
    return;
  }

  // Cancelled click (released elsewhere than pressed) or released outside the
  // popup: dismiss only for outside releases.
  if (item != armed || !inside) {
    if (!inside) world_clock_popup_close(app);
    else draw(app);
    return;
  }

  if (item == -3) {
    world_clock_popup_close(app);
  } else if (item == -5) {
    app.worldClockSearch.clear();
    rebuild_tz_filter(app);
    app.worldClockOpenDropdown = -1;
    app.worldClockDropdownHover = -1;
    app.worldClockSearchFocus = true;
    draw(app);
  } else if (item >= 10000) {
    const int idx = item - 10000;
    const int row = app.worldClockOpenDropdown;
    const std::vector<std::string>& tzList = active_tz_list(app);
    if (row >= 0 && row < static_cast<int>(app.worldClockTimezones.size()) &&
        idx < static_cast<int>(tzList.size())) {
      const std::string& picked = tzList[static_cast<size_t>(idx)];
      app.worldClockTimezones[static_cast<size_t>(row)] = picked;
      auto pos = picked.find('/');
      std::string city = (pos != std::string::npos) ? picked.substr(pos + 1) : picked;
      std::replace(city.begin(), city.end(), '_', ' ');
      app.worldClockCities[static_cast<size_t>(row)] = city;
    }
    app.worldClockOpenDropdown = -1;
    app.worldClockDropdownHover = -1;
    draw(app);
  } else if (item >= 1) {
    const int row = item - 1;
    if (app.worldClockOpenDropdown == row) {
      app.worldClockOpenDropdown = -1;
      app.worldClockDropdownHover = -1;
    } else {
      app.worldClockOpenDropdown = row;
      app.worldClockDropdownScroll = 0;
    }
    draw(app);
  } else if (item == -2) {
    save_world_clock_config(app.worldClockCities, app.worldClockTimezones);
    world_clock_popup_close(app);
  } else {
    // search box (-4): focus already taken on press; dismiss any open dropdown.
    if (dropdownWasOpen) {
      app.worldClockOpenDropdown = -1;
      app.worldClockDropdownHover = -1;
    }
    draw(app);
  }
}

void world_clock_popup_consume_pointer_motion(App& app) {
  if (!app.worldClockPopupOpen) return;
  const PopupLayout l = compute_layout(app, app.width, app.height);
  const int prev = app.worldClockHoverItem;
  const int cur = hit_item(app, app.pointerX, app.pointerY, l);
  app.worldClockHoverItem = cur;
  bool changed = false;
  if (app.worldClockOpenDropdown >= 0) {
    const int dprev = app.worldClockDropdownHover;
    app.worldClockDropdownHover = (cur >= 10000) ? (cur - 10000) : -1;
    changed = (dprev != app.worldClockDropdownHover) || (prev != cur);
  } else {
    changed = (prev != cur);
  }
  if (changed) draw(app);
}

bool world_clock_popup_consume_scroll(App& app, double dy) {
  if (!app.worldClockPopupOpen || app.worldClockOpenDropdown < 0) return false;
  const PopupLayout l = compute_layout(app, app.width, app.height);
  if (app.pointerX < l.listX || app.pointerX >= l.listX + l.listW ||
      app.pointerY < l.listY || app.pointerY >= l.listY + l.listH)
    return false;
  double px = dy;
  if (std::abs(dy) >= 60.0) px = dy / 8.0;  // high-res wheels tick 120px ≈ one notch
  int steps = static_cast<int>(std::lround(px / kTzItemH));
  steps = std::clamp(steps, -2, 2);
  if (steps == 0 && std::abs(dy) >= 8.0) steps = (dy > 0.0) ? 1 : -1;
  if (steps != 0) {
    app.worldClockDropdownScroll = std::clamp(app.worldClockDropdownScroll - steps, 0, l.maxScroll);
    draw(app);
  }
  return true;
}

void world_clock_popup_consume_key(App& app, std::uint32_t sym, std::uint32_t state,
                                   const char* utf8, int utf8_len) {
  if (!app.worldClockPopupOpen) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  bool printable = false;
  if (utf8 && utf8_len > 0) {
    printable = true;
    for (int i = 0; i < utf8_len; i++) {
      if (static_cast<unsigned char>(utf8[i]) < 0x20) {
        printable = false;
        break;
      }
    }
  }

  if (!app.worldClockSearchFocus) {
    if (sym == XKB_KEY_Escape) {
      world_clock_popup_close(app);
      return;
    }
    if (!printable) return;
    app.worldClockSearchFocus = true;
  }

  bool changed = false;
  if (sym == XKB_KEY_Escape) {
    if (!app.worldClockSearch.empty()) {
      app.worldClockSearch.clear();
      changed = true;
    } else if (app.worldClockOpenDropdown >= 0) {
      app.worldClockOpenDropdown = -1;
      app.worldClockDropdownHover = -1;
      changed = true;
    } else {
      world_clock_popup_close(app);
      return;
    }
  } else if (sym == XKB_KEY_BackSpace) {
    if (!app.worldClockSearch.empty()) {
      std::string& s = app.worldClockSearch;
      size_t n = s.size();
      while (n > 0 && (s[n - 1] & 0xC0) == 0x80) --n;
      if (n > 0) --n;
      s.resize(n);
      changed = true;
    }
  } else if (printable) {
    app.worldClockSearch.append(utf8, static_cast<size_t>(utf8_len));
    changed = true;
  }

  if (!changed) return;
  rebuild_tz_filter(app);
  if (!app.worldClockSearch.empty() && app.worldClockOpenDropdown < 0)
    app.worldClockOpenDropdown = 0;
  app.worldClockDropdownScroll = 0;
  app.worldClockDropdownHover = -1;
  draw(app);
}

void world_clock_popup_paint(App& app, cairo_t* cr) {
  if (!app.worldClockPopupOpen) return;
  const PopupLayout l = compute_layout(app, app.width, app.height);
  app.worldClockX = l.dlgX;
  app.worldClockY = l.dlgY;
  app.worldClockW = l.dlgW;
  app.worldClockH = l.dlgH;

  const eh::config::ShellConfig& scPick0 = eh::config::shell_config_snapshot();
  app.drawChromeMatugen = scPick0.appearance.anyPaletteActive();
  app.drawChrome = eh::config::derived_chrome_colors(scPick0.appearance);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.38);
  cairo_rectangle(cr, 0, 0, app.width, app.height);
  cairo_fill(cr);

  // Panel.
  {
    m3::Box panel;
    panel.setGeometry(static_cast<float>(l.dlgX), static_cast<float>(l.dlgY),
                      static_cast<float>(l.dlgW), static_cast<float>(l.dlgH));
    panel.setRadius(static_cast<float>(kPanelR));
    if (app.drawChromeMatugen)
      panel.setColor(static_cast<float>(app.drawChrome.panelFillR),
                     static_cast<float>(app.drawChrome.panelFillG),
                     static_cast<float>(app.drawChrome.panelFillB), 0.96f);
    else
      panel.setColor(static_cast<float>(Theme::BgR), static_cast<float>(Theme::BgG),
                     static_cast<float>(Theme::BgB), 0.96f);
    panel.setGlassy(true);
    panel.paint(cr);
  }

  // Header.
  const double hx = l.dlgX + kPad;
  const double hy = l.dlgY + kPad;
  popup_icon_box(app, cr, hx, hy + 3.0, kIconBoxS, "public");
  settings_show_text(cr, hx + kIconBoxS + 12, hy + 23, "World Clock Settings", 16, 700,
                     t_r, t_g, t_b, 0.95);
  settings_show_text(cr, hx + kIconBoxS + 12, hy + 40, "Timezones shown by the world_clock widget",
                     11, 400, t_r, t_g, t_b, 0.55);

  // Close button.
  {
    const bool closeHover = app.worldClockHoverItem == -3;
    m3::Button closeBtn;
    closeBtn.setMinSize(0, 0);
    closeBtn.setGlyph("close");
    closeBtn.setGeometry(static_cast<float>(l.dlgX + l.dlgW - kPad - kCloseS),
                         static_cast<float>(l.dlgY + kPad - 2.0),
                         static_cast<float>(kCloseS), static_cast<float>(kCloseS));
    closeBtn.setStyle(m3::Button::Style::Outlined);
    closeBtn.setSize(m3::Button::Size::XS);
    closeBtn.setAccentColor(a_r, a_g, a_b);
    closeBtn.setOutlineColor(o_r, o_g, o_b);
    closeBtn.setHovered(closeHover);
    closeBtn.paint(cr);
  }

  // Search field (with clear chip, clipped text + caret).
  {
    {
      m3::Box sb;
      sb.setGeometry(static_cast<float>(l.searchX), static_cast<float>(l.searchY),
                     static_cast<float>(l.searchW), static_cast<float>(l.searchH));
      sb.setRadius(12.0f);
      float br, bg, bb;
      popup_card_src(app, &br, &bg, &bb);
      sb.setColor(br, bg, bb, 1.0f);
      sb.setGlassy(true);
      sb.paint(cr);
    }

    material_symbols_draw_glyph(cr, l.searchX + 18.0, l.searchY + l.searchH * 0.5 + 0.5,
                                18.0, "search", t_r, t_g, t_b, 0.5);
    const double textX = l.searchX + 46.0;
    const double textY = l.searchY + (l.searchH + 15.0) * 0.5;

    cairo_save(cr);
    cairo_rectangle(cr, textX, l.searchY + 6.0, l.clearX - 6.0 - textX, l.searchH - 12.0);
    cairo_clip(cr);
    if (app.worldClockSearch.empty()) {
      if (!app.worldClockSearchFocus)
        settings_show_text(cr, textX, textY, "Search timezone...", 13, 400, t_r, t_g, t_b, 0.42);
    } else {
      settings_show_text(cr, textX, textY, app.worldClockSearch.c_str(), 13, 400,
                         t_r, t_g, t_b, 0.92);
    }
    if (app.worldClockSearchFocus) {
      const bool caretOn = eh::shell::text_caret_blink_on(eh::shell::monotonic_ms(), true);
      if (caretOn) {
        const double caretX =
            textX + static_cast<double>(picker_text_width_px(app.worldClockSearch.c_str(), 13, 400)) + 1.5;
        cairo_set_line_width(cr, 1.6);
        cairo_set_source_rgba(cr, t_r, t_g, t_b, 0.85);
        cairo_move_to(cr, caretX, l.searchY + 12.0);
        cairo_line_to(cr, caretX, l.searchY + l.searchH - 12.0);
        cairo_stroke(cr);
      }
    }
    cairo_restore(cr);

    if (!app.worldClockSearch.empty()) {
      const bool clearHover = app.worldClockHoverItem == -5;
      m3::Box cb;
      if (clearHover)
        cb.setColor(a_r, a_g, a_b, 0.92f);
      else
        cb.setColor(t_r, t_g, t_b, 0.08f);
      cb.setRadius(13.0f);
      cb.setGeometry(static_cast<float>(l.clearX), static_cast<float>(l.clearY),
                     static_cast<float>(kClearW), static_cast<float>(kClearH));
      cb.setGlassy(true);
      cb.paint(cr);
      const double gcx = l.clearX + kClearW * 0.5;
      const double gcy = l.clearY + kClearH * 0.5 + 0.5;
      if (clearHover)
        material_symbols_draw_glyph(cr, gcx, gcy, 16.0, "close", 1, 1, 1, 0.95);
      else
        material_symbols_draw_glyph(cr, gcx, gcy, 16.0, "close", t_r, t_g, t_b, 0.65);
    }
  }

  // City rows + dropdown triggers.
  const size_t n = app.worldClockCities.size();
  for (size_t i = 0; i < n; i++) {
    const double rowY = l.rowsY + static_cast<double>(i) * l.rowH;
    const bool rowHover = app.worldClockHoverItem == static_cast<int>(1 + i);
    settings_show_text(cr, l.dlgX + kPad, rowY + l.rowH * 0.5 + 5.0, app.worldClockCities[i].c_str(),
                       14, 600, t_r, t_g, t_b, rowHover ? 1.0f : 0.92f);

    const double boxX = l.dlgX + l.dlgW - kPad - l.boxW;
    const double boxY = rowY + (l.rowH - l.boxH) * 0.5;
    {
      float br, bg, bb;
      popup_card_src(app, &br, &bg, &bb);
      if (rowHover) {
        br = std::min(1.0f, br + (a_r - br) * 0.16f);
        bg = std::min(1.0f, bg + (a_g - bg) * 0.16f);
        bb = std::min(1.0f, bb + (a_b - bb) * 0.16f);
      }
      m3::Box tzBox;
      tzBox.setGeometry(static_cast<float>(boxX), static_cast<float>(boxY),
                        static_cast<float>(l.boxW), static_cast<float>(l.boxH));
      tzBox.setRadius(static_cast<float>(kCardR));
      tzBox.setColor(br, bg, bb, 1.0f);
      tzBox.setGlassy(true);
      tzBox.paint(cr);
    }
    if (rowHover) {
      cairo_round_rect(cr, boxX + 0.5, boxY + 0.5, l.boxW - 1.0, l.boxH - 1.0, kCardR);
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.55);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }
    const bool open = app.worldClockOpenDropdown == static_cast<int>(i);
    if (open) {
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.6);
      cairo_set_line_width(cr, 1.2);
      cairo_round_rect(cr, boxX + 0.5, boxY + 0.5, l.boxW - 1.0, l.boxH - 1.0, kCardR);
      cairo_stroke(cr);
    }

    const std::string tz =
        ellipsize_text(app.worldClockTimezones[i], l.boxW - 36.0);
    settings_show_text(cr, boxX + 10.0, boxY + l.boxH * 0.5 + 5.0, tz.c_str(),
                       12, 400, t_r, t_g, t_b, 0.9);
    material_symbols_draw_glyph(cr, boxX + l.boxW - 13.0, boxY + l.boxH * 0.5 + 0.5,
                                16.0, open ? "expand_less" : "expand_more", t_r, t_g, t_b, 0.55);
  }

  // Dropdown list.
  if (app.worldClockOpenDropdown >= 0 && l.visibleItems > 0) {
    m3::Box listBox;
    float br, bg, bb;
    popup_card_src(app, &br, &bg, &bb);
    listBox.setGeometry(static_cast<float>(l.listX), static_cast<float>(l.listY),
                        static_cast<float>(l.listW), static_cast<float>(l.listH));
    listBox.setRadius(12.0f);
    listBox.setColor(br, bg, bb, 0.98f);
    listBox.setGlassy(true);
    listBox.paint(cr);

    cairo_save(cr);
    cairo_rectangle(cr, l.listX, l.listY, l.listW, l.listH);
    cairo_clip(cr);
    const std::vector<std::string>& tzList = active_tz_list(app);
    for (int r = 0; r < l.visibleItems; r++) {
      const int idx = app.worldClockDropdownScroll + r;
      if (idx < 0 || idx >= static_cast<int>(tzList.size())) break;
      const double iy = l.listY + 4.0 + static_cast<double>(r) * kTzItemH;
      const std::string& tzName = tzList[static_cast<size_t>(idx)];
      if (app.worldClockDropdownHover == idx) {
        cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.18);
        cairo_round_rect(cr, l.listX + 3.0, iy, l.listW - 6.0, kTzItemH - 2.0, 6.0);
        cairo_fill(cr);
      }
      settings_show_text(cr, l.listX + 10.0, iy + 17.0, tzName.c_str(), 12, 400,
                         t_r, t_g, t_b, 0.92);
    }
    cairo_restore(cr);

    if (l.maxScroll > 0) {
      const double sbX = l.listX + l.listW - 7.0;
      const double listContentH = static_cast<double>(active_tz_list(app).size()) * kTzItemH + 8.0;
      const double thumbH =
          std::max(24.0, l.listH * l.listH / std::max(listContentH, l.listH));
      const double norm = static_cast<double>(app.worldClockDropdownScroll) /
                          static_cast<double>(l.maxScroll);
      const double thumbY = l.listY + (l.listH - thumbH) * norm;
      cairo_set_source_rgba(cr, t_r, t_g, t_b, 0.30);
      cairo_round_rect(cr, sbX, thumbY, 4.0, thumbH, 2.0);
      cairo_fill(cr);
    }
  }

  // Done button.
  {
    const bool btnHov = app.worldClockHoverItem == -2;
    m3::Box doneBtn;
    doneBtn.setGeometry(static_cast<float>(l.doneX), static_cast<float>(l.doneY),
                        static_cast<float>(l.doneW), static_cast<float>(l.doneH));
    doneBtn.setRadius(8.0f);
    if (btnHov) {
      doneBtn.setColor(std::min(1.0f, a_r * 1.12f), std::min(1.0f, a_g * 1.12f),
                       std::min(1.0f, a_b * 1.12f), 0.95f);
    } else {
      doneBtn.setColor(a_r, a_g, a_b, 0.9f);
    }
    doneBtn.setGlassy(true);
    doneBtn.paint(cr);
    const int doneTw = picker_text_width_px("Save", 12, 700);
    settings_show_text(cr, l.doneX + (l.doneW - static_cast<double>(doneTw)) * 0.5,
                       l.doneY + l.doneH * 0.5 + 5.0, "Save", 12, 700, 1.0f, 1.0f, 1.0f, 0.97f);
  }

  // Footer.
  {
    const int fw = picker_text_width_px("Esc or click outside to close", 11, 400);
    settings_show_text(cr, l.dlgX + (l.dlgW - static_cast<double>(fw)) * 0.5, l.footY,
                       "Esc or click outside to close", 11, 400, t_r, t_g, t_b, 0.45);
  }

  cairo_restore(cr);
}