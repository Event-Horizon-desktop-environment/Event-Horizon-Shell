#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include "configuration/shell_config.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

// BlueZ reports battery as a percentage via org.bluez.Battery1; the widget
// layer exposes it per device as `battery_pct` (-1 when unknown).

const char* bt_battery_glyph(int pct) {
  if (pct >= 95) return "battery_full";
  if (pct >= 80) return "battery_6_bar";
  if (pct >= 60) return "battery_5_bar";
  if (pct >= 40) return "battery_4_bar";
  if (pct >= 20) return "battery_3_bar";
  if (pct >= 10) return "battery_2_bar";
  if (pct >= 5)  return "battery_1_bar";
  return "battery_0_bar";
}

void bt_battery_color(int pct, double& r, double& g, double& b) {
  if (pct <= 20)      { r = 1.0;  g = 0.43; b = 0.38; }
  else if (pct <= 50) { r = 0.96; g = 0.72; b = 0.30; }
  else                { r = 0.45; g = 0.85; b = 0.50; }
}

// EH_BLUETOOTH_DEBUG lets the dock slot and popup be exercised without
// hardware, mirroring the battery widget's EH_BATTERY_DEBUG. Values:
//   connected  – mixed fleet, several devices reporting battery
//   off        – adapter present but powered off
//   noadapter  – no adapter found
//   scanning   – powered and scanning
//   empty      – powered, no devices known
// Any other non-empty value behaves like "connected".

std::optional<eh::bt::FullState> debug_full_state() {
  const char* env = std::getenv("EH_BLUETOOTH_DEBUG");
  if (!env || env[0] == '\0') return std::nullopt;
  const std::string_view sv(env);

  using K = eh::bt::BluetoothDeviceKind;
  auto dev = [](const char* path, const char* addr, const char* alias, K kind, bool paired, bool connected,
                bool has_battery, uint8_t pct) {
    eh::bt::DeviceInfo d;
    d.path = path;
    d.address = addr;
    d.alias = alias;
    d.kind = kind;
    d.paired = paired;
    d.connected = connected;
    d.has_battery = has_battery;
    d.battery_percent = pct;
    return d;
  };

  eh::bt::FullState st;
  st.snap.available = true;
  st.snap.powered = true;

  if (sv == "off") {
    st.snap.powered = false;
    st.snap.paired_count = 3;
    st.devices = {
        dev("/org/bluez/hci0/dev_50_50_50_00_00_01", "50:50:50:00:00:01", "WH-1000XM5", K::Headphones, true, false,
            true, 78),
        dev("/org/bluez/hci0/dev_50_50_50_00_00_02", "50:50:50:00:00:02", "MX Master 3S", K::Mouse, true, false, true,
            34),
        dev("/org/bluez/hci0/dev_50_50_50_00_00_03", "50:50:50:00:00:03", "HHKB Professional", K::Keyboard, true,
            false, false, 0),
    };
    return st;
  }
  if (sv == "noadapter") {
    st.snap.available = false;
    st.snap.powered = false;
    return st;
  }
  if (sv == "scanning") {
    st.snap.scanning = true;
    st.devices = {
        dev("/org/bluez/hci0/dev_50_50_50_00_00_04", "50:50:50:00:00:04", "Studio Speaker", K::Speaker, false, false,
            false, 0),
        dev("/org/bluez/hci0/dev_50_50_50_00_00_05", "50:50:50:00:00:05", "USB Dongle", K::Unknown, false, false,
            false, 0),
    };
    return st;
  }
  if (sv == "empty") {
    return st;  // Powered, nothing known yet.
  }

  // Default: connected fleet.
  st.snap.connected = true;
  st.snap.paired_count = 5;
  st.snap.connected_count = 4;
  st.devices = {
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_01", "AA:BB:CC:DD:EE:01", "WH-1000XM5", K::Headphones, true, true, true,
          78),
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_02", "AA:BB:CC:DD:EE:02", "MX Master 3S", K::Mouse, true, true, true, 34),
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_03", "AA:BB:CC:DD:EE:03", "AirPods Pro", K::Earbuds, true, true, true,
          100),
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_04", "AA:BB:CC:DD:EE:04", "HHKB Professional", K::Keyboard, true, false,
          false, 0),
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_05", "AA:BB:CC:DD:EE:05", "DualSense", K::Gamepad, true, true, false, 0),
      dev("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_06", "AA:BB:CC:DD:EE:06", "Studio Speaker", K::Speaker, false, false,
          false, 0),
  };
  return st;
}


struct BluetoothTextCache {
  std::string text;
  int textW = 0;
  int textH = 0;
};
static std::unordered_map<std::string, BluetoothTextCache> g_bluetoothTextCache;
static std::atomic<bool> g_btStateChanged{false};
static std::atomic<bool> g_btImmediateChanged{false};

