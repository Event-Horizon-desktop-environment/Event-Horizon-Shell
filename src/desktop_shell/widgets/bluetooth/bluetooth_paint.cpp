#include "desktop_shell/widgets/bluetooth/bluetooth_paint.hpp"
#include "desktop_shell/widgets/shared/slot_pill_style.hpp"
#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "desktop_shell/widgets/shared/widget_settings.hpp"
#include "desktop_shell/widgets/shared/measure_scratch.hpp"
#include "configuration/shell_config.hpp"
#include "services/bluetooth/bluez_service.hpp"
#include "desktop_shell/shared/popup/session/session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace eh::widgets {
namespace {

bool parse_bool_setting(const std::string& v, bool fallback) {
   
  if (v.empty()) return fallback;
  if (v == "1" || v == "true" || v == "True" || v == "yes" || v == "Yes") return true;
  if (v == "0" || v == "false" || v == "False" || v == "no" || v == "No") return false;
  return fallback;
}

struct BluetoothTextCache {
  std::string text;
  int textW = 0;
  int textH = 0;
};
static std::unordered_map<std::string, BluetoothTextCache> g_bluetoothTextCache;
static std::atomic<bool> g_btStateChanged{false};
static std::atomic<bool> g_btImmediateChanged{false};

}

void bluetooth_widget_init() {
    
  auto& bt = eh::bt::BluezService::instance();
  bt.start();
  bt.set_change_callback([]() {
    g_btStateChanged.store(true, std::memory_order_relaxed);
    g_btImmediateChanged.store(true, std::memory_order_relaxed);
  });
}

bool bluetooth_widget_poll() {
    
  // Periodically trim the text cache to prevent unbounded growth
  if (g_bluetoothTextCache.size() > 64) {
    g_bluetoothTextCache.clear();
  }
  return g_btStateChanged.exchange(false, std::memory_order_relaxed);
}

bool bluetooth_widget_needs_immediate_draw() {
    
  // Debounce: at most ~6 FPS for immediate BT-triggered redraws
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

// Mediated Bluetooth operations.

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
      .battery_pct = static_cast<int>(d.battery_percent),
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
  return {
    .available = s.available,
    .powered = s.powered,
    .connected = s.connected,
    .paired_count = s.paired_count,
    .connected_count = s.connected_count,
  };
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
  const bool showLabel = parse_bool_setting(widget_setting(sc, instance_id, "show_label"), false);

  if (!showLabel) {
    return icon_ref_px + 4.0 * us;
  }

  eh::bt::BluezService::instance().start();
  const auto btSnapshot = eh::bt::BluezService::instance().snapshot();
  if (!btSnapshot.connected || btSnapshot.connected_count == 0) {
    return icon_ref_px + 4.0 * us;
  }

  char buf[32];
  const int n = btSnapshot.connected_count;
  if (n == 1) {
    std::snprintf(buf, sizeof(buf), "%d device", n);
  } else {
    std::snprintf(buf, sizeof(buf), "%d devices", n);
  }

  const double padX = 6.0 * us;
  const double gap = 6.0 * us;
  const double minW = std::max(icon_ref_px * 1.25, 40.0 * us);

  if (!measure_cr) {
    measure_cr = get_measure_cr();
  }

  int tw = 0;
  const std::string btkid(instance_id);
  auto btit = g_bluetoothTextCache.find(btkid);
  if (btit != g_bluetoothTextCache.end() && btit->second.text == buf) {
    tw = btit->second.textW;
  } else {
    const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
    std::string fd = "Inter " + std::to_string(static_cast<int>(fontPx));
    PangoLayout* layout = pango_cairo_create_layout(measure_cr);
    PangoFontDescription* desc = pango_font_description_from_string(fd.c_str());
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(layout, buf, -1);
    int th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    g_object_unref(layout);
    BluetoothTextCache& btc = g_bluetoothTextCache[btkid];
    btc.text = buf;
    btc.textW = tw;
    btc.textH = th;
  }

  const double total = icon_ref_px + gap + static_cast<double>(tw) + padX * 2.0;
  return std::max(minW, total);
}

