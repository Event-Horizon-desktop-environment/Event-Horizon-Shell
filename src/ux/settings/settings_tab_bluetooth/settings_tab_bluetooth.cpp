#include <cairo/cairo.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "m3/controls/containers/button.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_bluetooth/settings_tab_bluetooth.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kDevRowH = 56;
static constexpr int kPowerCardH = 52 + 2 * kSliderRowH + kSpacingXL;
static constexpr int kBtnRowH = 36;
static constexpr int kBtnH = 28;

static constexpr int kPowerCardTop = kCardTop;
static constexpr int kScanCardTop = kPowerCardTop + kPowerCardH + kCardGap;
static constexpr int kScanBodyTop = kScanCardTop + 52;
static constexpr int kScanCardH = 52 + kBtnRowH + kSpacingXL;

// ── Sizing helpers ────────────────────────────────────────────────────────
// m3::Button::setGeometry auto-expands a label that doesn't fit to
// labelWidth + 2*kAutoPad and re-centres the button on its requested x —
// so two right-aligned pills sized too small slide into each other. Buttons
// here are sized from the measured label (m3::Label for XS = "plain" 12px
// @400) plus headroom, so the auto-expand never fires and the painted
// geometry stays identical to the hit-testing rects.

static double m3_xs_label_w(const char* text) {
  auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* cr = cairo_create(surface);
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "plain");
  pango_font_description_set_size(desc, static_cast<int>(12.0f * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(400));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  (void)ph;
  return static_cast<double>(pw);
}

// Must be >= measured label + 2*kAutoPad (20px) or m3 expands + re-centres.
static double settings_btn_w(const char* label) {
  return m3_xs_label_w(label) + 24.0;
}