int measure_text_px(cairo_t* cr, const char* text, double font_px) {
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc =
      pango_font_description_from_string(("Inter " + std::to_string(static_cast<int>(font_px))).c_str());
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(layout, text, -1);
  int w = 0, h = 0;
  pango_layout_get_pixel_size(layout, &w, &h);
  g_object_unref(layout);
  return w;
}


struct SlotLabel {
  enum class Kind { None, Count, Battery };
  Kind kind = Kind::None;
  char text[32]{};
  int pct = 0;
};

SlotLabel make_slot_label(const eh::bt::Snapshot& snap, const std::vector<eh::bt::DeviceInfo>& devs) {
  SlotLabel out;
  if (!snap.connected || snap.connected_count <= 0) return out;
  int min_pct = 101;
  int with_battery = 0;
  for (const auto& d : devs) {
    if (!d.connected || !d.has_battery) continue;
    ++with_battery;
    if (static_cast<int>(d.battery_percent) < min_pct) min_pct = static_cast<int>(d.battery_percent);
  }
  if (with_battery > 0) {
    out.kind = SlotLabel::Kind::Battery;
    out.pct = std::clamp(min_pct, 0, 100);
    std::snprintf(out.text, sizeof(out.text), "%d%%", out.pct);
  } else {
    out.kind = SlotLabel::Kind::Count;
    std::snprintf(out.text, sizeof(out.text), snap.connected_count == 1 ? "%d device" : "%d devices",
                  snap.connected_count);
  }
  return out;
}

// One shared layout used by both the paint and click handlers, so the

struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  [[nodiscard]] bool contains(double px, double py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

constexpr int kMaxRows = 7;
constexpr double kHeaderH = 56.0;
constexpr double kStatusH = 34.0;
constexpr double kRowH = 48.0;
constexpr double kSectionH = 26.0;
constexpr double kFooterGap = 10.0;
constexpr double kScanH = 40.0;
constexpr double kBottomPad = 14.0;

// row. Sections give connected devices the top slot and keep remembered
// (paired, not connected) devices in their own space below.
struct PopupItem {
  bool header = false;
  const char* label = nullptr;               // header text
  const eh::bt::DeviceInfo* dev = nullptr;   // device row
  Rect body;      // header label area, or whole row (for hover highlight)
  Rect tile;      // device-kind glyph tile
  Rect battery;   // right-aligned battery slot (w == 0 when unknown)
  Rect action;    // Connect / Disconnect / Pair pill
  Rect forget;    // Forget ghost button (w == 0 when hidden)
};

struct PopupLayout {
  double W = 0;
  double H = 0;
  Rect header;
  Rect iconTile;
  Rect toggle;
  Rect closeBtn;
  Rect status;
  std::vector<PopupItem> items;
  Rect scan;
  int shown = 0;   // device rows only (excludes section headers)
};

std::vector<PopupItem> plan_popup_items(const eh::bt::Snapshot& bt,
                                        const std::vector<eh::bt::DeviceInfo>& devs) {
  std::vector<PopupItem> items;
  if (!bt.available || !bt.powered) return items;

  std::vector<const eh::bt::DeviceInfo*> connected, remembered, nearby;
  for (const auto& d : devs) {
    if (d.connected) connected.push_back(&d);
    else if (d.paired) remembered.push_back(&d);
    else nearby.push_back(&d);
  }

  const int groups = (connected.empty() ? 0 : 1) + (remembered.empty() ? 0 : 1) +
                     (nearby.empty() ? 0 : 1);
  const bool show_headers = groups > 1;

  int budget = kMaxRows;
  auto add_group = [&](const std::vector<const eh::bt::DeviceInfo*>& g, const char* header) {
    if (g.empty() || budget <= 0) return;
    if (show_headers) {
      PopupItem h;
      h.header = true;
      h.label = header;
      items.push_back(h);
    }
    for (const auto* d : g) {
      if (budget <= 0) break;
      PopupItem it;
      it.dev = d;
      items.push_back(it);
      --budget;
    }
  };
  add_group(connected, "CONNECTED");
  add_group(remembered, "REMEMBERED DEVICES");
  add_group(nearby, "NEARBY DEVICES");
  return items;
}

double popup_height_for_state(const eh::bt::Snapshot& bt, const std::vector<eh::bt::DeviceInfo>& devs) {
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  double h = 16.0 * us + kHeaderH * us;
  if (!bt.available) {
    return std::ceil(h + kStatusH * us + kBottomPad * us);
  }
  h += kStatusH * us;
  if (bt.powered) {
    for (const auto& it : plan_popup_items(bt, devs)) {
      h += (it.header ? kSectionH : kRowH) * us;
    }
    h += kFooterGap * us + kScanH * us + kBottomPad * us;
  } else {
    h += kBottomPad * us;
  }
  return std::ceil(h);
}

PopupLayout build_popup_layout(const eh::bt::Snapshot& bt, const std::vector<eh::bt::DeviceInfo>& devs,
                               double us, double W, double H) {
  PopupLayout L;
  L.W = W;
  L.H = H;
  const double pad = 16.0 * us;
  const double headerH = kHeaderH * us;

  const double closeD = 28.0 * us;
  const double togW = 46.0 * us;
  const double togH = 26.0 * us;
  const double tileD = 34.0 * us;

  L.header = {pad, pad, W - 2.0 * pad, headerH};
  L.iconTile = {pad, pad + (headerH - tileD) * 0.5, tileD, tileD};
  L.closeBtn = {W - pad - closeD, pad + (headerH - closeD) * 0.5, closeD, closeD};
  L.toggle = {L.closeBtn.x - 8.0 * us - togW, pad + (headerH - togH) * 0.5, togW, togH};
  L.status = {pad, pad + headerH, W - 2.0 * pad, kStatusH * us};

  L.items = plan_popup_items(bt, devs);
  L.shown = 0;
  double listY = L.status.y + L.status.h;
  const double rowH = kRowH * us;
  const double sectionH = kSectionH * us;
  for (auto& it : L.items) {
    if (it.header) {
      it.body = {pad + 2.0 * us, listY, W - 2.0 * pad - 4.0 * us, sectionH};
      listY += sectionH;
      continue;
    }
    const double ry = listY;
    const auto& d = *it.dev;
    it.body = {pad, ry, W - 2.0 * pad, rowH};
    const double tileS = 32.0 * us;
    it.tile = {pad + 1.0 * us, ry + (rowH - tileS) * 0.5, tileS, tileS};

    const double actH = 30.0 * us;
    const double actW = (d.connected ? 84.0 : (d.paired ? 62.0 : 46.0)) * us;
    it.action = {W - pad - actW, ry + (rowH - actH) * 0.5, actW, actH};

    if (d.paired) {
      const double fogW = 26.0 * us;
      it.forget = {it.action.x - 6.0 * us - fogW, ry + (rowH - fogW) * 0.5, fogW, fogW};
    }

    if (d.has_battery) {
      const double batW = 44.0 * us;
      const double batH = rowH;
      const double refX = it.forget.w > 0 ? it.forget.x : it.action.x;
      it.battery = {refX - 6.0 * us - batW, ry, batW, batH};
    }
    ++L.shown;
    listY += rowH;
  }

  if (bt.available && bt.powered) {
    const double sW = W - 2.0 * pad;
    const double sH = kScanH * us;
    L.scan = {pad, H - kBottomPad * us - sH, sW, sH};
  }
  return L;
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────────

void bluetooth_widget_init() {
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  bt.set_change_callback([]() {
    g_btStateChanged.store(true, std::memory_order_relaxed);
    g_btImmediateChanged.store(true, std::memory_order_relaxed);
  });
}

bool bluetooth_widget_poll() {
  // Periodically trim the text cache to prevent unbounded growth.
  if (g_bluetoothTextCache.size() > 64) {
    g_bluetoothTextCache.clear();
  }

  // Some devices expose org.bluez.Battery1 late (after services resolve) or
  // update it without emitting PropertiesChanged. A light, debounced refresh
  // keeps the connectivity/battery view honest while connected.
  static std::chrono::steady_clock::time_point lastRefresh{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastRefresh >= std::chrono::seconds(4)) {
    lastRefresh = now;
    eh::bt::BluezService& bt = eh::bt::BluezService::instance();
    if (bt.snapshot().connected) bt.refresh();
  }

  return g_btStateChanged.exchange(false, std::memory_order_relaxed);
}

bool bluetooth_widget_needs_immediate_draw() {
  // Debounce: at most ~6 FPS for immediate BT-triggered redraws.
  static std::chrono::steady_clock::time_point lastDraw{};
  auto now = std::chrono::steady_clock::now();
  if (now - lastDraw < std::chrono::milliseconds(150)) {
    return false;
  }
  if (g_btImmediateChanged.exchange(false, std::memory_order_relaxed)) {
    lastDraw = now;
    return true;
  }
  return false;
}

void bluetooth_widget_shutdown() {
}

// ── Mediated Bluetooth operations ────────────────────────────────────────

void bluetooth_ensure_service() {
  eh::bt::BluezService::instance().start();
}

void bluetooth_start_discovery() {
  eh::bt::BluezService::instance().start_discovery();
}

void bluetooth_stop_discovery() {
  eh::bt::BluezService::instance().stop_discovery();
}

std::vector<BluetoothDevice> bluetooth_devices() {
  const auto raw = eh::bt::BluezService::instance().devices();
  std::vector<BluetoothDevice> out;
  out.reserve(raw.size());
  for (const auto& d : raw) {
    out.push_back({
      .path = d.path,
      .address = d.address,
      .alias = d.alias,
      .kind = static_cast<BluetoothDeviceKind>(d.kind),
      .paired = d.paired,
      .connected = d.connected,
      .connecting = d.connecting,
      .battery_pct = d.has_battery ? static_cast<int>(d.battery_percent) : -1,
    });
  }
  return out;
}

const char* bluetooth_device_kind_glyph(BluetoothDeviceKind kind) {
  return eh::bt::bluetooth_device_kind_glyph(static_cast<::eh::bt::BluetoothDeviceKind>(kind));
}

BluetoothSnapshot bluetooth_snapshot() {
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  const eh::bt::Snapshot s = bt.snapshot();
  // Lowest battery among connected devices so the control center can show the
  // most actionable number, matching the dock slot label.
  int battery = -1;
  for (const auto& d : bt.devices()) {
    if (!d.connected || !d.has_battery) continue;
    const int pct = static_cast<int>(d.battery_percent);
    if (battery < 0 || pct < battery) battery = pct;
  }
  return {
    .available = s.available,
    .powered = s.powered,
    .connected = s.connected,
    .paired_count = s.paired_count,
    .connected_count = s.connected_count,
    .battery_pct = battery,
  };
}

void bluetooth_set_powered(bool on) {
  eh::bt::BluezService::instance().set_powered(on);
}

void bluetooth_connect_device(const std::string& path) {
  eh::bt::BluezService::instance().connect_device(path);
}

void bluetooth_disconnect_device(const std::string& path) {
  eh::bt::BluezService::instance().disconnect_device(path);
}

void bluetooth_pair_device(const std::string& path) {
  eh::bt::BluezService::instance().pair_device(path);
}

void bluetooth_forget_device(const std::string& path) {
  eh::bt::BluezService::instance().forget_device(path);
}

bool widget_list_contains_bluetooth(const eh::config::ShellConfig& sc, const std::vector<std::string>& widgets) {
  (void)sc;
  for (const auto& id : widgets) {
    if (eh::config::widget_implementation_type(id) == "bluetooth") return true;
  }
  return false;
}


double dock_bluetooth_slot_width(cairo_t* measure_cr, const eh::config::ShellConfig& sc,
                                 std::string_view instance_id, double icon_ref_px, double /*bar_height*/) {
  const double us = dock_ui_scale(sc.dock);
  const bool show_label = parse_bool_setting(widget_setting(sc, instance_id, "show_label"), false);
  if (!show_label) return icon_ref_px + 4.0 * us;

  eh::bt::FullState st;
  if (auto dbg = debug_full_state()) {
    st = *dbg;
  } else {
    eh::bt::BluezService::instance().start();
    st = eh::bt::BluezService::instance().full_state();
  }
  const SlotLabel lbl = make_slot_label(st.snap, st.devices);
  if (lbl.kind == SlotLabel::Kind::None) return icon_ref_px + 4.0 * us;

  if (!measure_cr) measure_cr = get_measure_cr();

  int tw = 0;
  const std::string btkid(instance_id);
  auto btit = g_bluetoothTextCache.find(btkid);
  if (btit != g_bluetoothTextCache.end() && btit->second.text == lbl.text) {
    tw = btit->second.textW;
  } else {
    const double font_px = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
    tw = measure_text_px(measure_cr, lbl.text, font_px);
    BluetoothTextCache& btc = g_bluetoothTextCache[btkid];
    btc.text = lbl.text;
    btc.textW = tw;
    btc.textH = static_cast<int>(font_px);
  }

  const double pad_x = 6.0 * us;
  const double gap = 6.0 * us;
  const double min_w = std::max(icon_ref_px * 1.25, 40.0 * us);
  const double total = icon_ref_px + gap + static_cast<double>(tw) + pad_x * 2.0;
  return std::max(min_w, total);
}

void paint_bluetooth_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                          double x, double y, double slot_w, double slot_h, double icon_ref_px,
                          bool hovered, bool pressed) {
  const double us = dock_ui_scale(sc.dock);
  const bool show_label = parse_bool_setting(widget_setting(sc, instance_id, "show_label"), false);

  eh::bt::FullState st;
  if (auto dbg = debug_full_state()) {
    st = *dbg;
  } else {
    eh::bt::BluezService::instance().start();
    st = eh::bt::BluezService::instance().full_state();
  }
  const auto& bt = st.snap;

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const bool active = bt.available && bt.powered;
  const double fg_opacity = active ? 1.0 : 0.55;
  double fgR = mc.textR, fgG = mc.textG, fgB = mc.textB;
  if (active && bt.connected) {
    fgR = mc.accentR; fgG = mc.accentG; fgB = mc.accentB;
  }
  const char* glyph = active ? "bluetooth" : "bluetooth_disabled";
  const double glyph_px = icon_ref_px * 0.52;

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);
  if (hovered || pressed) {
    eh::shell::shared::rounded_rect(cr, x, y, slot_w, slot_h,
                                     slot_pill_style::corner_radius(slot_h, slot_w));
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, pressed ? 0.14 : 0.08);
    cairo_fill(cr);
  }

  const SlotLabel lbl = make_slot_label(bt, st.devices);
  if (lbl.kind == SlotLabel::Kind::None || !show_label) {
    eh::shell::draw_material_glyph(cr, x + slot_w * 0.5, y + slot_h * 0.5, glyph_px, glyph, fgR, fgG, fgB,
                                   fg_opacity);
    cairo_restore(cr);
    return;
  }

  const double font_px = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
  int tw = 0, th = 0;
  const std::string pbtkid(instance_id);
  auto pbtit = g_bluetoothTextCache.find(pbtkid);
  if (pbtit != g_bluetoothTextCache.end() && pbtit->second.text == lbl.text) {
    tw = pbtit->second.textW;
    th = pbtit->second.textH;
  } else {
    tw = measure_text_px(cr, lbl.text, font_px);
    th = static_cast<int>(font_px);
    BluetoothTextCache& btc2 = g_bluetoothTextCache[pbtkid];
    btc2.text = lbl.text;
    btc2.textW = tw;
    btc2.textH = th;
  }

  double labR = fgR, labG = fgG, labB = fgB;
  if (lbl.kind == SlotLabel::Kind::Battery) bt_battery_color(lbl.pct, labR, labG, labB);

  const double gap = 6.0 * us;
  const double total_inner = icon_ref_px + gap + static_cast<double>(tw);
  const double inner_x = x + (slot_w - total_inner) * 0.5;

  eh::shell::draw_material_glyph(cr, inner_x + icon_ref_px * 0.5, y + slot_h * 0.5, glyph_px, glyph, fgR, fgG, fgB,
                                 fg_opacity);

  cairo_set_source_rgba(cr, labR, labG, labB, fg_opacity);
  PangoLayout* layout = pango_cairo_create_layout(cr);
  PangoFontDescription* desc = pango_font_description_from_string(
      ("Inter " + std::to_string(static_cast<int>(font_px))).c_str());
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(layout, lbl.text, -1);
  cairo_move_to(cr, inner_x + icon_ref_px + gap, y + (slot_h - static_cast<double>(th)) * 0.5);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);

  cairo_restore(cr);
}