void paint_bluetooth_slot(cairo_t* cr, const eh::config::ShellConfig& sc, std::string_view instance_id,
                           double x, double y, double slot_w, double slot_h, double icon_ref_px,
                           bool hovered, bool pressed) {
  (void)hovered; (void)pressed;
  const double us = dock_ui_scale(sc.dock);
  const bool showLabel = parse_bool_setting(widget_setting(sc, instance_id, "show_label"), false);

  eh::bt::BluezService::instance().start();
  const auto btSnapshot = eh::bt::BluezService::instance().snapshot();

  cairo_save(cr);
  slot_pill_style::paint_pill(cr, x, y, slot_w, slot_h);

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const double fgOpacity = (!btSnapshot.available || !btSnapshot.powered) ? 0.55 : 1.0;
  double fgR = mc.textR, fgG = mc.textG, fgB = mc.textB;

  const double glyphPx = icon_ref_px * 0.52;

  if (showLabel && btSnapshot.connected && btSnapshot.connected_count > 0) {
    const double fontPx = std::clamp(icon_ref_px * 0.38, 11.0 * us, 15.0 * us);
    std::string fd = "Inter " + std::to_string(static_cast<int>(fontPx));
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* desc = pango_font_description_from_string(fd.c_str());
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);

    char buf[32];
    const int n = btSnapshot.connected_count;
    if (n == 1) {
      std::snprintf(buf, sizeof(buf), "%d device", n);
    } else {
      std::snprintf(buf, sizeof(buf), "%d devices", n);
    }
    pango_layout_set_text(layout, buf, -1);

    int tw = 0, th = 0;
    const std::string pbtkid(instance_id);
    auto pbtit = g_bluetoothTextCache.find(pbtkid);
    if (pbtit != g_bluetoothTextCache.end() && pbtit->second.text == buf) {
      tw = pbtit->second.textW;
      th = pbtit->second.textH;
    } else {
      pango_layout_get_pixel_size(layout, &tw, &th);
      BluetoothTextCache& btc2 = g_bluetoothTextCache[pbtkid];
      btc2.text = buf;
      btc2.textW = tw;
      btc2.textH = th;
    }

    const double gap = 6.0 * us;
    const double totalInner = icon_ref_px + gap + static_cast<double>(tw);
    const double innerX = x + (slot_w - totalInner) * 0.5;
    const double glyphCx = innerX + icon_ref_px * 0.5;
    const double glyphCy = y + slot_h * 0.5;

    eh::shell::draw_material_glyph(cr, glyphCx, glyphCy, glyphPx, "bluetooth", fgR, fgG, fgB, fgOpacity);

    const double labelX = innerX + icon_ref_px + gap;
    const double labelY = y + (slot_h - static_cast<double>(th)) * 0.5;
    cairo_set_source_rgba(cr, fgR, fgG, fgB, fgOpacity);
    cairo_move_to(cr, labelX, labelY);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
  } else {
    const double gx = x + slot_w * 0.5;
    const double gy = y + slot_h * 0.5;
    eh::shell::draw_material_glyph(cr, gx, gy, glyphPx, "bluetooth", fgR, fgG, fgB, fgOpacity);
  }

  cairo_restore(cr);
}

// Popup.

static int bluetooth_popup_height_for_state(const eh::bt::Snapshot& /*bt*/, const std::vector<eh::bt::DeviceInfo>& devs) {
    
  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  constexpr int kMaxDevices = 8;
  const int n = std::min(kMaxDevices, static_cast<int>(devs.size()));
  const int headerH = 40;
  const int statusH = 22 + 22 + 8;
  const int rowsH = n * 34;
  const int gap = 8;
  const int btnH = 32;
  const int pad = 16;
  return static_cast<int>((headerH + statusH + rowsH + gap + btnH + pad) * us);
}

int bluetooth_popup_height() {
    
  eh::bt::BluezService::instance().start();
  const auto state = eh::bt::BluezService::instance().full_state();
  return bluetooth_popup_height_for_state(state.snap, state.devices);
}