// Measure a text string the way settings_show_text renders it ("Inter").
static double pango_text_w(const char* text, float fontSize, int weight) {
  auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto* cr = cairo_create(surface);
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(weight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  int pw = 0, ph = 0;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  (void)ph;
  return static_cast<double>(pw);
}

// settings_show_text with a max width (ellipsizes instead of wrapping).
static void settings_show_text_w(cairo_t* cr, double x, double y, const char* text, float fontSize,
                                 int fontWeight, double maxW, float r, float g, float b, float a) {
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_new();
  pango_font_description_set_family(desc, "Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, text, -1);
  if (maxW > 0.0) {
    pango_layout_set_width(layout, static_cast<int>(maxW * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  }
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  cairo_save(cr);
  cairo_translate(cr, x, y - static_cast<double>(ph) + 2.0);
  cairo_set_source_rgba(cr, r, g, b, a);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
  pango_font_description_free(desc);
  g_object_unref(layout);
}

// Battery level → material glyph + colour (matches the dock widget).
static const char* bt_tab_battery_glyph(int pct) {
  if (pct >= 95) return "battery_full";
  if (pct >= 80) return "battery_6_bar";
  if (pct >= 60) return "battery_5_bar";
  if (pct >= 40) return "battery_4_bar";
  if (pct >= 20) return "battery_3_bar";
  if (pct >= 10) return "battery_2_bar";
  if (pct >= 5)  return "battery_1_bar";
  return "battery_0_bar";
}

static void bt_tab_battery_color(int pct, float& r, float& g, float& b) {
  if (pct <= 20)      { r = 1.00f; g = 0.43f; b = 0.38f; }
  else if (pct <= 50) { r = 0.96f; g = 0.72f; b = 0.30f; }
  else                { r = 0.45f; g = 0.85f; b = 0.50f; }
}

// Display order for the device list: the connected device first, then other
// remembered (paired) devices, then nearby unpaired ones. Both the paint and
// the click handler must use this so hit-testing lines up with what is drawn.
static std::vector<eh::bt::DeviceInfo> bt_tab_ordered_devices(
    const std::vector<eh::bt::DeviceInfo>& devices) {
  std::vector<eh::bt::DeviceInfo> out = devices;
  std::stable_sort(out.begin(), out.end(),
                   [](const eh::bt::DeviceInfo& a, const eh::bt::DeviceInfo& b) {
                     auto rank = [](const eh::bt::DeviceInfo& d) {
                       if (d.connected) return 0;
                       if (d.paired) return 1;
                       return 2;
                     };
                     return rank(a) < rank(b);
                   });
  return out;
}

// ── Sectioned device list ────────────────────────────────────────────────
// The list is split into "CONNECTED" / "REMEMBERED DEVICES" / "NEARBY DEVICES"
// groups (headers only when more than one group is present). A plan is shared
// by the paint, click and hover passes so the geometry can never drift.

static constexpr int kSecH = 30;

struct BtTabEntry {
  bool header = false;
  const char* label = nullptr;
  int dev = -1;   // index into the ordered device vector
};

static std::vector<BtTabEntry> bt_tab_plan(
    const std::vector<eh::bt::DeviceInfo>& ordered) {
  std::vector<BtTabEntry> out;
  bool has_connected = false, has_remembered = false, has_nearby = false;
  for (const auto& d : ordered) {
    if (d.connected) has_connected = true;
    else if (d.paired) has_remembered = true;
    else has_nearby = true;
  }
  const bool show_headers =
      (has_connected ? 1 : 0) + (has_remembered ? 1 : 0) + (has_nearby ? 1 : 0) > 1;
  int last_rank = -1;
  for (int i = 0; i < static_cast<int>(ordered.size()); ++i) {
    const int rank = ordered[static_cast<size_t>(i)].connected
                         ? 0
                         : (ordered[static_cast<size_t>(i)].paired ? 1 : 2);
    if (show_headers && rank != last_rank) {
      const char* lbl = rank == 0 ? "CONNECTED"
                                  : (rank == 1 ? "REMEMBERED DEVICES" : "NEARBY DEVICES");
      out.push_back({true, lbl, -1});
      last_rank = rank;
    }
    out.push_back({false, nullptr, i});
  }
  return out;
}

static int devices_content_height(const std::vector<BtTabEntry>& plan) {
  if (plan.empty()) return 60;
  int h = kSpacingXL;
  for (const auto& e : plan) h += e.header ? kSecH : kDevRowH;
  return h;
}

// Top edge of the device list content (shared with the hover handler).
static constexpr int bt_tab_list_top() {
  return kScanCardTop + kScanCardH + kCardGap + 52;
}

// Ordered-device index under the pointer (page coords incl. scroll), or -1.
static int bt_tab_hover_row(App& app, int contentX, int contentW,
                            const std::vector<BtTabEntry>& plan) {
  const int cardX = contentX + 8;
  const double py = app.pointerY + settings_scroll_px(app);
  int y = bt_tab_list_top();
  for (const auto& e : plan) {
    if (e.header) {
      y += kSecH;
      continue;
    }
    if (point_in_rect(app.pointerX, py, cardX + kCardPad, y,
                      contentW - 4 * kCardPad, kDevRowH)) {
      return e.dev;
    }
    y += kDevRowH;
  }
  return -1;
}

void paint_bluetooth_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
    
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  bt.set_auto_reconnect(app.settings.btAutoReconnect);
  const auto state = bt.full_state();
  const auto& snap = state.snap;
  const auto devices = bt_tab_ordered_devices(state.devices);

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  const double pyC = app.pointerY + settings_scroll_px(app);
  int yPos = kBodyTop;

  // Power card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kPowerCardTop),
                static_cast<double>(cardW), static_cast<double>(kPowerCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kPowerCardTop + 22, "ADAPTER");
  settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(yPos),
                 "Bluetooth", snap.available ? (snap.powered ? "Powered on" : "Powered off") : "No adapter found");
  settings_toggle(app, cr, cardX, yPos, cardW,
                   static_cast<double>(yPos - 20), 26.0,
                   snap.available && snap.powered, 0.0);
  yPos += kSliderRowH;

  settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(yPos),
                 "Auto-reconnect", "Reconnect devices you have used before when they are in range");
  settings_toggle(app, cr, cardX, yPos, cardW,
                   static_cast<double>(yPos - 20), 26.0,
                   app.settings.btAutoReconnect, 0.0);
  yPos += kSliderRowH;

  if (snap.available) {
    // Scan card.
    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kScanCardTop),
                  static_cast<double>(cardW), static_cast<double>(kScanCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kScanCardTop + 22, "DISCOVERY");

    const double btnW = 120.0;
    const double btnH = kBtnH;
    const double btnX = cardX + cardW - btnW - kCardPad;
    const double btnY = kScanBodyTop;

    const char* scanLbl = snap.scanning ? "Stop scan" : "Scan";
    const bool scan_hov = point_in_rect(app.pointerX, pyC,
                                         static_cast<int>(btnX), static_cast<int>(btnY),
                                         static_cast<int>(btnW), static_cast<int>(btnH));
    {
      m3::Button btn;
      btn.setMinSize(0, 0);
      btn.setLabel(scanLbl);
      btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                      static_cast<float>(btnW), static_cast<float>(btnH));
      btn.setStyle(m3::Button::Style::Outlined);
      btn.setSize(m3::Button::Size::XS);
      btn.setAccentColor(a_r, a_g, a_b);
      btn.setOutlineColor(o_r, o_g, o_b);
      btn.setHovered(scan_hov);
      btn.setEnabled(!snap.scanning);
      btn.paint(cr);
    }

    // Devices card.
    const int devCardTop = kScanCardTop + kScanCardH + kCardGap;
    const auto plan = bt_tab_plan(devices);
    const int devContentH = devices_content_height(plan);
    const int devCardH = 52 + devContentH;

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(devCardTop),
                  static_cast<double>(cardW), static_cast<double>(devCardH), glassOv);
    settings_cat_label(cr, static_cast<double>(cardX + kCardPad), devCardTop + 22, "DEVICES");

    const int listTop = devCardTop + 52;

    if (plan.empty()) {
      settings_show_text(cr, cardX + kCardPad, listTop + 24, snap.scanning ? "Scanning for devices..." : "No devices found", 13, 400, t_r, t_g, t_b, 0.42);
    } else {
      cairo_save(cr);
      cairo_rectangle(cr, static_cast<double>(cardX), static_cast<double>(listTop),
                      static_cast<double>(cardW), static_cast<double>(devCardH - 52));
      cairo_clip(cr);

      int cursorY = listTop;
      for (const auto& entry : plan) {
        if (entry.header) {
          // Group label (CONNECTED / REMEMBERED DEVICES / NEARBY DEVICES).
          settings_show_text(cr, cardX + kCardPad, cursorY + kSecH * 0.5 + 4.0, entry.label,
                             10, 700, t_r, t_g, t_b, 0.42);
          cursorY += kSecH;
          continue;
        }
        const auto& dev = devices[static_cast<size_t>(entry.dev)];
        const int rowY = cursorY;
        cursorY += kDevRowH;

        const bool hovered = (entry.dev == app.settingsBluetoothHoverRow);

        if (hovered) {
          cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
          cairo_rectangle(cr, static_cast<double>(cardX), static_cast<double>(rowY),
                          static_cast<double>(cardW), static_cast<double>(kDevRowH));
          cairo_fill(cr);
        }

        // Device type glyph
        const char* glyph = eh::bt::bluetooth_device_kind_glyph(dev.kind);
        eh::shell::draw_material_glyph(cr,
                                       cardX + kCardPad + 14.0,
                                       rowY + kDevRowH * 0.5,
                                       18.0, glyph, t_r, t_g, t_b, 0.80);

        // Action buttons, right-aligned. Sized from the measured label so the
        // m3 auto-expand in setGeometry (label + 2*kAutoPad, re-centred) never
        // fires — it would push the pills into each other.
        const double actBtnH = 24.0;
        const double actBtnY = static_cast<double>(rowY) + 3.0;
        double actBtnX = cardX + cardW - kCardPad - kSpacingS;
        double btnAreaLeft = actBtnX;
        if (dev.paired) {
          const double forgetBtnW = settings_btn_w("Forget");
          actBtnX -= forgetBtnW;
          btnAreaLeft = actBtnX;
          const bool fov = point_in_rect(app.pointerX, pyC,
                                          static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                                          static_cast<int>(forgetBtnW), static_cast<int>(actBtnH));
          m3::Button btn;
          btn.setMinSize(0, 0);
          btn.setLabel("Forget");
          btn.setGeometry(static_cast<float>(actBtnX), static_cast<float>(actBtnY),
                          static_cast<float>(forgetBtnW), static_cast<float>(actBtnH));
          btn.setStyle(m3::Button::Style::Outlined);
          btn.setSize(m3::Button::Size::XS);
          btn.setAccentColor(a_r, a_g, a_b);
          btn.setOutlineColor(o_r, o_g, o_b);
          btn.setHovered(fov);
          btn.paint(cr);
          actBtnX -= 10.0;
        }
        {
          const char* lbl = dev.connected ? "Disconnect" : "Connect";
          const double connBtnW = settings_btn_w(lbl);
          actBtnX -= connBtnW;
          btnAreaLeft = std::min(btnAreaLeft, actBtnX);
          const bool cov = point_in_rect(app.pointerX, pyC,
                                          static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                                          static_cast<int>(connBtnW), static_cast<int>(actBtnH));
          m3::Button btn;
          btn.setMinSize(0, 0);
          btn.setLabel(lbl);
          btn.setGeometry(static_cast<float>(actBtnX), static_cast<float>(actBtnY),
                          static_cast<float>(connBtnW), static_cast<float>(actBtnH));
          btn.setStyle(m3::Button::Style::Outlined);
          btn.setSize(m3::Button::Size::XS);
          btn.setAccentColor(a_r, a_g, a_b);
          btn.setOutlineColor(o_r, o_g, o_b);
          btn.setHovered(cov);
          btn.paint(cr);
        }

        // Device alias + battery — the level sits inline, right after the name.
        const double aliasX = cardX + kCardPad + 40.0;
        const double aliasMaxW = std::max(24.0, btnAreaLeft - 10.0 - aliasX);
        const double nameBaseline = static_cast<double>(rowY) + 22.0;
        const double aliasW = pango_text_w(dev.alias.c_str(), 14.0f, 400);
        if (dev.has_battery) {
          char batBuf[8];
          std::snprintf(batBuf, sizeof(batBuf), "%d%%", dev.battery_percent);
          const double batTxtW = pango_text_w(batBuf, 11.0f, 400);
          const double batBlockW = 8.0 + 14.0 + 4.0 + batTxtW;   // gap icon gap text
          const double nameW = std::max(24.0, aliasMaxW - batBlockW);
          settings_show_text_w(cr, aliasX, nameBaseline, dev.alias.c_str(), 14.0f, 400, nameW,
                               t_r, t_g, t_b, 0.93f);
          const double drawnNameW = std::min(aliasW, nameW);
          float batR = 0.45f, batG = 0.85f, batB = 0.50f;
          bt_tab_battery_color(dev.battery_percent, batR, batG, batB);
          eh::shell::draw_material_glyph(cr, aliasX + drawnNameW + 8.0 + 7.0, nameBaseline - 7.0, 14.0,
                                         bt_tab_battery_glyph(dev.battery_percent), batR, batG, batB, 0.92f);
          settings_show_text(cr, aliasX + drawnNameW + 8.0 + 14.0 + 4.0, nameBaseline, batBuf, 11.0f, 400,
                             batR, batG, batB, 0.92f);
        } else {
          settings_show_text_w(cr, aliasX, nameBaseline, dev.alias.c_str(), 14.0f, 400, aliasMaxW,
                               t_r, t_g, t_b, 0.93f);
        }

        // Kind + status text
        char statusBuf[128];
        if (dev.connected) {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s \u00b7 Connected",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        } else if (dev.paired) {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s \u00b7 Paired",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        } else {
          std::snprintf(statusBuf, sizeof(statusBuf), "%s",
                        eh::bt::bluetooth_device_kind_name(dev.kind));
        }
        settings_show_text(cr, cardX + kCardPad + 40.0, rowY + 40, statusBuf, 11, 400, t_r, t_g, t_b, 0.46);
      }
      cairo_restore(cr);
    }
  }
}

