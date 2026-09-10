#include <cairo/cairo.h>

#ifdef EH_HAVE_RSVG
#include <librsvg/rsvg.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_network/settings_tab_network.hpp"
#include "ux/settings/settings_tab_network/dialogs/wifi_password_prompt.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

extern void draw(App& app);

static constexpr int kWifiCardTop    = kContentTop;
static constexpr int kWifiCardH      = 52 + 48;
static constexpr int kApListHeaderH  = 52;
static constexpr int kApRowH         = 52;
static constexpr int kBtnW           = 64;
static constexpr int kBtnH           = 24;

#ifdef EH_HAVE_RSVG
static int strength_idx(std::uint8_t strength) {
  if (strength >= 80) return 3;
  if (strength >= 55) return 2;
  if (strength >= 30) return 1;
  return 0;
}
#endif

#ifdef EH_HAVE_RSVG
static cairo_surface_t* wifi_svg_icon(bool isDark, int idx) {
   
  static cairo_surface_t* cache[2][4] = {};
  const int mi = isDark ? 0 : 1;
  if (idx < 0 || idx > 3) return nullptr;
  if (cache[mi][idx]) return cache[mi][idx];

  const char* subdir = isDark ? "WI-FI-Dark" : "WI-FI-Light";
  char fname[64];
  std::snprintf(fname, sizeof(fname), "WI-FI/%s/WI-FI-%d.svg", subdir, idx);

  auto try_load = [&](const std::string& base) -> cairo_surface_t* {
    std::string p = base + "/" + fname;
    if (::access(p.c_str(), R_OK) != 0) return nullptr;
    GError* err = nullptr;
    RsvgHandle* h = rsvg_handle_new_from_file(p.c_str(), &err);
    if (!h) {
      if (err) g_error_free(err);
      return nullptr;
    }
    const int size = 24;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      if (surf) cairo_surface_destroy(surf);
      g_object_unref(h);
      return nullptr;
    }
    cairo_t* cr = cairo_create(surf);
    RsvgRectangle viewport{0, 0, static_cast<double>(size), static_cast<double>(size)};
    GError* renderErr = nullptr;
    const gboolean ok = rsvg_handle_render_document(h, cr, &viewport, &renderErr);
    cairo_destroy(cr);
    g_object_unref(h);
    if (!ok) {
      if (renderErr) g_error_free(renderErr);
      cairo_surface_destroy(surf);
      return nullptr;
    }
    return surf;
  };

  if (const char* d = std::getenv("EH_ASSETS_DIR"); d && *d)
    if (auto* s = try_load(d)) { cache[mi][idx] = s; return s; }
  if (auto* s = try_load("assets")) { cache[mi][idx] = s; return s; }
  if (auto* s = try_load("src/assets")) { cache[mi][idx] = s; return s; }
  {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      std::string exe(buf);
      const auto slash = exe.find_last_of('/');
      if (slash != std::string::npos) {
        std::string dir = exe.substr(0, slash);
        if (auto* s = try_load(dir + "/../assets")) { cache[mi][idx] = s; return s; }
        if (auto* s = try_load(dir + "/../src/assets")) { cache[mi][idx] = s; return s; }
      }
    }
  }
  if (const char* h = std::getenv("HOME"); h && *h) {
    if (auto* s = try_load(std::string(h) + "/.local/share/event-horizon/assets")) { cache[mi][idx] = s; return s; }
  }
  for (const char* base : {"/usr/local/share/event-horizon/assets", "/usr/share/event-horizon/assets"}) {
    if (auto* s = try_load(base)) { cache[mi][idx] = s; return s; }
  }
  return nullptr;
}
#endif

static void draw_wifi_icon(App& app, cairo_t* cr, double cx, double cy, double size, std::uint8_t strength, double alpha) {
  (void)cr;
    
#ifdef EH_HAVE_RSVG
  const bool isDark = !app.drawChromeMatugen || app.settings.matugenMode == "dark";
  cairo_surface_t* svg = wifi_svg_icon(isDark, strength_idx(strength));
  if (svg) {
    cairo_save(cr);
    cairo_set_source_surface(cr, svg, cx - size * 0.5, cy - size * 0.5);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
    return;
  }
#endif
  (void)app; (void)cx; (void)cy; (void)size; (void)strength; (void)alpha;
}

