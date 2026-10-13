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
#include "desktop_shell/common/log/debug_log.hpp"

extern void draw(App& app);
extern void save_settings(const Settings& s);

static constexpr int kCardTop = kContentTop;
static constexpr int kBodyTop = kCardTop + 52;
static constexpr int kDevRowH = 64;
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

// RSSI → friendly signal quality (dBm numbers mean nothing to most users).
static const char* bt_tab_signal_name(int16_t rssi) {
  if (rssi >= -60) return "Excellent";
  if (rssi >= -70) return "Good";
  if (rssi >= -80) return "Fair";
  return "Weak";
}

// Rank for display order: in-flight/connected first, then remembered (paired),
// then nearby unpaired. Connecting devices sort with connected so the spinner
// stays at the top instead of jumping groups mid-operation.
static int bt_tab_rank(const eh::bt::DeviceInfo& d) {
  if (d.connected || d.connecting) return 0;
  if (d.paired) return 1;
  return 2;
}

// Display order for the device list. Both the paint and the click handler must
// use this so hit-testing lines up with what is drawn.
static bool bt_tab_has_name(const eh::bt::DeviceInfo& d) {
  return !d.alias.empty() && d.alias != d.address;
}

static std::vector<eh::bt::DeviceInfo> bt_tab_ordered_devices(
    const std::vector<eh::bt::DeviceInfo>& devices) {
  std::vector<eh::bt::DeviceInfo> out = devices;
  std::stable_sort(out.begin(), out.end(),
                   [](const eh::bt::DeviceInfo& a, const eh::bt::DeviceInfo& b) {
                     const int ra = bt_tab_rank(a), rb = bt_tab_rank(b);
                     if (ra != rb) return ra < rb;
                     // Within one group (matters for NEARBY): real names before
                     // anonymous LE beacons that only report a MAC address.
                     if (ra == 2) {
                       const bool na = bt_tab_has_name(a), nb = bt_tab_has_name(b);
                       if (na != nb) return na;
                     }
                     return false;
                   });
  return out;
}

// The tab scrolls (settings_app clips + translates by the tab scroll offset),
// so show every device instead of truncating anonymous LE noise. All three
// passes (paint/click/hover) must use this same helper.
static std::vector<eh::bt::DeviceInfo> bt_tab_visible_devices(
    const std::vector<eh::bt::DeviceInfo>& ordered, int& total_out) {
  total_out = static_cast<int>(ordered.size());
  return ordered;
}

// Second-line status: kind, connection state, signal/last-used hints, address.
// The address disambiguates same-name devices and aids debugging.
static void bt_tab_status_text(const eh::bt::DeviceInfo& dev, const std::string& last_addr,
                               char* out, size_t n) {
  const char* kind = eh::bt::bluetooth_device_kind_name(dev.kind);
  char extra[96] = "";
  if (dev.connected) {
    if (dev.has_rssi)
      std::snprintf(extra, sizeof(extra), " \u00b7 Signal %s", bt_tab_signal_name(dev.rssi));
  } else if (dev.connecting) {
    // no extra; "Connecting…" is the message
  } else if (dev.paired) {
    const bool is_last = !last_addr.empty() && dev.address == last_addr;
    if (is_last && dev.has_rssi)
      std::snprintf(extra, sizeof(extra), " \u00b7 Last used \u00b7 In range");
    else if (is_last)
      std::snprintf(extra, sizeof(extra), " \u00b7 Last used");
    else if (dev.has_rssi)
      std::snprintf(extra, sizeof(extra), " \u00b7 In range \u00b7 Signal %s",
                    bt_tab_signal_name(dev.rssi));
  } else {
    if (dev.has_rssi)
      std::snprintf(extra, sizeof(extra), " \u00b7 Signal %s", bt_tab_signal_name(dev.rssi));
    else
      std::snprintf(extra, sizeof(extra), " \u00b7 Nearby");
  }
  if (dev.connected)
    std::snprintf(out, n, "%s \u00b7 Connected%s \u00b7 %s", kind, extra,
                  dev.address.empty() ? "?" : dev.address.c_str());
  else if (dev.connecting)
    std::snprintf(out, n, "%s \u00b7 Connecting\u2026 \u00b7 %s", kind,
                  dev.address.empty() ? "?" : dev.address.c_str());
  else if (dev.paired)
    std::snprintf(out, n, "%s \u00b7 Paired%s \u00b7 %s", kind, extra,
                  dev.address.empty() ? "?" : dev.address.c_str());
  else
    std::snprintf(out, n, "%s%s \u00b7 %s", kind, extra,
                  dev.address.empty() ? "?" : dev.address.c_str());
}

