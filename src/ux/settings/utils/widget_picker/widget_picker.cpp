#include "ux/settings/utils/widget_picker/widget_picker.hpp"

#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "ux/settings/utils/widget_picker/settings_widget_drag.hpp"

#include <cairo/cairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"

#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "ux/settings/common/settings_common.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/time/text_caret.hpp"
#include "ux/settings/settings_app_types.hpp"
#include "desktop_shell/common/ns/namespaces.hpp"
#include "wl/core/protocols.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"

extern void draw(App& app);

namespace eh::settings::widget_picker {

void teardown_layer(App& app);
void close_picker(App& app);

// Search caret blink, frame-synced. Follows the same pattern as the app
// drawer's search caret: a dedicated wl_surface.frame chain polls the 530 ms
// blink cycle and only redraws the (whole settings window) when the cycle
// flips, so there's no 60 fps redraw loop.
void widget_picker_caret_frame_done(void* data, wl_callback* cb, uint32_t);

void destroy_caret_frame(App& app) {
  if (app.widgetPickerCaretFrameCb) {
    wl_callback_destroy(app.widgetPickerCaretFrameCb);
    app.widgetPickerCaretFrameCb = nullptr;
  }
}

void queue_caret_frame(App& app) {
  if (!app.surface || app.widgetPickerCaretFrameCb) return;
  if (!app.widgetPickerOpen || !app.widgetPickerSearchActive) return;
  static const wl_callback_listener kListener = {.done = widget_picker_caret_frame_done};
  app.widgetPickerCaretFrameCb = wl_surface_frame(app.surface);
  wl_callback_add_listener(app.widgetPickerCaretFrameCb, &kListener, &app);
}

void focus_search(App& app) {
  if (app.widgetPickerSearchActive) return;
  app.widgetPickerSearchActive = true;
  app.widgetPickerCaretBlinkHalf = eh::shell::monotonic_ms() / 530u;
  draw(app);
}

void widget_picker_caret_frame_done(void* data, wl_callback* cb, uint32_t) {
  auto& app = *static_cast<App*>(data);
  wl_callback_destroy(cb);
  app.widgetPickerCaretFrameCb = nullptr;
  if (!app.surface || !app.widgetPickerOpen || !app.widgetPickerSearchActive) return;
  constexpr uint64_t kHalfMs = 530;
  const uint64_t half = eh::shell::monotonic_ms() / kHalfMs;
  if (half != app.widgetPickerCaretBlinkHalf) {
    app.widgetPickerCaretBlinkHalf = half;
    draw(app);
    return;
  }
  queue_caret_frame(app);
}

static const char* widget_display_desc(const std::string& id) {
  if (id == "pinned_apps") return "Shows pinned apps (and running indicators)";
  if (id == "running_apps") return "Shows running applications with focus indication";
  if (eh::config::widget_token_is_system_tray(id)) return "System notification area icons";
  if (id == "settings_button") return "Quick access to this settings window";
  if (id == "distro_spotlight")
    return "Compact search with distro logo (dock Spotlight), not the full app drawer";
  if (id == "app_menu") return "Nine-dot icon; opens the full start-menu style app drawer";
  if (id == "app_drawer") return "OS logo; opens the same full app drawer as App menu";
  if (eh::config::widget_implementation_type(id) == "smenu")
    return "Start menu icon; opens search + app grid (no pinned, no power buttons)";
  if (id == "launchpad")
    return "Full-screen app grid (same launcher UI as the app menu, edge-to-edge layer)";
  if (id == "clock") return "Time and date (12h by default); optional [widget.clock] in TOML";
  if (id == "weather")
    return "Open-Meteo strip (icon, temp, unit); [widget.weather] weather_lat/lon or weather_city, weather_unit c|f";
  if (id == "media") return "MPRIS now-playing (title/artist, prev/play/next); optional [widget.media] in TOML";
  if (id == "workspaces")
    return "Hyprland workspace strip (hyprctl); optional [widget.workspaces] display=id|name|none";
  if (id == "world_clock")
    return "Multi-timezone clock w/ neon retro style; configurable via [widget.world_clock] in TOML";
  if (id == "control_center")
    return "Status pill icons (ethernet/audio/mic/bluetooth); optional [widget.control_center] show_* booleans";
  if (id == "notifications")
    return "Bell opens Control Center notifications; right-click toggles do-not-disturb";
  if (id == "spacer" || eh::config::widget_implementation_type(id) == "spacer")
    return "Empty slot one icon wide; use between dock groups instead of automatic gaps";
  if (id == "trash" || eh::config::widget_implementation_type(id) == "trash")
    return "Trash can icon (empty/full); right-click for Empty Trash";
  if (id == "volume_mixer" || eh::config::widget_implementation_type(id) == "volume_mixer")
    return "Volume mixer popup with device/app streams, sliders, mute, and EQ";
  if (id == "vpn" || eh::config::widget_implementation_type(id) == "vpn")
    return "VPN connection popup with connect/disconnect";
  if (id == "battery" || eh::config::widget_implementation_type(id) == "battery")
    return "Battery percentage and charging state from UPower";
  if (id == "bluetooth" || eh::config::widget_implementation_type(id) == "bluetooth")
    return "Bluetooth adapter status, connected devices, and power toggle";
  if (id == "weather_fancy")
    return "Detailed weather with animated icons, feels-like, wind, humidity";
  if (id == "calendar")
    return "Monthly calendar popup with date navigation and events";
  if (id == "system_monitor")
    return "CPU, memory, disk, and network usage graphs";
  if (id == "media_compact")
    return "Compact MPRIS pill (album art, title/artist, prev/play/next)";
  return "";
}

// M3 design constants.
constexpr int kPickerPad = 20;
constexpr int kPickerHeaderH = 56;
constexpr int kPickerGapSearch = 12;
constexpr int kPickerSearchH = 44;
constexpr int kPickerGapGrid = 12;
constexpr int kPickerFooterH = 22;
constexpr double kPickerPanelR = 16.0;
constexpr double kPickerCardR = 12.0;
constexpr int kPickerCardPad = 16;
constexpr int kPickerIconBox = 38;
constexpr double kPickerIconR = 10.0;

static void picker_card_src(const App& app, float* r, float* g, float* b) {
  if (app.drawChromeMatugen) {
    *r = static_cast<float>(app.drawChrome.drawerDimR);
    *g = static_cast<float>(app.drawChrome.drawerDimG);
    *b = static_cast<float>(app.drawChrome.drawerDimB);
  } else {
    *r = static_cast<float>(Theme::BgR) * 0.88f;
    *g = static_cast<float>(Theme::BgG) * 0.90f;
    *b = static_cast<float>(Theme::BgB) * 0.95f;
  }
}

static void picker_icon_box(App& app, cairo_t* cr, double x, double y, double sz, const char* ligature) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  const float ir = std::min(1.0f, a_r * 0.45f + 0.42f);
  const float ig = std::min(1.0f, a_g * 0.45f + 0.42f);
  const float ib = std::min(1.0f, a_b * 0.45f + 0.42f);
  m3::Box ibox;
  ibox.setColor(ir, ig, ib, 0.85f);
  ibox.setRadius(static_cast<float>(kPickerIconR));
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

// Word-wrapped Inter text: drawn top-aligned at (x, y), wrapped to wrapW px.
static void picker_show_wrapped(cairo_t* cr, double x, double y, const char* text,
                                float fontSize, int weight, float r, float g, float b, float a,
                                int wrapW, bool ellipsize = false) {
  if (!text || !text[0]) return;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  if (wrapW > 0) {
    pango_layout_set_width(layout, wrapW * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  }
  if (ellipsize) pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
}

static int picker_wrapped_line_count(const char* text, float fontSize, int weight, int wrapW) {
  if (!text || !text[0]) return 1;
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    if (surf) cairo_surface_destroy(surf);
    return 1;
  }
  cairo_t* cr = cairo_create(surf);
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  if (wrapW > 0) {
    pango_layout_set_width(layout, wrapW * PANGO_SCALE);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  }
  const int lines = std::max(1, pango_layout_get_line_count(layout));
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return lines;
}

// Cached per-widget max(title width, description width) at the fonts used in the cards.
static int picker_wid_text_width(const std::string& wid) {
  static std::unordered_map<std::string, int> s_cache;
  auto it = s_cache.find(wid);
  if (it != s_cache.end()) return it->second;
  const int tw = picker_text_width_px(widget_display_title(wid).c_str(), 14, 600);
  const int dw = picker_text_width_px(widget_display_desc(wid), 11, 400);
  const int m = std::max(tw, dw);
  s_cache.emplace(wid, m);
  return m;
}

// Cached wrapped line count for a widget's description at a given wrap width.
static int picker_desc_line_count_cached(const std::string& wid, int wrapW) {
  static int s_wrapW = -1;
  static std::unordered_map<std::string, int> s_cache;
  if (s_wrapW != wrapW) {
    s_cache.clear();
    s_wrapW = wrapW;
  }
  auto it = s_cache.find(wid);
  if (it != s_cache.end()) return it->second;
  const int n = picker_wrapped_line_count(widget_display_desc(wid), 11, 400, wrapW);
  s_cache.emplace(wid, n);
  return n;
}

static const char* const kDesktopOnlyPickIds[] = {
    "weather_fancy", "calendar", "system_monitor", "media_compact",
};
static constexpr int kDesktopOnlyPickCount =
    static_cast<int>(sizeof(kDesktopOnlyPickIds) / sizeof(kDesktopOnlyPickIds[0]));

static bool is_desktop_widget_id(const std::string& id) {
  return id == "clock" || id == "weather" || id == "weather_fancy" ||
         id == "calendar" || id == "system_monitor" || id == "media" ||
         id == "media_compact" || id == "world_clock";
}

static bool widget_matches_filter(const std::string& id, const std::string& needle) {
  if (needle.empty()) return true;
  std::string hay = widget_display_title(id);
  hay.push_back(' ');
  hay += widget_display_desc(id);
  for (char& c : hay) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return hay.find(needle) != std::string::npos;
}

std::vector<int> visible_indices(const App& app) {
  std::vector<int> vis;
  std::string needle = app.widgetPickerFilter;
  for (char& c : needle) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (int i = 0; i < kDockWidgetPickCount; ++i) {
    if (app.widgetPickerForDesktop && !is_desktop_widget_id(std::string(kDockWidgetPickIds[i])))
      continue;
    if (widget_matches_filter(std::string(kDockWidgetPickIds[i]), needle))
      vis.push_back(i);
  }
  if (app.widgetPickerForDesktop) {
    for (int i = 0; i < kDesktopOnlyPickCount; ++i) {
      const std::string wid = kDesktopOnlyPickIds[i];
      if (widget_matches_filter(wid, needle))
        vis.push_back(kDockWidgetPickCount + i);
    }
  }
  return vis;
}

WidgetPickerLayout layout_for(App& app, int nVisible, int viewportW, int viewportH,
                              int settingsContentInsetX) {
  WidgetPickerLayout L{};
  constexpr int pad = kPickerPad;
  constexpr int headerH = kPickerHeaderH;
  constexpr int gapSearch = kPickerGapSearch;
  constexpr int searchH = kPickerSearchH;
  constexpr int gapGrid = kPickerGapGrid;
  constexpr int footerH = kPickerFooterH;
  constexpr int colGap = 12;
  constexpr int rowGap = 12;

  const int contentAvailW = settingsContentInsetX < 0
                                ? viewportW - 48
                                : std::max(120, viewportW - settingsContentInsetX - 24);
  const int viewportCap = std::max(360, contentAvailW);

  // Width: grow from measured text so titles and descriptions have room.
  const std::vector<int> visL = visible_indices(app);
  int maxTextW = 0;
  for (int vi : visL)
    maxTextW = std::max(maxTextW, picker_wid_text_width(widget_id_for_visible_index(vi)));
  const int cardWNeeded = kPickerCardPad + kPickerIconBox + 12 + maxTextW + 12 + 24 + kPickerCardPad;
  const int desiredPw = 2 * cardWNeeded + colGap + 2 * pad;
  const int pw = std::clamp(desiredPw, std::min(500, viewportCap), viewportCap);
  const int cardW = std::max(170, (pw - pad * 2 - colGap) / 2);

  // Height: wrap each description to the actual card text width and size the
  // cards uniformly to the longest one.
  const int descWrapW =
      std::max(60, cardW - (kPickerCardPad + kPickerIconBox + 12) - (12 + 24 + kPickerCardPad));
  int descLines = 1;
  for (int vi : visL)
    descLines = std::max(descLines, picker_desc_line_count_cached(widget_id_for_visible_index(vi), descWrapW));
  descLines = std::clamp(descLines, 1, 3);
  const int cardH = std::clamp(52 + descLines * 15, 72, 96);

  const int maxPh = std::max(300, std::min(viewportH - 48, (viewportH * 82) / 100));
  const int chrome = pad + headerH + gapSearch + searchH + gapGrid + footerH + pad;
  const int gridAvail = std::max(80, maxPh - chrome);

  const int rows = nVisible <= 0 ? 0 : (nVisible + 1) / 2;
  const int gridContentH = rows <= 0 ? 0 : rows * cardH + (rows - 1) * rowGap;
  const int gridClipH = rows <= 0 ? std::min(gridAvail, 150) : std::min(gridContentH, gridAvail);
  const int gridScrollMax = std::max(0, gridContentH - gridClipH);
  app.widgetPickerScrollPx = std::clamp(app.widgetPickerScrollPx, 0, gridScrollMax);

  const int ph = pad + headerH + gapSearch + searchH + gapGrid + gridClipH + footerH + pad;

  L.pw = pw;
  L.ph = ph;
  if (settingsContentInsetX < 0) {
    L.px = std::max(pad / 2, (viewportW - L.pw) / 2);
  } else {
    const int contentW = viewportW - settingsContentInsetX - 16;
    L.px = settingsContentInsetX + std::max(0, contentW - L.pw) / 2;
  }
  L.py = std::max(16, (viewportH - L.ph) / 2);

  L.closeX = L.px + L.pw - pad - L.closeW;
  L.closeY = L.py + pad - 2;
  L.searchX = L.px + pad;
  L.searchY = L.py + pad + headerH;
  L.searchW = L.pw - pad * 2;
  L.searchH = searchH;
  L.clearX = L.searchX + L.searchW - 14 - L.clearW;
  L.clearY = L.searchY + (searchH - L.clearH) / 2;
  L.gridX = L.px + pad;
  L.gridY = L.searchY + L.searchH + gapGrid;
  L.gridW = L.pw - pad * 2;
  L.gridClipH = gridClipH;
  L.gridContentH = gridContentH;
  L.gridScrollMax = gridScrollMax;
  L.cardW = cardW;
  L.cardH = cardH;
  L.colGap = colGap;
  L.rowGap = rowGap;

  return L;
}

void paint(App& app, cairo_t* cr, int viewportW, int viewportH, int settingsContentInsetX) {
  const eh::config::ShellConfig& scPick0 = eh::config::shell_config_snapshot();
  app.drawChromeMatugen = scPick0.appearance.anyPaletteActive();
  app.drawChrome = eh::config::derived_chrome_colors(scPick0.appearance);

  const std::vector<int> vis = visible_indices(app);
  const WidgetPickerLayout pk =
      layout_for(app, static_cast<int>(vis.size()), viewportW, viewportH, settingsContentInsetX);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.38);
  cairo_rectangle(cr, 0, 0, viewportW, viewportH);
  cairo_fill(cr);