bool settings_bluetooth_consume_pointer_down(App& app, int contentX, int contentW) {
    
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  const auto state = bt.full_state();
  const auto& snap = state.snap;
  const auto devices = bt_tab_ordered_devices(state.devices);

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const double ly = app.pointerY + settings_scroll_px(app);

  // Power toggle
  {
    constexpr int swW = 52, swH = 26;
    const int swX = cardX + cardW - swW - kSpacingXL;
    const int swY = kBodyTop - 20;
    if (point_in_rect(app.pointerX, ly, swX, swY, swW, swH)) {
      bt.set_powered(!snap.powered);
      draw(app);
      return true;
    }
  }

  // Auto-reconnect toggle
  {
    constexpr int swW = 52, swH = 26;
    const int swX = cardX + cardW - swW - kSpacingXL;
    const int swY = kBodyTop + kSliderRowH - 20;
    if (point_in_rect(app.pointerX, ly, swX, swY, swW, swH)) {
      app.settings.btAutoReconnect = !app.settings.btAutoReconnect;
      bt.set_auto_reconnect(app.settings.btAutoReconnect);
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  if (!snap.available) return false;

  // Scan button
  {
    const double btnW = 120.0;
    const double btnH = kBtnH;
    const double btnX = cardX + cardW - btnW - kCardPad;
    const double btnY = kScanBodyTop;
    if (point_in_rect(app.pointerX, ly,
                       static_cast<int>(btnX), static_cast<int>(btnY),
                       static_cast<int>(btnW), static_cast<int>(btnH))) {
      if (snap.scanning)
        bt.stop_discovery();
      else
        bt.start_discovery();
      draw(app);
      return true;
    }
  }

  // Device list buttons
  const auto plan = bt_tab_plan(devices);
  int cursorY = bt_tab_list_top();

  for (const auto& entry : plan) {
    if (entry.header) {
      cursorY += kSecH;
      continue;
    }
    const auto& dev = devices[static_cast<size_t>(entry.dev)];
    const int rowY = cursorY;
    cursorY += kDevRowH;
    const double actBtnH = 24.0;
    const double actBtnY = static_cast<double>(rowY) + 3.0;
    double actBtnX = cardX + cardW - kCardPad - kSpacingS;

    // Forget button
    if (dev.paired) {
      const double forgetBtnW = settings_btn_w("Forget");
      actBtnX -= forgetBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(forgetBtnW), static_cast<int>(actBtnH))) {
        bt.forget_device(dev.path);
        draw(app);
        return true;
      }
      actBtnX -= 10.0;
    }

    // Connect/Disconnect button
    {
      const char* lbl = dev.connected ? "Disconnect" : "Connect";
      const double connBtnW = settings_btn_w(lbl);
      actBtnX -= connBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(connBtnW), static_cast<int>(actBtnH))) {
        if (dev.connected)
          bt.disconnect_device(dev.path);
        else
          bt.connect_device(dev.path);
        draw(app);
        return true;
      }
    }
  }

  return false;
}

// Real-time row hover, driven from the shared sectioned plan so it can never
// drift from what the paint pass draws (including section headers).
void settings_bluetooth_consume_pointer_move(App& app, int contentX, int contentW) {
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  const auto ordered = bt_tab_ordered_devices(bt.full_state().devices);
  const auto plan = bt_tab_plan(ordered);
  const int new_hover = bt_tab_hover_row(app, contentX, contentW, plan);
  if (new_hover != app.settingsBluetoothHoverRow) {
    app.settingsBluetoothHoverRow = new_hover;
    draw(app);
  }
}