// Primary action label per device state. Unpaired devices pair (the old code
// offered "Connect" for those, which BlueZ always rejects); connecting rows
// get a disabled pill instead of a live button.
static const char* bt_tab_action_label(const eh::bt::DeviceInfo& dev) {
  if (dev.connected) return "Disconnect";
  if (dev.connecting) return "Connecting\u2026";
  if (dev.paired) return "Connect";
  return "Pair";
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
    const int r = bt_tab_rank(d);
    if (r == 0) has_connected = true;
    else if (r == 1) has_remembered = true;
    else has_nearby = true;
  }
  const bool show_headers =
      (has_connected ? 1 : 0) + (has_remembered ? 1 : 0) + (has_nearby ? 1 : 0) > 1;
  int last_rank = -1;
  for (int i = 0; i < static_cast<int>(ordered.size()); ++i) {
    const int rank = bt_tab_rank(ordered[static_cast<size_t>(i)]);
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

// Vertically centred action-button Y for a row (shared paint/click).
static double bt_tab_action_y(int rowY) {
  return static_cast<double>(rowY) + (static_cast<double>(kDevRowH) - 24.0) * 0.5;
}

// Raw BlueZ D-Bus errors mean nothing to users ("org.bluez.Error.X").
// Map the common ones to actionable one-liners; fall back to truncated raw.
static std::string bt_friendly_error(const std::string& raw) {
  if (raw.find("AuthenticationRejected") != std::string::npos)
    return "Headphones refused pairing (not in pairing mode) — hold them in pairing "
           "mode until the LED flashes, forget the PC on the buds if listed, Forget "
           "here, then Pair once.";
  if (raw.find("AuthenticationFailed") != std::string::npos)
    return "Authentication failed — retry pairing with the device in pairing mode.";
  if (raw.find("AuthenticationCanceled") != std::string::npos)
    return "Pairing canceled — retry when ready.";
  if (raw.find("AlreadyExists") != std::string::npos)
    return "Already paired — use Connect instead of Pair.";
  if (raw.find("NotAvailable") != std::string::npos)
    return "Device not available — make sure it is powered and in range.";
  if (raw.find("Failed") != std::string::npos && raw.find("Pair") != std::string::npos)
    return "Pairing failed — put the device in pairing mode and retry.";
  std::string s = raw;
  if (s.size() > 96) s.resize(96);
  return s;
}

// Page-coordinate bottom of the tab (mirrors the paint-pass card stack) so
// the scroll clamp knows the true extent once the list is uncapped.
int bluetooth_tab_content_bottom() {
  const auto ordered = bt_tab_ordered_devices(eh::bt::BluezService::instance().devices());
  const auto plan = bt_tab_plan(ordered);
  const int devCardTop = kScanCardTop + kScanCardH + kCardGap;
  return devCardTop + 52 + devices_content_height(plan);
}

void paint_bluetooth_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {

  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  bt.set_auto_reconnect(app.settings.btAutoReconnect);
  bt.refresh_if_needed();
  const auto state = bt.full_state();
  const auto& snap = state.snap;
  int total_devices = 0;
  const auto devices =
      bt_tab_visible_devices(bt_tab_ordered_devices(state.devices), total_devices);
  const std::string last_addr = bt.last_connected_address();
  const std::string last_err = bt.last_error();

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  const double pyC = app.pointerY + settings_scroll_px(app);
  int yPos = kBodyTop;

  // Adapter summary for the power-card description. An rfkill block is called
  // out explicitly: powering on and scanning both fail while blocked, and
  // without this the failure looks like a broken button (fix: rfkill unblock).
  char btDesc[160];
  const bool rfBlocked = snap.rfkillSoftBlocked || snap.rfkillHardBlocked;
  if (!snap.available)
    std::snprintf(btDesc, sizeof(btDesc), "No adapter found");
  else if (rfBlocked)
    std::snprintf(btDesc, sizeof(btDesc), "Blocked (%s) \u00b7 run: rfkill unblock bluetooth",
                  snap.rfkillHardBlocked ? "hard block" : "soft block");
  else if (snap.powered)
    std::snprintf(btDesc, sizeof(btDesc), "Powered on \u00b7 %d paired \u00b7 %d connected",
                  snap.paired_count, snap.connected_count);
  else
    std::snprintf(btDesc, sizeof(btDesc), "Powered off \u00b7 %d remembered", snap.paired_count);

  // Power card.
  settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(kPowerCardTop),
                static_cast<double>(cardW), static_cast<double>(kPowerCardH), glassOv);
  settings_cat_label(cr, static_cast<double>(cardX + kCardPad), kPowerCardTop + 22, "ADAPTER");
  settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(yPos),
                 "Bluetooth", btDesc);
  settings_toggle(app, cr, cardX, yPos, cardW,
                   static_cast<double>(yPos - 20), 26.0,
                   snap.available && snap.powered, 0.0);
  yPos += kSliderRowH;

  // Auto-reconnect description names the remembered device so the memory is
  // visible instead of a vague promise.
  char reconDesc[192] = "Reconnect devices you have used before when they are in range";
  if (!last_addr.empty()) {
    std::string last_name = last_addr;
    for (const auto& d : state.devices) {
      if (d.address == last_addr && !d.alias.empty()) {
        last_name = d.alias;
        break;
      }
    }
    std::snprintf(reconDesc, sizeof(reconDesc), "Reconnect %s and other used devices when in range",
                  last_name.c_str());
  }
  settings_label(cr, static_cast<double>(cardX + kCardPad), static_cast<double>(yPos),
                 "Auto-reconnect", reconDesc);
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

    // Left-side status: error (red) > powered-off hint > scanning > counts.
    {
      const double statusMaxW = std::max(24.0, btnX - 10.0 - (cardX + kCardPad));
      const double statusY = kScanBodyTop + 20.0;
      if (!last_err.empty()) {
        std::string short_err = bt_friendly_error(last_err);
        // BlueZ errors carry a "org.bluez.Error.X: " prefix + detail; keep it
        // to one helper line.
        settings_show_text_w(cr, cardX + kCardPad, statusY, short_err.c_str(), 12.0f, 400,
                             statusMaxW, 1.00f, 0.43f, 0.38f, 0.92f);
      } else if (!snap.powered) {
        settings_show_text_w(cr, cardX + kCardPad, statusY, "Turn Bluetooth on to scan", 12.0f,
                             400, statusMaxW, t_r, t_g, t_b, 0.55f);
      } else if (snap.scanning) {
        settings_show_text_w(cr, cardX + kCardPad, statusY, "Scanning for devices\u2026", 12.0f,
                             400, statusMaxW, t_r, t_g, t_b, 0.75f);
      } else if (devices.empty()) {
        settings_show_text_w(cr, cardX + kCardPad, statusY, "Scan to find nearby devices", 12.0f,
                             400, statusMaxW, t_r, t_g, t_b, 0.55f);
      } else {
        char found[64];
        std::snprintf(found, sizeof(found), "%d device%s found \u00b7 scan for more",
                      static_cast<int>(devices.size()),
                      devices.size() == 1 ? "" : "s");
        settings_show_text_w(cr, cardX + kCardPad, statusY, found, 12.0f, 400, statusMaxW,
                             t_r, t_g, t_b, 0.55f);
      }
    }

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
      btn.setEnabled(snap.powered);
      btn.paint(cr);
    }

    // Devices card.
    const int devCardTop = kScanCardTop + kScanCardH + kCardGap;
    const auto plan = bt_tab_plan(devices);
    const int devContentH = snap.powered ? devices_content_height(plan) : 60;
    const int devCardH = 52 + devContentH;

    settings_card(app, cr, static_cast<double>(cardX), static_cast<double>(devCardTop),
                  static_cast<double>(cardW), static_cast<double>(devCardH), glassOv);
    {
      char devCat[64];
      if (total_devices > static_cast<int>(devices.size()))
        std::snprintf(devCat, sizeof(devCat), "DEVICES \u00b7 %d OF %d SHOWN",
                      static_cast<int>(devices.size()), total_devices);
      else
        std::snprintf(devCat, sizeof(devCat), "DEVICES");
      settings_cat_label(cr, static_cast<double>(cardX + kCardPad), devCardTop + 22, devCat);
    }

    const int listTop = devCardTop + 52;

    if (!snap.powered) {
      settings_show_text(cr, cardX + kCardPad, listTop + 24,
                         "Bluetooth is off \u2014 turn it on to manage devices", 13, 400, t_r,
                         t_g, t_b, 0.42);
    } else if (plan.empty()) {
      settings_show_text(cr, cardX + kCardPad, listTop + 24, snap.scanning ? "Scanning for devices..." : "No devices found \u2014 scan to discover nearby devices", 13, 400, t_r, t_g, t_b, 0.42);
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

        // Device type glyph (amber while an operation is in flight).
        const char* glyph = eh::bt::bluetooth_device_kind_glyph(dev.kind);
        float g_r = t_r, g_g = t_g, g_b = t_b, g_a = 0.80f;
        if (dev.connecting) {
          g_r = 0.96f;
          g_g = 0.72f;
          g_b = 0.30f;
          g_a = 0.95f;
        }
        eh::shell::draw_material_glyph(cr,
                                       cardX + kCardPad + 14.0,
                                       rowY + kDevRowH * 0.5,
                                       18.0, glyph, g_r, g_g, g_b, g_a);

        // Action buttons, right-aligned and vertically centred. Sized from the
        // measured label so the m3 auto-expand in setGeometry (label +
        // 2*kAutoPad, re-centred) never fires — it would push the pills into
        // each other.
        const double actBtnH = 24.0;
        const double actBtnY = bt_tab_action_y(rowY);
        double actBtnX = cardX + cardW - kCardPad - kSpacingS;
        double btnAreaLeft = actBtnX;
        const bool rowBusy = dev.connecting;
        // Forget clears the BlueZ object even when unpaired: a stale cached
        // device (bond lost, headphones still holding the old key) can only be
        // re-paired after RemoveDevice. Hidden only while an op is in flight.
        if (!dev.connecting) {
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
          const char* lbl = bt_tab_action_label(dev);
          const double connBtnW = settings_btn_w(lbl);
          actBtnX -= connBtnW;
          btnAreaLeft = std::min(btnAreaLeft, actBtnX);
          const bool cov = !rowBusy && point_in_rect(app.pointerX, pyC,
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
          btn.setEnabled(!rowBusy);
          btn.paint(cr);
        }

        // Device alias + battery — the level sits inline, right after the name.
        const double aliasX = cardX + kCardPad + 40.0;
        const double aliasMaxW = std::max(24.0, btnAreaLeft - 10.0 - aliasX);
        const double nameBaseline = static_cast<double>(rowY) + 26.0;
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

        // Kind + state + signal/last-used + address.
        char statusBuf[192];
        bt_tab_status_text(dev, last_addr, statusBuf, sizeof(statusBuf));
        if (dev.connecting)
          settings_show_text_w(cr, cardX + kCardPad + 40.0, rowY + 48, statusBuf, 11, 400,
                               aliasMaxW, 0.96f, 0.72f, 0.30f, 0.85f);
        else
          settings_show_text_w(cr, cardX + kCardPad + 40.0, rowY + 48, statusBuf, 11, 400,
                               aliasMaxW, t_r, t_g, t_b, 0.46f);
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
  int total_devices = 0;
  const auto devices =
      bt_tab_visible_devices(bt_tab_ordered_devices(state.devices), total_devices);
  (void)total_devices;

  const int cardX = contentX + 8;
  const int cardW = contentW - 16;
  const double ly = app.pointerY + settings_scroll_px(app);

  // Power toggle
  {
    constexpr int swW = 52, swH = 26;
    const int swX = cardX + cardW - swW - kSpacingXL;
    const int swY = kBodyTop - 20;
    if (point_in_rect(app.pointerX, ly, swX, swY, swW, swH)) {
      debug_log("bluetooth", "ui power -> %s", !snap.powered ? "on" : "off");
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
      debug_log("bluetooth", "ui auto-reconnect -> %s",
                app.settings.btAutoReconnect ? "on" : "off");
      bt.set_auto_reconnect(app.settings.btAutoReconnect);
      save_settings(app.settings);
      draw(app);
      return true;
    }
  }

  if (!snap.available) return false;

  // Scan button (stays clickable while scanning so the scan can be stopped).
  {
    const double btnW = 120.0;
    const double btnH = kBtnH;
    const double btnX = cardX + cardW - btnW - kCardPad;
    const double btnY = kScanBodyTop;
    if (snap.powered && point_in_rect(app.pointerX, ly,
                       static_cast<int>(btnX), static_cast<int>(btnY),
                       static_cast<int>(btnW), static_cast<int>(btnH))) {
      debug_log("bluetooth", "ui %s discovery (powered=%d)",
                  snap.scanning ? "stop" : "start", snap.powered ? 1 : 0);
      if (snap.scanning)
        bt.stop_discovery();
      else
        bt.start_discovery();
      draw(app);
      return true;
    }
  }

  if (!snap.powered) return false;

  // Device list buttons (geometry mirrors the paint pass exactly).
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
    // In-flight rows are not clickable until the operation settles.
    if (dev.connecting) continue;
    const double actBtnH = 24.0;
    const double actBtnY = bt_tab_action_y(rowY);
    double actBtnX = cardX + cardW - kCardPad - kSpacingS;

    // Forget button (paired or not — clears stale BlueZ objects).
    {
      const double forgetBtnW = settings_btn_w("Forget");
      actBtnX -= forgetBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(forgetBtnW), static_cast<int>(actBtnH))) {
        debug_log("bluetooth", "ui forget '%s' addr=%s", dev.alias.c_str(),
                  dev.address.c_str());
        bt.forget_device(dev.path);
        draw(app);
        return true;
      }
      actBtnX -= 10.0;
    }

    // Primary action: Disconnect / Connect / Pair.
    {
      const char* lbl = bt_tab_action_label(dev);
      const double connBtnW = settings_btn_w(lbl);
      actBtnX -= connBtnW;
      if (point_in_rect(app.pointerX, ly,
                         static_cast<int>(actBtnX), static_cast<int>(actBtnY),
                         static_cast<int>(connBtnW), static_cast<int>(actBtnH))) {
        const char* action = dev.connected ? "disconnect" : (dev.paired ? "connect" : "pair");
        debug_log("bluetooth", "ui %s '%s' addr=%s", action, dev.alias.c_str(),
                  dev.address.c_str());
        if (dev.connected)
          bt.disconnect_device(dev.path);
        else if (dev.paired)
          bt.connect_device(dev.path);
        else
          bt.pair_device(dev.path);
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
  int total_devices = 0;
  const auto ordered = bt_tab_visible_devices(
      bt_tab_ordered_devices(bt.full_state().devices), total_devices);
  (void)total_devices;
  const auto plan = bt_tab_plan(ordered);
  const int new_hover = bt_tab_hover_row(app, contentX, contentW, plan);
  if (new_hover != app.settingsBluetoothHoverRow) {
    app.settingsBluetoothHoverRow = new_hover;
    draw(app);
  }
}