static const char* band_label(std::uint32_t freq) {
  if (freq == 0) return "";
  if (freq < 3000) return "2.4G";
  if (freq < 6000) return "5G";
  return "6G";
}

static bool btn_hit(int px, int py, int btnX, int btnY) {
  return px >= btnX && px < btnX + kBtnW && py >= btnY && py < btnY + kBtnH;
}

// Compute the right-edge layout for an AP row.  Returns the x of the connect button.
static int ap_row_btn_x(int rowRightX) {
  // rightX = cx + cw - kCardPad  (input rowRightX)
  // pctX ≈ rightX - 22  (% text ~22px wide)
  // btnX = pctX - kBtnW - 10  (button left of %)
  return rowRightX - 22 - kBtnW - 10;
}

static int ap_row_btn_y(int rowY) {
  return rowY + 5;
}

void paint_wifi_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv,
                    double paintPointerYOffset) {
   
  (void)paintPointerYOffset;
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  const auto& st = nm.state();

  const int cx = contentX + kCardPad;
  const int cw = contentW - 2 * kCardPad;

  // Card 1: Wi-Fi toggle + scan
  settings_card(app, cr, static_cast<double>(cx), static_cast<double>(kWifiCardTop),
                static_cast<double>(cw), static_cast<double>(kWifiCardH), glassOv);

  settings_show_text(cr, cx + kCardPad, kWifiCardTop + 30, "Wi-Fi", 14.0, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  // Toggle
  const int toggleX = cx + cw - kCardPad - 52;
  const int toggleY = kWifiCardTop + 10;
  const bool on = st.wirelessEnabled;
  {
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    const uint32_t now = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    auto& toggle = app.wifiToggle;
    toggle.setGeometry(static_cast<float>(toggleX), static_cast<float>(toggleY), 52.0f, 26.0f);
    toggle.setOn(on, now);
    toggle.setAccentColor(a_r, a_g, a_b);
    toggle.setSurfaceColor(s_r, s_g, s_b);
    toggle.setTextColor(t_r, t_g, t_b);
    toggle.setOutlineColor(o_r, o_g, o_b);
    toggle.setHovered(false);
    toggle.paint(cr, now);
  }

  // Scan button
  if (on) {
    const int scanBtnX = cx + cw - kCardPad - 52 - kSpacingL - 80;
    const int scanBtnY = kWifiCardTop + 9;
    const int scanBtnW = 80;
    const int scanBtnH = 28;
    const bool scanHover =
        point_in_rect(app.pointerX, app.pointerY, scanBtnX, scanBtnY, scanBtnW, scanBtnH);
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button scanBtn;
    scanBtn.setMinSize(0, 0);
    scanBtn.setLabel(st.scanning ? "Scanning" : "Scan");
    scanBtn.setGeometry(static_cast<float>(scanBtnX), static_cast<float>(scanBtnY),
                         static_cast<float>(scanBtnW), static_cast<float>(scanBtnH));
    scanBtn.setStyle(m3::Button::Style::Outlined);
    scanBtn.setSize(m3::Button::Size::XS);
    scanBtn.setAccentColor(a_r, a_g, a_b);
    scanBtn.setOutlineColor(o_r, o_g, o_b);
    scanBtn.setHovered(scanHover);
    scanBtn.setEnabled(!st.scanning);
    scanBtn.paint(cr);
  }

  if (!on) return;

  // Connection status card
  const bool wifiConnected = (st.connected && st.kind == NetworkConnectivity::Wireless);
  const int statusCardTop = kWifiCardTop + kWifiCardH + kCardGap;
  const int statusCardH = wifiConnected ? 110 : 52;
  settings_card(app, cr, static_cast<double>(cx), static_cast<double>(statusCardTop),
                static_cast<double>(cw), static_cast<double>(statusCardH), glassOv);

  if (wifiConnected) {
    settings_show_text(cr, cx + kCardPad, statusCardTop + 30, "Connected to Wi-Fi", 14.0, 700, Theme::AccR, Theme::AccG, Theme::AccB, 1.0);

    draw_wifi_icon(app, cr, cx + cw - kCardPad - 30, statusCardTop + 62, 28, st.signalStrength, 1.0);

    settings_show_text(cr, cx + kCardPad, statusCardTop + 58, st.ssid.c_str(), 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    char sbuf[16];
    std::snprintf(sbuf, sizeof(sbuf), "%d%%", static_cast<int>(st.signalStrength));
    settings_show_text(cr, cx + kCardPad + 4, statusCardTop + 78, sbuf, 10.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    if (!st.interfaceName.empty()) {
      std::string iface = "Interface: " + st.interfaceName;
      settings_show_text(cr, cx + kCardPad, statusCardTop + 98, iface.c_str(), 11.f, 400,
                         Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);
    }
  } else {
    settings_show_text(cr, cx + kCardPad, statusCardTop + 30, "Not connected", 13.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);
  }

  // Card 3: Access points list
  const int apCardTop = statusCardTop + statusCardH + kCardGap;
  const int totalApRows = static_cast<int>(st.accessPoints.size());
  const int apListContentH = totalApRows * kApRowH;
  const int maxApListH =
      std::max(60, app.height - apCardTop - kApListHeaderH - kSpacingL - kSpacingL);
  const int apListH = kApListHeaderH + std::min(apListContentH, maxApListH);

  settings_card(app, cr, static_cast<double>(cx), static_cast<double>(apCardTop),
                static_cast<double>(cw), static_cast<double>(apListH), glassOv);

  settings_show_text(cr, cx + kCardPad, apCardTop + 30, "Available Networks", 14.f, 700,
                     Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);

  const int listY0 = apCardTop + kApListHeaderH;
  const int listH = apListH - kApListHeaderH;
  cairo_save(cr);
  cairo_rectangle(cr, static_cast<double>(cx + kCardPad), static_cast<double>(listY0),
                  static_cast<double>(cw - 2 * kCardPad), static_cast<double>(listH));
  cairo_clip(cr);

  const int scrollMax = std::max(0, apListContentH - listH);
  const int scrollPx = std::min(app.settingsNetworkScrollPx, scrollMax);
  const int drawY0 = listY0 - scrollPx;

  for (int i = 0; i < totalApRows; ++i) {
    const auto& ap = st.accessPoints[static_cast<size_t>(i)];
    const int ry = drawY0 + i * kApRowH;
    if (ry + kApRowH < listY0 || ry > listY0 + listH) continue;

    const bool hover = point_in_rect(app.pointerX, app.pointerY, cx + kCardPad, ry,
                                     cw - 2 * kCardPad, kApRowH);
    if (hover) {
      m3::Box box;
      float r, g, b;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.panelFillR;
        g = app.drawChrome.panelFillG;
        b = app.drawChrome.panelFillB;
      } else {
        r = static_cast<float>(Theme::BgR);
        g = static_cast<float>(Theme::BgG);
        b = static_cast<float>(Theme::BgB);
      }
      box.setColor(r, g, b, 0.18f);
      box.setRadius(8.f);
      box.setGeometry(cx + kCardPad + 2, ry, cw - 2 * kCardPad - 4, kApRowH);
      box.paint(cr);
    }

    draw_wifi_icon(app, cr, cx + kCardPad + 20, ry + kApRowH * 0.5, 20, ap.strength, 1.0);

    // Band label + security type
    {
      auto show_text_w = [&](double px, double py, const char* t, float s, int w,
                             float rr, float gg, float bb, float aa) -> double {
        if (!t || !t[0]) return 0;
        auto* l = pango_cairo_create_layout(cr);
        auto* d = pango_font_description_new();
        pango_font_description_set_family(d, "Inter");
        pango_font_description_set_size(d, static_cast<int>(s * PANGO_SCALE));
        pango_font_description_set_weight(d, static_cast<PangoWeight>(w));
        pango_layout_set_font_description(l, d);
        pango_layout_set_text(l, t, -1);
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(l, &pw, &ph);
        cairo_save(cr);
        cairo_translate(cr, px, py - static_cast<double>(ph) + 2.0);
        cairo_set_source_rgba(cr, rr, gg, bb, aa);
        pango_cairo_show_layout(cr, l);
        cairo_restore(cr);
        pango_font_description_free(d);
        g_object_unref(l);
        return static_cast<double>(pw);
      };

      double ssidW = show_text_w(cx + kCardPad + 48, ry + kApRowH * 0.5 + 6,
                                 ap.ssid.c_str(), 13.f, 400,
                                 Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);
      double labelX = cx + kCardPad + 52 + ssidW + 4;

      const char* bl = band_label(ap.frequency);
      if (bl[0] != '\0') {
        labelX += show_text_w(labelX, ry + kApRowH * 0.5 + 4, bl, 9.f, 400,
                              Theme::TextR, Theme::TextG, Theme::TextB, 1.0f) + 4;
      }

      if (!ap.securityType.empty()) {
        show_text_w(labelX, ry + kApRowH * 0.5 + 4, ap.securityType.c_str(), 9.f, 400,
                    Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);
      }
    }

    // Right side: connect/disconnect button, then %, then active dot
    double rightX = cx + cw - kCardPad;

    // Strength percent text
    char sbuf[16];
    std::snprintf(sbuf, sizeof(sbuf), "%d%%", static_cast<int>(ap.strength));
    settings_show_text(cr, rightX, ry + kApRowH * 0.5 + 5, sbuf, 10.f, 400,
                       Theme::TextR, Theme::TextG, Theme::TextB, 1.0f);

    // Connect / Disconnect button
    const int btnX = ap_row_btn_x(rightX);
    const int btnY = ap_row_btn_y(ry);
    const bool btnHover = btn_hit(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), btnX, btnY);
    const bool isActive = ap.active;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button apBtn;
    apBtn.setMinSize(0, 0);
    apBtn.setLabel(isActive ? "Disconnect" : "Connect");
    apBtn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                       static_cast<float>(kBtnW), static_cast<float>(kBtnH));
    apBtn.setStyle(m3::Button::Style::Outlined);
    apBtn.setSize(m3::Button::Size::XS);
    apBtn.setAccentColor(a_r, a_g, a_b);
    apBtn.setOutlineColor(o_r, o_g, o_b);
    apBtn.setHovered(btnHover);
    apBtn.paint(cr);

    if (ap.active) {
      cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 1.0);
      cairo_arc(cr, btnX - 42, ry + kApRowH * 0.5 - 2.0, 4, 0, 2 * M_PI);
      cairo_fill(cr);
    }
  }

  cairo_restore(cr);

  // Scrollbar
  if (apListContentH > listH) {
    const int sbTop = listY0;
    const int sbH = listH;
    const int sbX = cx + cw - kCardPad - 6;
    const int sbTrackW = 4;
    const double thumbH =
        static_cast<double>(sbH) * static_cast<double>(sbH) / static_cast<double>(apListContentH);
    const double thumbY = static_cast<double>(sbTop) +
                          static_cast<double>(scrollPx) *
                              (static_cast<double>(sbH) - thumbH) /
                              static_cast<double>(apListContentH - sbH);
    {
      m3::Box box;
      float r, g, b;
      if (app.drawChromeMatugen) {
        r = app.drawChrome.panelFillR;
        g = app.drawChrome.panelFillG;
        b = app.drawChrome.panelFillB;
      } else {
        r = static_cast<float>(Theme::BgR);
        g = static_cast<float>(Theme::BgG);
        b = static_cast<float>(Theme::BgB);
      }
      box.setColor(r, g, b, 0.15f);
      box.setRadius(2.f);
      box.setGeometry(sbX, sbTop, sbTrackW, sbH);
      box.paint(cr);
    }
    {
      m3::Box box;
      float hr, hg, hb;
      if (app.drawChromeMatugen) {
        hr = 0.48f + 0.52f * app.drawChrome.outlineR;
        hg = 0.48f + 0.52f * app.drawChrome.outlineG;
        hb = 0.48f + 0.52f * app.drawChrome.outlineB;
      } else {
        hr = 1.f; hg = 1.f; hb = 1.f;
      }
      box.setColor(hr, hg, hb, 0.25f);
      box.setRadius(2.f);
      box.setGeometry(sbX, thumbY, sbTrackW, thumbH);
      box.paint(cr);
    }
  }
}