// ── Popup ────────────────────────────────────────────────────────────────

int bluetooth_popup_height() {
  if (auto dbg = debug_full_state()) {
    return static_cast<int>(popup_height_for_state(dbg->snap, dbg->devices));
  }
  eh::bt::BluezService::instance().start();
  const auto st = eh::bt::BluezService::instance().full_state();
  return static_cast<int>(popup_height_for_state(st.snap, st.devices));
}

void dock_bluetooth_popup_paint(double pointerX, double pointerY, cairo_t* cr, const eh::config::ShellConfig& sc) {
  eh::bt::FullState state;
  if (auto dbg = debug_full_state()) {
    state = *dbg;
  } else {
    eh::bt::BluezService::instance().start();
    state = eh::bt::BluezService::instance().full_state();
  }
  const auto& bt = state.snap;
  const auto& devs = state.devices;

  const double W = static_cast<double>(kBluetoothPopupW);
  const double H = popup_height_for_state(bt, devs);
  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const double us = dock_ui_scale(sc.dock);
  const double pad = 16.0 * us;
  const double kCornerRadius = 20.0 * us;
  const double kShadOffX = 2.0 * us, kShadOffY = 4.0 * us;
  constexpr double kShadAlpha = 0.28;
  const double px = pointerX, py = pointerY;

  const PopupLayout L = build_popup_layout(bt, devs, us, W, H);


  cairo_save(cr);
  eh::shell::shared::rounded_rect(cr, kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  eh::shell::shared::rounded_rect(cr, 0, 0, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, mc.dockFillR * 0.30, mc.dockFillG * 0.30, mc.dockFillB * 0.30,
                        0.92 * sc.appearance.overlayOpacityWidgetCard);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.14);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const bool powered = bt.available && bt.powered;
  eh::shell::shared::rounded_rect(cr, L.iconTile.x, L.iconTile.y, L.iconTile.w, L.iconTile.h, 10.0 * us);
  if (powered) {
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.18);
  } else {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
  }
  cairo_fill(cr);
  {
    const double gr = powered ? mc.accentR : 0.65;
    const double gg = powered ? mc.accentG : 0.68;
    const double gb = powered ? mc.accentB : 0.72;
    if (bt.scanning) {
      eh::shell::draw_material_glyph(cr, L.iconTile.x + L.iconTile.w * 0.5, L.iconTile.y + L.iconTile.h * 0.5,
                                     18.0 * us, "bluetooth_searching", gr, gg, gb, 0.95);
    } else {
      eh::shell::draw_material_glyph(cr, L.iconTile.x + L.iconTile.w * 0.5, L.iconTile.y + L.iconTile.h * 0.5,
                                     18.0 * us, powered ? "bluetooth" : "bluetooth_disabled", gr, gg, gb, 0.95);
    }
  }

  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.95);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 15.0 * us);
  cairo_move_to(cr, L.iconTile.x + L.iconTile.w + 10.0 * us,
                L.header.y + L.header.h * 0.5 + 5.5 * us);
  cairo_show_text(cr, "Bluetooth");

  const bool close_hov = L.closeBtn.contains(px, py);
  eh::shell::shared::rounded_rect(cr, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, 7.0 * us);
  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, close_hov ? 0.70 : 0.35);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, L.closeBtn.x + L.closeBtn.w * 0.5, L.closeBtn.y + L.closeBtn.h * 0.5,
                                 14.0 * us, "close", mc.textR, mc.textG, mc.textB, close_hov ? 1.0 : 0.85);

  if (bt.available) {
    const bool tog_hov = L.toggle.contains(px, py);
    eh::shell::shared::rounded_rect(cr, L.toggle.x, L.toggle.y, L.toggle.w, L.toggle.h, L.toggle.h * 0.5);
    if (bt.powered) {
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, tog_hov ? 1.0 : 0.85);
    } else {
      cairo_set_source_rgba(cr, 0.15, 0.15, 0.18, tog_hov ? 0.55 : 0.40);
    }
    cairo_fill(cr);
    if (!bt.powered) {
      eh::shell::shared::rounded_rect(cr, L.toggle.x + 0.5, L.toggle.y + 0.5, L.toggle.w - 1.0, L.toggle.h - 1.0,
                 L.toggle.h * 0.5 - 0.5);
      cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.6);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }
    const double th = L.toggle.h - 6.0 * us;
    const double tx = bt.powered ? (L.toggle.x + L.toggle.w - 3.0 * us - th) : (L.toggle.x + 3.0 * us);
    cairo_set_source_rgba(cr, 1, 1, 1, 1.0);
    cairo_arc(cr, tx + th * 0.5, L.toggle.y + L.toggle.h * 0.5, th * 0.5, 0, 2.0 * M_PI);
    cairo_fill(cr);
  }

  {
    char status_buf[64];
    const char* status = nullptr;
    if (!bt.available) {
      status = "No Bluetooth adapter found";
    } else if (!bt.powered) {
      status = "Bluetooth is off";
    } else if (bt.scanning) {
      status = "Scanning for devices…";
    } else if (bt.connected_count > 0) {
      std::snprintf(status_buf, sizeof(status_buf), "On · %d device%s connected",
                    bt.connected_count, bt.connected_count == 1 ? "" : "s");
      status = status_buf;
    } else {
      status = "On · no devices connected";
    }
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.6);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_move_to(cr, L.status.x, L.status.y + L.status.h * 0.5 + 4.5 * us);
    cairo_show_text(cr, status);
  }

  if (L.shown > 0) {
    const double dy = L.status.y + L.status.h;
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.16);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pad + 2.0 * us, dy);
    cairo_line_to(cr, W - pad - 2.0 * us, dy);
    cairo_stroke(cr);
  }

  if (L.shown > 0) {
    cairo_save(cr);
    cairo_rectangle(cr, L.items.front().body.x, L.items.front().body.y, L.items.front().body.w,
                    L.items.back().body.y + L.items.back().body.h - L.items.front().body.y);
    cairo_clip(cr);

    for (const auto& item : L.items) {
      if (item.header) {
        cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.42);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 10.0 * us);
        cairo_move_to(cr, item.body.x, item.body.y + item.body.h * 0.5 + 3.5 * us);
        cairo_show_text(cr, item.label);
        continue;
      }
      const auto& d = *item.dev;
      const PopupItem& r = item;
      const bool row_hov = r.body.contains(px, py);

      if (d.connected || row_hov) {
        eh::shell::shared::rounded_rect(cr, r.body.x, r.body.y, r.body.w, r.body.h, 10.0 * us);
        if (d.connected) {
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.14);
        } else {
          cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        }
        cairo_fill(cr);
      }

      eh::shell::shared::rounded_rect(cr, r.tile.x, r.tile.y, r.tile.w, r.tile.h, 9.0 * us);
      if (d.connected) {
        cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, 0.20);
      } else {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
      }
      cairo_fill(cr);

      double gr = 0.75, gg = 0.78, gb = 0.82;
      if (d.connecting) {
        gr = 0.96; gg = 0.72; gb = 0.30;             // amber while connecting
      } else if (d.connected) {
        gr = mc.accentR; gg = mc.accentG; gb = mc.accentB;
      }
      eh::shell::draw_material_glyph(cr, r.tile.x + r.tile.w * 0.5, r.tile.y + r.tile.h * 0.5, 16.0 * us,
                                     eh::bt::bluetooth_device_kind_glyph(d.kind), gr, gg, gb, 0.95);

      const std::string& label = d.alias.empty() ? d.address : d.alias;
      double textRight = W - pad;
      if (r.battery.w > 0) { textRight = r.battery.x; }
      if (r.forget.w > 0) { textRight = std::min(textRight, r.forget.x); }
      textRight = std::min(textRight, r.action.x);
      const double nameX = r.tile.x + r.tile.w + 10.0 * us;
      const double nameMaxW = std::max(24.0, textRight - 8.0 * us - nameX);
      constexpr double kNameFontPx = 13.0;
      constexpr double kSubFontPx = 11.0;
      const double lineGap = 4.0 * us;

      PangoLayout* pl = pango_cairo_create_layout(cr);
      PangoFontDescription* pdesc = pango_font_description_from_string(
          ("Inter Medium " + std::to_string(static_cast<int>(kNameFontPx * us))).c_str());
      pango_layout_set_font_description(pl, pdesc);
      pango_font_description_free(pdesc);
      pango_layout_set_text(pl, label.c_str(), -1);
      pango_layout_set_width(pl, static_cast<int>(nameMaxW * PANGO_SCALE));
      pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_END);
      PangoRectangle nameInk{};
      pango_layout_get_pixel_extents(pl, &nameInk, nullptr);

      const char* sub = d.connected ? "Connected" : (d.connecting ? "Connecting…"
                                  : (d.paired ? "Paired" : "Not paired"));
      PangoLayout* spl = pango_cairo_create_layout(cr);
      PangoFontDescription* sdesc = pango_font_description_from_string(
          ("Inter " + std::to_string(static_cast<int>(kSubFontPx * us))).c_str());
      pango_layout_set_font_description(spl, sdesc);
      pango_font_description_free(sdesc);
      pango_layout_set_text(spl, sub, -1);
      pango_layout_set_width(spl, static_cast<int>(nameMaxW * PANGO_SCALE));
      pango_layout_set_ellipsize(spl, PANGO_ELLIPSIZE_END);
      PangoRectangle subInk{};
      pango_layout_get_pixel_extents(spl, &subInk, nullptr);

      // Vertically center the two lines as a block inside the row.
      const double blockH =
          static_cast<double>(nameInk.height) + lineGap + static_cast<double>(subInk.height);
      const double blockTop = r.body.y + (r.body.h - blockH) * 0.5;

      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, d.connected ? 0.95 : 0.80);
      cairo_move_to(cr, nameX, blockTop - static_cast<double>(nameInk.y));
      pango_cairo_show_layout(cr, pl);

      cairo_set_source_rgba(cr, d.connecting ? 0.96 : mc.textR, d.connecting ? 0.72 : mc.textG,
                            d.connecting ? 0.30 : mc.textB, 0.5);
      cairo_save(cr);
      cairo_rectangle(cr, nameX, blockTop + static_cast<double>(nameInk.height) + lineGap, nameMaxW,
                      static_cast<double>(subInk.height) + 4.0 * us);
      cairo_clip(cr);
      cairo_move_to(cr, nameX, blockTop + static_cast<double>(nameInk.height) + lineGap -
                                   static_cast<double>(subInk.y));
      pango_cairo_show_layout(cr, spl);
      cairo_restore(cr);

      g_object_unref(spl);
      g_object_unref(pl);

      if (r.battery.w > 0 && d.has_battery) {
        char bbuf[8];
        std::snprintf(bbuf, sizeof(bbuf), "%d%%", static_cast<int>(d.battery_percent));
        double colR, colG, colB;
        bt_battery_color(static_cast<int>(d.battery_percent), colR, colG, colB);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11.0 * us);
        cairo_text_extents_t te;
        cairo_text_extents(cr, bbuf, &te);
        const double gsz = 15.0 * us;
        const double endX = r.battery.x + r.battery.w;
        const double cy = r.body.y + r.body.h * 0.5;
        const double glyphCx = endX - te.x_advance - 3.0 * us - gsz * 0.5;
        eh::shell::draw_material_glyph(cr, glyphCx, cy, gsz, bt_battery_glyph(static_cast<int>(d.battery_percent)),
                                       colR, colG, colB, 1.0);
        cairo_set_source_rgba(cr, colR, colG, colB, 0.95);
        cairo_move_to(cr, endX - te.x_advance, cy + te.height * 0.35);
        cairo_show_text(cr, bbuf);
      }

      {
        const char* action_label = d.connected ? "Disconnect" : (d.paired ? "Connect" : "Pair");
        const bool act_hov = r.action.contains(px, py);
        eh::shell::shared::rounded_rect(cr, r.action.x, r.action.y, r.action.w, r.action.h, r.action.h * 0.5);
        if (d.connected) {
          cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, act_hov ? 1.0 : 0.88);
        } else {
          cairo_set_source_rgba(cr, 1, 1, 1, act_hov ? 0.22 : 0.13);
        }
        cairo_fill(cr);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11.0 * us);
        cairo_text_extents_t ate;
        cairo_text_extents(cr, action_label, &ate);
        const double atx = r.action.x + (r.action.w - ate.x_advance) * 0.5;
        const double aty = r.action.y + r.action.h * 0.5 + ate.height * 0.35;
        if (d.connected) {
          cairo_set_source_rgba(cr, 1, 1, 1, act_hov ? 1.0 : 0.92);
        } else {
          cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.9);
        }
        cairo_move_to(cr, atx, aty);
        cairo_show_text(cr, action_label);
      }

      if (r.forget.w > 0) {
        const bool fog_hov = r.forget.contains(px, py);
        eh::shell::shared::rounded_rect(cr, r.forget.x, r.forget.y, r.forget.w, r.forget.h, 6.0 * us);
        cairo_set_source_rgba(cr, fog_hov ? 0.90 : 0.55, fog_hov ? 0.35 : 0.55,
                              fog_hov ? 0.30 : 0.60, fog_hov ? 0.30 : 0.10);
        cairo_fill(cr);
        eh::shell::draw_material_glyph(cr, r.forget.x + r.forget.w * 0.5, r.forget.y + r.forget.h * 0.5,
                                       14.0 * us, "delete", mc.textR, mc.textG, mc.textB, fog_hov ? 0.95 : 0.55);
      }
    }
    cairo_restore(cr);

    const double dy = L.items.back().body.y + L.items.back().body.h;
    cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.16);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, pad + 2.0 * us, dy);
    cairo_line_to(cr, W - pad - 2.0 * us, dy);
    cairo_stroke(cr);
  }

  if (L.scan.w > 0) {
    const bool scan_hov = L.scan.contains(px, py);
    eh::shell::shared::rounded_rect(cr, L.scan.x, L.scan.y, L.scan.w, L.scan.h, L.scan.h * 0.5);
    if (bt.scanning) {
      cairo_set_source_rgba(cr, 0.85, 0.27, 0.22, scan_hov ? 1.0 : 0.85);
    } else {
      cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, scan_hov ? 1.0 : 0.88);
    }
    cairo_fill(cr);

    const char* scan_label = bt.scanning ? "Stop scan" : "Scan for devices";
    const char* scan_glyph = bt.scanning ? "close" : "add";
    cairo_set_source_rgba(cr, 1, 1, 1, scan_hov ? 1.0 : 0.92);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12.0 * us);
    cairo_text_extents_t ste;
    cairo_text_extents(cr, scan_label, &ste);
    const double glyph_sz = 16.0 * us;
    const double total_glyph = glyph_sz + 6.0 * us + ste.x_advance;
    const double startX = L.scan.x + (L.scan.w - total_glyph) * 0.5;
    const double cy = L.scan.y + L.scan.h * 0.5;
    eh::shell::draw_material_glyph(cr, startX + glyph_sz * 0.5, cy, glyph_sz, scan_glyph, 1, 1, 1,
                                   scan_hov ? 1.0 : 0.92);
    cairo_move_to(cr, startX + glyph_sz + 6.0 * us, cy + ste.height * 0.35);
    cairo_show_text(cr, scan_label);
  }
}