  cairo_save(cr);
  cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
  {
    m3::Box b;
    b.setGeometry(static_cast<float>(pk.px), static_cast<float>(pk.py),
                  static_cast<float>(pk.pw), static_cast<float>(pk.ph));
    b.setRadius(static_cast<float>(kPickerPanelR));
    if (app.drawChromeMatugen)
      b.setColor(static_cast<float>(app.drawChrome.panelFillR),
                 static_cast<float>(app.drawChrome.panelFillG),
                 static_cast<float>(app.drawChrome.panelFillB),
                 0.96f);
    else
      b.setColor(static_cast<float>(Theme::BgR), static_cast<float>(Theme::BgG),
                 static_cast<float>(Theme::BgB), 0.96f);
    b.setGlassy(true);
    b.paint(cr);
  }
  cairo_restore(cr);

  // Header.
  const int hx = pk.px + kPickerPad;
  const int hy = pk.py + kPickerPad;
  picker_icon_box(app, cr, hx, hy + 3.0, static_cast<double>(kPickerIconBox), "widgets");
  settings_show_text(cr, hx + kPickerIconBox + 12, hy + 23, "Add widget", 16, 700, t_r, t_g, t_b, 0.95);
  {
    int nWidgets = app.widgetPickerForDesktop ? static_cast<int>(vis.size()) : kDockWidgetPickCount;
    std::string sub = std::to_string(nWidgets) + " widgets available";
    settings_show_text(cr, hx + kPickerIconBox + 12, hy + 40, sub.c_str(), 11, 400, t_r, t_g, t_b, 0.55);
  }