void dock_bluetooth_popup_paint(double pointerX, double pointerY, cairo_t* cr, const eh::config::ShellConfig& sc) {
    
  eh::bt::BluezService::instance().start();
  const auto state = eh::bt::BluezService::instance().full_state();
  const auto& bt = state.snap;
  const auto& devs = state.devices;

  const double W = static_cast<double>(kBluetoothPopupW);
  const double H = static_cast<double>(bluetooth_popup_height_for_state(bt, devs));

  const auto& mc = eh::config::derived_chrome_colors(sc.appearance);
  const double shellOv = static_cast<double>(
      eh::config::overlay_surface_alpha_scale(sc, eh::config::OverlaySurfaceAlphaKind::Weather));
  const double us = dock_ui_scale(sc.dock);
  const double kPad = 16.0 * us;
  const double kCornerRadius = 14.0 * us;
  const double kShadOffX = 2.0 * us, kShadOffY = 4.0 * us;
  constexpr double kShadAlpha = 0.28;

  auto round_rect = [&](double x, double y, double w, double h, double r) {
    const double rad = std::min({r, w * 0.5, h * 0.5});
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - rad, y + rad,     rad, -M_PI_2,      0);
    cairo_arc(cr, x + w - rad, y + h - rad, rad,       0, M_PI_2);
    cairo_arc(cr, x + rad,     y + h - rad, rad,  M_PI_2,   M_PI);
    cairo_arc(cr, x + rad,     y + rad,     rad,     M_PI, 3 * M_PI_2);
    cairo_close_path(cr);
  };

  cairo_save(cr);
  round_rect(kShadOffX, kShadOffY, W, H, kCornerRadius);
  cairo_set_source_rgba(cr, 0, 0, 0, kShadAlpha);
  cairo_fill(cr);
  cairo_restore(cr);

  {
    m3::Box box;
    box.setColor(static_cast<float>(mc.dockFillR * 0.35), static_cast<float>(mc.dockFillG * 0.35),
                 static_cast<float>(mc.dockFillB * 0.35), static_cast<float>(0.78 * shellOv));
    box.setRadius(static_cast<float>(kCornerRadius));
    box.setGeometry(0, 0, static_cast<float>(W), static_cast<float>(H));
    box.setGlassy(true);
    box.paint(cr);
  }

  round_rect(0.5, 0.5, W - 1.0, H - 1.0, kCornerRadius);
  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  const double cbX = W - kPad - 24.0 * us, cbY = kPad;
  const double cbW = 24.0 * us, cbH = 24.0 * us;
  const double px = pointerX, py = pointerY;
  const bool hc = px >= cbX && px < cbX + cbW && py >= cbY && py < cbY + cbH;
  round_rect(cbX, cbY, cbW, cbH, 7 * us);
  cairo_set_source_rgba(cr, 0.3, 0.3, 0.35, hc ? 0.75 : 0.45);
  cairo_fill(cr);
  eh::shell::draw_material_glyph(cr, cbX + cbW * 0.5, cbY + cbH * 0.5,
                                 14 * us, "close", mc.textR, mc.textG, mc.textB, hc ? 1.0 : 0.85);

  double yPos = kPad;

  cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14 * us);
  cairo_move_to(cr, kPad, yPos + 22.0 * us);
  cairo_show_text(cr, "Bluetooth");

  eh::shell::draw_material_glyph(cr, kPad + 100.0 * us, yPos + 16.0 * us, 18 * us,
                                 "bluetooth", mc.textR, mc.textG, mc.textB, 0.85);

  yPos += 40.0 * us;

  if (!bt.available) {
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13 * us);
    cairo_move_to(cr, kPad, yPos);
    cairo_show_text(cr, "No Bluetooth adapter");
    return;
  }

  // Power status
  cairo_set_source_rgba(cr, bt.powered ? 0.4 : mc.textR, bt.powered ? 0.8 : mc.textG,
                        bt.powered ? 0.4 : mc.textB, 0.7);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13 * us);
  cairo_move_to(cr, kPad, yPos);
  cairo_show_text(cr, bt.powered ? "On" : "Off");
  yPos += 22.0 * us;

  // Connected / paired count
  {
    char buf[64];
    if (bt.connected_count > 0) {
      if (bt.connected_count == 1) {
        std::snprintf(buf, sizeof(buf), "%d device connected", bt.connected_count);
      } else {
        std::snprintf(buf, sizeof(buf), "%d devices connected", bt.connected_count);
      }
    } else {
      std::snprintf(buf, sizeof(buf), "No devices connected");
    }
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13 * us);
    cairo_move_to(cr, kPad, yPos);
    cairo_show_text(cr, buf);
    yPos += 22.0 * us;
  }

  if (bt.paired_count > 0) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d paired device%s", bt.paired_count,
                  bt.paired_count == 1 ? "" : "s");
    cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.5);
    cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 13 * us);
    cairo_move_to(cr, kPad, yPos);
    cairo_show_text(cr, buf);
    yPos += 22.0 * us;
  }

  yPos += 8.0 * us;

  // Device list (scrollable area)
  {
    constexpr int kMaxDevices = 8;
    const int n = std::min(kMaxDevices, static_cast<int>(devs.size()));
    const double rowH = 34.0 * us;
    const double listY = yPos;
    const double listH = static_cast<double>(n) * rowH;
    const double rowX = kPad + 4.0 * us;
    const double rowW = W - kPad * 2.0 - 8.0 * us;

    // Clip to device list area
    cairo_save(cr);
    cairo_rectangle(cr, rowX, listY, rowW, listH);
    cairo_clip(cr);

    for (int i = 0; i < n; ++i) {
      const auto& d = devs[static_cast<size_t>(i)];
      const double ry = listY + static_cast<double>(i) * rowH;
      const bool rowHov = px >= rowX && px < rowX + rowW && py >= ry && py < ry + rowH;

      if (rowHov) {
        round_rect(rowX, ry + 2.0 * us, rowW, rowH - 4.0 * us, 8.0 * us);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        cairo_fill(cr);
      }

      // Device icon
      const char* glyph = eh::bt::bluetooth_device_kind_glyph(d.kind);
      eh::shell::draw_material_glyph(cr, rowX + 14.0 * us, ry + rowH * 0.5, 14.0 * us, glyph,
                                     d.connected ? 0.30 : 0.72, d.connected ? 0.85 : 0.78,
                                     d.connected ? 0.30 : 0.82, 1.0);

      // Device name (clipped to make room for buttons)
      cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, 0.92);
      cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12.0 * us);
      const std::string& label = d.alias.empty() ? d.address : d.alias;
      // Clamp name width so it doesn't overlap buttons
      const double nameMaxW = rowX + rowW - 140.0 * us;
      cairo_save(cr);
      cairo_rectangle(cr, rowX + 30.0 * us, ry, std::max(0.0, nameMaxW - rowX - 30.0 * us), rowH);
      cairo_clip(cr);
      cairo_move_to(cr, rowX + 30.0 * us, ry + 20.0 * us);
      cairo_show_text(cr, label.c_str());
      cairo_restore(cr);

      // Action buttons.
      const char* actionText = d.connected ? "Disconnect" : (d.paired ? "Connect" : "Pair");
      const double actionBtnW = d.connected ? 74.0 * us : (d.paired ? 60.0 * us : 40.0 * us);
      const bool showForget = d.paired;
      const double forgetBtnW = 48.0 * us;
      const double btnGap = 6.0 * us;
      const double btnAreaW = actionBtnW + (showForget ? (btnGap + forgetBtnW) : 0.0);
      const double btnStartX = rowX + rowW - btnAreaW - 4.0 * us;
      const double btnH = rowH - 8.0 * us;
      const double btnY = ry + (rowH - btnH) * 0.5;

      auto draw_btn = [&](const char* text, double bx, double bw) {
        round_rect(bx, btnY, bw, btnH, 5.0 * us);
        const bool btnHov = px >= bx && px < bx + bw && py >= btnY && py < btnY + btnH;
        cairo_set_source_rgba(cr, btnHov ? 0.35 : 0.22, btnHov ? 0.35 : 0.22, btnHov ? 0.40 : 0.28, 0.7);
        cairo_fill(cr);
        cairo_set_source_rgba(cr, mc.textR, mc.textG, mc.textB, btnHov ? 0.95 : 0.80);
        cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10.0 * us);
        cairo_text_extents_t bte;
        cairo_text_extents(cr, text, &bte);
        cairo_move_to(cr, bx + (bw - bte.x_advance) * 0.5, btnY + btnH * 0.5 + bte.height * 0.35);
        cairo_show_text(cr, text);
      };

      draw_btn(actionText, btnStartX, actionBtnW);
      if (showForget)
        draw_btn("Forget", btnStartX + actionBtnW + btnGap, forgetBtnW);
    }

    cairo_restore(cr);

    // Divider line above device list
    if (n > 0) {
      cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.15);
      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, kPad + 8.0 * us, yPos);
      cairo_line_to(cr, W - kPad - 8.0 * us, yPos);
      cairo_stroke(cr);
    }

    yPos += listH + 8.0 * us;
  }

  // Scan button
  const double btnW = 120.0 * us;
  const double btnH = 32.0 * us;
  const double btnX = (W - btnW) * 0.5;
  const double btnY = H - kPad - btnH;
  const bool scanHover = px >= btnX && px < btnX + btnW && py >= btnY && py < btnY + btnH;
  round_rect(btnX, btnY, btnW, btnH, 6 * us);
  if (bt.scanning) {
    cairo_set_source_rgba(cr, 0.85, 0.25, 0.20, scanHover ? 0.85 : 0.65);
  } else {
    cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, scanHover ? 0.85 : 0.65);
  }
  cairo_fill(cr);

  const char* scanLbl = bt.scanning ? "Stop scan" : "Scan";
  cairo_set_source_rgba(cr, 1, 1, 1, scanHover ? 1.0 : 0.90);
  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 12 * us);
  cairo_text_extents_t te;
  cairo_text_extents(cr, scanLbl, &te);
  cairo_move_to(cr, btnX + (btnW - te.x_advance) * 0.5, btnY + btnH * 0.5 + te.height * 0.35);
  cairo_show_text(cr, scanLbl);
}