BluetoothPopupClick bluetooth_popup_click_action(double x, double y) {
  eh::bt::FullState state;
  const bool debug = debug_full_state().has_value();
  if (auto dbg = debug_full_state()) {
    state = *dbg;
  } else {
    eh::bt::BluezService::instance().start();
    state = eh::bt::BluezService::instance().full_state();
  }
  const auto& bt = state.snap;
  const auto& devs = state.devices;

  const double W = static_cast<double>(kBluetoothPopupW);
  const double H = popup_height_for_state(bt, devs);
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);

  if (x < 0 || x >= W || y < 0 || y >= H) return BluetoothPopupClick::Close;

  const PopupLayout L = build_popup_layout(bt, devs, us, W, H);

  if (L.closeBtn.contains(x, y)) return BluetoothPopupClick::Close;

  if (bt.available && L.toggle.contains(x, y)) {
    if (!debug) eh::bt::BluezService::instance().set_powered(!bt.powered);
    return BluetoothPopupClick::Reopen;
  }

  for (const auto& item : L.items) {
    if (item.header) continue;
    const auto& d = *item.dev;
    const PopupItem& r = item;

    if (r.forget.w > 0 && r.forget.contains(x, y)) {
      if (!debug) eh::bt::BluezService::instance().forget_device(d.path);
      return BluetoothPopupClick::Redraw;
    }
    if (r.action.contains(x, y)) {
      if (!debug) {
        if (d.connected) {
          eh::bt::BluezService::instance().disconnect_device(d.path);
        } else if (d.paired) {
          eh::bt::BluezService::instance().connect_device(d.path);
        } else {
          eh::bt::BluezService::instance().pair_device(d.path);
        }
      }
      return BluetoothPopupClick::Redraw;
    }
  }

  if (L.scan.w > 0 && L.scan.contains(x, y)) {
    if (!debug) {
      if (bt.scanning) {
        eh::bt::BluezService::instance().stop_discovery();
      } else {
        eh::bt::BluezService::instance().start_discovery();
      }
    }
    return BluetoothPopupClick::Reopen;
  }
  return BluetoothPopupClick::None;
}

void dock_bluetooth_popup_handle_click(::DockApp& app, double x, double y, uint32_t serial) {
  switch (bluetooth_popup_click_action(x, y)) {
    case BluetoothPopupClick::Close: popup_close(app); break;
    case BluetoothPopupClick::Redraw: popup_draw_surface(app); break;
    case BluetoothPopupClick::Reopen: popup_open_bluetooth(app, app.popupAnchorX, serial); break;
    case BluetoothPopupClick::None: break;
  }
}

} // namespace eh::widgets