  // Close button.
  {
    const bool closeHovered =
        point_in_rect(app.pointerX, app.pointerY, pk.closeX, pk.closeY, pk.closeW, pk.closeH);
    m3::Button closeBtn;
    closeBtn.setMinSize(0, 0);
    closeBtn.setGlyph("close");
    closeBtn.setGeometry(static_cast<float>(pk.closeX), static_cast<float>(pk.closeY),
                         static_cast<float>(pk.closeW), static_cast<float>(pk.closeH));
    closeBtn.setStyle(m3::Button::Style::Outlined);
    closeBtn.setSize(m3::Button::Size::XS);
    closeBtn.setAccentColor(a_r, a_g, a_b);
    closeBtn.setOutlineColor(o_r, o_g, o_b);
    closeBtn.setHovered(closeHovered);
    closeBtn.paint(cr);
  }

  // Search field.
  {
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_GOOD);
    {
      m3::Box b;
      b.setGeometry(static_cast<float>(pk.searchX), static_cast<float>(pk.searchY),
                    static_cast<float>(pk.searchW), static_cast<float>(pk.searchH));
      b.setRadius(12.0f);
      float br, bg, bb;
      picker_card_src(app, &br, &bg, &bb);
      b.setColor(br, bg, bb, 1.0f);
      b.setGlassy(true);
      b.paint(cr);
    }
    cairo_restore(cr);