void dock_bluetooth_popup_handle_click(::DockApp& app, double x, double y, uint32_t serial) {
    
  eh::bt::BluezService::instance().start();
  const auto state = eh::bt::BluezService::instance().full_state();
  const auto& bt = state.snap;
  const auto& devs = state.devices;

  const double W = static_cast<double>(kBluetoothPopupW);
  const double H = static_cast<double>(bluetooth_popup_height_for_state(bt, devs));

  if (x < 0 || x >= W || y < 0 || y >= H) {
    popup_close(app);
    return;
  }

  const double us = dock_ui_scale(eh::config::shell_config_snapshot().dock);
  const double kPad = 16.0 * us;
  const double cbX = W - kPad - 24.0 * us, cbY = kPad;
  const double cbW = 24.0 * us, cbH = 24.0 * us;

  if (x >= cbX && x < cbX + cbW && y >= cbY && y < cbY + cbH) {
    popup_close(app);
    return;
  }

  // Device row buttons
  {
    constexpr int kMaxDevices = 8;
    const int n = std::min(kMaxDevices, static_cast<int>(devs.size()));
    const double rowH = 34.0 * us;
    const double rowX = kPad + 4.0 * us;
    const double rowW = W - kPad * 2.0 - 8.0 * us;

    double yPos = kPad + 40.0 * us + 22.0 * us + 22.0 * us;
    if (bt.paired_count > 0) yPos += 22.0 * us;
    yPos += 8.0 * us;

    for (int i = 0; i < n; ++i) {
      const auto& d = devs[static_cast<size_t>(i)];
      const double ry = yPos + static_cast<double>(i) * rowH;

      const double actionBtnW = d.connected ? 74.0 * us : (d.paired ? 60.0 * us : 40.0 * us);
      const bool showForget = d.paired;
      const double forgetBtnW = 48.0 * us;
      const double btnGap = 6.0 * us;
      const double btnAreaW = actionBtnW + (showForget ? (btnGap + forgetBtnW) : 0.0);
      const double btnStartX = rowX + rowW - btnAreaW - 4.0 * us;
      const double btnH = rowH - 8.0 * us;
      const double btnY = ry + (rowH - btnH) * 0.5;

      // Action button
      if (x >= btnStartX && x < btnStartX + actionBtnW && y >= btnY && y < btnY + btnH) {
        if (d.connected)
          eh::bt::BluezService::instance().disconnect_device(d.path);
        else if (d.paired)
          eh::bt::BluezService::instance().connect_device(d.path);
        else
          eh::bt::BluezService::instance().pair_device(d.path);
        popup_draw_surface(app);
        return;
      }

      // Forget button
      if (showForget) {
        const double forgetBtnX = btnStartX + actionBtnW + btnGap;
        if (x >= forgetBtnX && x < forgetBtnX + forgetBtnW && y >= btnY && y < btnY + btnH) {
          eh::bt::BluezService::instance().forget_device(d.path);
          popup_draw_surface(app);
          return;
        }
      }
    }
  }

  // Scan button
  const double btnW = 120.0 * us;
  const double btnH = 32.0 * us;
  const double btnX = (W - btnW) * 0.5;
  const double btnY = H - kPad - btnH;
  if (x >= btnX && x < btnX + btnW && y >= btnY && y < btnY + btnH) {
    if (bt.scanning)
      eh::bt::BluezService::instance().stop_discovery();
    else
      eh::bt::BluezService::instance().start_discovery();
    // Reopen popup to accommodate devices
    popup_open_bluetooth(app, app.popupAnchorX, serial);
    return;
  }
}

}