bool settings_wifi_consume_pointer_down(App& app, int contentX, int contentW) {
   
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  nm.refresh();
  const auto& st = nm.state();

  const int cx = contentX + kCardPad;
  const int cw = contentW - 2 * kCardPad;

  // Wi-Fi toggle
  {
    const int toggleX = cx + cw - kCardPad - 52;
    const int toggleY = kWifiCardTop + 10;
    if (point_in_rect(app.pointerX, app.pointerY, toggleX, toggleY, 52, 26)) {
      nm.setWirelessEnabled(!st.wirelessEnabled);
      app.settingsNetworkScrollPx = 0;
      draw(app);
      return true;
    }
  }

  // Scan button
  {
    const int scanBtnX = cx + cw - kCardPad - 52 - kSpacingL - 80;
    const int scanBtnY = kWifiCardTop + 9;
    if (point_in_rect(app.pointerX, app.pointerY, scanBtnX, scanBtnY, 80, 28)) {
      nm.requestScan();
      return true;
    }
  }

  // AP list clicks — check Connect/Disconnect buttons first, then row body
  {
    const bool wifiConnected = (st.connected && st.kind == NetworkConnectivity::Wireless);
    const int statusCardH = wifiConnected ? 110 : 52;
    const int statusCardTop = kWifiCardTop + kWifiCardH + kCardGap;
    const int apCardTop = statusCardTop + statusCardH + kCardGap;
    const int totalApRows = static_cast<int>(st.accessPoints.size());
    const int apListContentH = totalApRows * kApRowH;
    const int maxApListH =
        std::max(60, app.height - apCardTop - kApListHeaderH - kSpacingL - kSpacingL);
    const int apListH = kApListHeaderH + std::min(apListContentH, maxApListH);
    const int listY0 = apCardTop + kApListHeaderH;
    const int listH = apListH - kApListHeaderH;
    const int scrollMax = std::max(0, apListContentH - listH);
    const int scrollPx = std::min(app.settingsNetworkScrollPx, scrollMax);

    for (int i = 0; i < totalApRows; ++i) {
      const auto& ap = st.accessPoints[static_cast<size_t>(i)];
      const int ry = listY0 - scrollPx + i * kApRowH;

      // Connect / Disconnect button hit test
      const int btnX = ap_row_btn_x(cx + cw - kCardPad);
      const int btnY = ap_row_btn_y(ry);

      if (btn_hit(static_cast<int>(app.pointerX), static_cast<int>(app.pointerY), btnX, btnY)) {
        if (ap.active) {
          nm.disconnect();
        } else if (ap.secured && !nm.hasSavedConnection(ap.ssid)) {
          wifi_password_prompt_open(app, ap.ssid, ap.strength);
        } else {
          nm.activateAccessPoint(ap);
        }
        return true;
      }

      // Row body hit
      if (point_in_rect(app.pointerX, app.pointerY, cx + kCardPad, ry, cw - 2 * kCardPad,
                        kApRowH)) {
        if (ap.active) {
          nm.disconnect();
        } else if (ap.secured && !nm.hasSavedConnection(ap.ssid)) {
          wifi_password_prompt_open(app, ap.ssid, ap.strength);
        } else {
          nm.activateAccessPoint(ap);
        }
        return true;
      }
    }
  }

  return false;
}

bool settings_wifi_consume_pointer_up(App& app, int contentX, int contentW) {
  (void)app;
  (void)contentX;
  (void)contentW;
  return false;
}