    material_symbols_draw_glyph(cr, static_cast<double>(pk.searchX + 18),
                                static_cast<double>(pk.searchY) + static_cast<double>(pk.searchH) * 0.5 + 0.5,
                                18.0, "search", t_r, t_g, t_b, 0.5);
    const int textX = pk.searchX + 46;
    const int textY = pk.searchY + (pk.searchH + 15) / 2;

    // Text + caret, clipped so long input never runs under the clear chip.
    cairo_save(cr);
    cairo_rectangle(cr, textX, pk.searchY + 6, pk.clearX - 6 - textX, pk.searchH - 12);
    cairo_clip(cr);
    if (app.widgetPickerFilter.empty()) {
      if (!app.widgetPickerSearchActive)
        settings_show_text(cr, textX, textY, "Search widgets by name or description...",
                           13, 400, t_r, t_g, t_b, 0.42);
    } else {
      settings_show_text(cr, textX, textY, app.widgetPickerFilter.c_str(),
                         13, 400, t_r, t_g, t_b, 0.92);
    }
    if (app.widgetPickerSearchActive) {
      const bool caretOn = eh::shell::text_caret_blink_on(eh::shell::monotonic_ms(), true);
      if (caretOn) {
        const double caretX =
            static_cast<double>(textX) +
            static_cast<double>(picker_text_width_px(app.widgetPickerFilter.c_str(), 13, 400)) + 1.5;
        cairo_set_line_width(cr, 1.6);
        cairo_set_source_rgba(cr, t_r, t_g, t_b, 0.85);
        cairo_move_to(cr, caretX, static_cast<double>(pk.searchY) + 12.0);
        cairo_line_to(cr, caretX, static_cast<double>(pk.searchY) + static_cast<double>(pk.searchH) - 12.0);
        cairo_stroke(cr);
      }
    }
    cairo_restore(cr);

    if (!app.widgetPickerFilter.empty()) {
      const bool clearHover =
          point_in_rect(app.pointerX, app.pointerY, pk.clearX, pk.clearY, pk.clearW, pk.clearH);
      m3::Box cb;
      if (clearHover)
        cb.setColor(a_r, a_g, a_b, 0.92f);
      else
        cb.setColor(t_r, t_g, t_b, 0.08f);
      cb.setRadius(13.0f);
      cb.setGeometry(static_cast<float>(pk.clearX), static_cast<float>(pk.clearY),
                     static_cast<float>(pk.clearW), static_cast<float>(pk.clearH));
      cb.setGlassy(true);
      cb.paint(cr);
      const double gcx = static_cast<double>(pk.clearX) + static_cast<double>(pk.clearW) * 0.5;
      const double gcy = static_cast<double>(pk.clearY) + static_cast<double>(pk.clearH) * 0.5 + 0.5;
      if (clearHover)
        material_symbols_draw_glyph(cr, gcx, gcy, 16.0, "close", 1, 1, 1, 0.95);
      else
        material_symbols_draw_glyph(cr, gcx, gcy, 16.0, "close", t_r, t_g, t_b, 0.65);
    }
  }

  // Widget grid (clipped + scrollable).
  cairo_save(cr);
  cairo_rectangle(cr, pk.gridX, pk.gridY, pk.gridW, pk.gridClipH);
  cairo_clip(cr);

  for (int slot = 0; slot < static_cast<int>(vis.size()); ++slot) {
    const int row = slot / 2;
    const int col = slot % 2;
    const int cx = pk.gridX + col * (pk.cardW + pk.colGap);
    const int cyTop = pk.gridY + row * (pk.cardH + pk.rowGap) - app.widgetPickerScrollPx;
    if (cyTop + pk.cardH < pk.gridY || cyTop >= pk.gridY + pk.gridClipH) continue;
    const bool hover = app.widgetPickerHoverSlot == slot;

    {
      m3::Box b;
      b.setGeometry(static_cast<float>(cx), static_cast<float>(cyTop),
                    static_cast<float>(pk.cardW), static_cast<float>(pk.cardH));
      b.setRadius(static_cast<float>(kPickerCardR));
      float br, bg, bb;
      picker_card_src(app, &br, &bg, &bb);
      if (hover) {
        br = std::min(1.0f, br + (a_r - br) * 0.16f);
        bg = std::min(1.0f, bg + (a_g - bg) * 0.16f);
        bb = std::min(1.0f, bb + (a_b - bb) * 0.16f);
      }
      b.setColor(br, bg, bb, 1.0f);
      b.setGlassy(true);
      b.paint(cr);
    }
    if (hover) {
      cairo_round_rect(cr, cx + 0.5, cyTop + 0.5, pk.cardW - 1, pk.cardH - 1, kPickerCardR);
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.55);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    const std::string wid = widget_id_for_visible_index(vis[static_cast<size_t>(slot)]);
    const double iconX = static_cast<double>(cx) + kPickerCardPad;
    const double iconY = static_cast<double>(cyTop) + (static_cast<double>(pk.cardH) - kPickerIconBox) * 0.5;
    picker_icon_box(app, cr, iconX, iconY, static_cast<double>(kPickerIconBox),
                    dock_widget_material_ligature(wid));

    const int tx = cx + kPickerCardPad + kPickerIconBox + 12;
    const int titleWrapW = pk.cardW - (kPickerCardPad + kPickerIconBox + 12) - kPickerCardPad;
    const int descWrapW = pk.cardW - (kPickerCardPad + kPickerIconBox + 12) - (12 + 24 + kPickerCardPad);
    cairo_save(cr);
    cairo_rectangle(cr, tx, cyTop + 14, pk.cardW - kPickerCardPad - (tx - cx), pk.cardH - 26);
    cairo_clip(cr);
    picker_show_wrapped(cr, tx, cyTop + 15, widget_display_title(wid).c_str(),
                        14, 600, t_r, t_g, t_b, hover ? 1.0f : 0.92f, titleWrapW, true);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_rectangle(cr, tx, cyTop + 34, pk.cardW - kPickerCardPad - (tx - cx), pk.cardH - 46);
    cairo_clip(cr);
    picker_show_wrapped(cr, tx, cyTop + 36, widget_display_desc(wid), 11, 400,
                        t_r, t_g, t_b, 0.5, descWrapW, true);
    cairo_restore(cr);

    const double abx = static_cast<double>(cx) + pk.cardW - 16 - 24;
    const double aby = static_cast<double>(cyTop) + (static_cast<double>(pk.cardH) - 24.0) * 0.5;
    {
      m3::Box ab;
      if (hover)
        ab.setColor(a_r, a_g, a_b, 0.92f);
      else
        ab.setColor(t_r, t_g, t_b, 0.07f);
      ab.setRadius(12.0f);
      ab.setGeometry(static_cast<float>(abx), static_cast<float>(aby), 24.0f, 24.0f);
      ab.setGlassy(true);
      ab.paint(cr);
    }
    if (hover)
      material_symbols_draw_glyph(cr, abx + 12.0, aby + 12.5, 18.0, "add", 1, 1, 1, 0.95);
    else
      material_symbols_draw_glyph(cr, abx + 12.0, aby + 12.5, 18.0, "add", t_r, t_g, t_b, 0.65);
  }

  if (vis.empty()) {
    const double ex = static_cast<double>(pk.gridX) + static_cast<double>(pk.gridW) * 0.5;
    material_symbols_draw_glyph(cr, ex, static_cast<double>(pk.gridY) + 30, 40.0,
                                "search_off", t_r, t_g, t_b, 0.22);
    const int tw = picker_text_width_px("No matching widgets", 14, 600);
    settings_show_text(cr, ex - static_cast<double>(tw) * 0.5, pk.gridY + 64,
                       "No matching widgets", 14, 600, t_r, t_g, t_b, 0.6);
    const int sw = picker_text_width_px("Try a different search term", 11, 400);
    settings_show_text(cr, ex - static_cast<double>(sw) * 0.5, pk.gridY + 86,
                       "Try a different search term", 11, 400, t_r, t_g, t_b, 0.4);
  }

  if (pk.gridScrollMax > 0) {
    const double sbX = static_cast<double>(pk.gridX + pk.gridW - 7);
    const double thumbH = std::max(24.0, static_cast<double>(pk.gridClipH) *
                                              static_cast<double>(pk.gridClipH) /
                                              static_cast<double>(pk.gridContentH));
    const double norm = static_cast<double>(app.widgetPickerScrollPx) /
                        static_cast<double>(pk.gridScrollMax);
    const double thumbY = static_cast<double>(pk.gridY) + (static_cast<double>(pk.gridClipH) - thumbH) * norm;
    cairo_set_source_rgba(cr, t_r, t_g, t_b, 0.30);
    cairo_round_rect(cr, sbX, thumbY, 4.0, thumbH, 2.0);
    cairo_fill(cr);
  }

  cairo_restore(cr);

  // Footer.
  const int footY = pk.py + pk.ph - kPickerFooterH + 2;
  const int fw = picker_text_width_px("Esc or click outside to close", 11, 400);
  settings_show_text(cr, pk.px + (pk.pw - fw) / 2, footY,
                     "Esc or click outside to close", 11, 400, t_r, t_g, t_b, 0.45);
}

void teardown_layer(App& app) {
  debug_log("settings", "widget_picker teardown_layer");
  app.widgetPickerWinW = 0;
  app.widgetPickerWinH = 0;
}

bool try_create_layer(App& app) {
  {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "widget_picker try_create_layer: open=%d", app.widgetPickerOpen ? 1 : 0);
    debug_log("settings", "%s", buf);
  }
  return app.widgetPickerOpen;
}

void present_layer_if_needed(App&) {
}

void close_picker(App& app) {
  debug_log("settings", "widget_picker close_picker");
  app.widgetPickerOpen = false;
  app.widgetPickerFilter.clear();
  app.widgetPickerHoverSlot = -1;
  app.widgetPickerScrollPx = 0;
  app.widgetPickerSearchActive = false;
  app.widgetPickerCaretBlinkHalf = static_cast<uint64_t>(-1);
  destroy_caret_frame(app);
  app.widgetPickerForDesktop = false;
  app.widgetPickerForPanel = false;
  app.widgetPickerForTaskbar = false;
  teardown_layer(app);
}

[[nodiscard]] std::string widget_id_for_visible_index(int vi) {
  if (vi < kDockWidgetPickCount)
    return kDockWidgetPickIds[vi];
  const int di = vi - kDockWidgetPickCount;
  if (di >= 0 && di < kDesktopOnlyPickCount)
    return kDesktopOnlyPickIds[di];
  return {};
}

void viewport_for_pointer(const App& app, wl_surface*, int* vw, int* vh, int* inset) {
  *vw = app.width;
  *vh = app.height;
  *inset = kSpacingL + kSidebarW + kSpacingL;
}

}
