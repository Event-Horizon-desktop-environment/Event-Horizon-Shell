#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_network/settings_tab_network.hpp"
#include "ux/settings/settings_tab_network/dialogs/vpn_add_dialog.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

// Layout constants.
static constexpr int kWifiCardTop    = kContentTop;
static constexpr int kWifiCardH      = 52 + 48; // header row + toggle row
static constexpr int kWifiCardBody   = kWifiCardTop + 52;

static constexpr int kApListCardTop  = kWifiCardTop + kWifiCardH + kCardGap;
static constexpr int kApListHeaderH  = 52;
static constexpr int kApRowH         = 48;

// WiFi tab.

// VPN tab.
static void measure_btn_text(cairo_t* cr, const char* label, float fontSize,
                             int& textW, int& textH) {
   
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_from_string("sans");
  pango_font_description_set_size(desc, fontSize * PANGO_SCALE);
  pango_font_description_set_weight(desc, PANGO_WEIGHT_MEDIUM);
  pango_layout_set_font_description(layout, desc);
  pango_layout_set_text(layout, label, -1);
  pango_layout_get_pixel_size(layout, &textW, &textH);
  pango_font_description_free(desc);
  g_object_unref(layout);
}

static constexpr int kBtnPad      = 4;
static constexpr int kVpnRowH     = 44;
static constexpr int kRemoveBtnSz = 28;
static constexpr int kAutoTgW     = 44;
static constexpr int kAutoTgH     = 24;
static constexpr int kPillInnerPad = 12;

static void vpn_ellipsis_fit(cairo_t* cr, std::string* text, float fontSize, int fontWeight,
                             double maxAdvance) {
  if (text->empty()) return;
  auto* layout = pango_cairo_create_layout(cr);
  auto* desc = pango_font_description_from_string("Inter");
  pango_font_description_set_size(desc, static_cast<int>(fontSize * PANGO_SCALE));
  pango_font_description_set_weight(desc, static_cast<PangoWeight>(fontWeight));
  pango_layout_set_font_description(layout, desc);
  auto width_of = [&](const std::string& s) {
    pango_layout_set_text(layout, s.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    return static_cast<double>(w);
  };
  if (width_of(*text) <= maxAdvance) {
    g_object_unref(layout);
    pango_font_description_free(desc);
    return;
  }
  std::string base = *text;
  while (!base.empty()) {
    while (!base.empty() && (static_cast<unsigned char>(base.back()) & 0xC0) == 0x80) base.pop_back();
    if (!base.empty()) base.pop_back();
    std::string trial = base;
    trial += "\u2026";
    if (width_of(trial) <= maxAdvance) {
      *text = std::move(trial);
      break;
    }
  }
  if (base.empty()) *text = "\u2026";
  g_object_unref(layout);
  pango_font_description_free(desc);
}

static double vpn_pill_radius(double h, double w) {
  return std::max(2.0, std::min(h, w) * 0.22);
}

struct VpnEntry {
  VpnConnectionInfo info;
  m3::Button btn;
  m3::Toggle autoTg;
  bool pillHovered = false;
  bool removeHovered = false;
  bool pendingActive = false;
  bool pendingAuto = false;
  std::chrono::steady_clock::time_point pendingSince{};

  VpnEntry(VpnConnectionInfo info_) : info(std::move(info_)) {
    btn.setMinSize(0, 0);
    btn.setStyle(m3::Button::Style::Outlined);
    btn.setSize(m3::Button::Size::XS);
    btn.setLabel(info.active ? "Disconnect" : "Connect");
  }

  void toggle() {
       
    auto& nm = eh::net::NetworkManagerService::instance();
    info.active = !info.active;
    pendingActive = true;
    pendingSince = std::chrono::steady_clock::now();
    btn.setLabel(info.active ? "Disconnect" : "Connect");
    if (info.active) {
      nm.activateVpnConnection(info);
    } else {
      nm.deactivateVpnConnection(info);
      // Disconnecting also clears autoconnect so NetworkManager does not
      // re-dial the VPN. Reflect that immediately in the auto toggle.
      info.autoconnect = false;
      pendingAuto = true;
      pendingSince = std::chrono::steady_clock::now();
    }
    nm.refreshVpnConnections();
  }

  void paint(App& app, cairo_t* cr, int btnX, int btnY, int btnW, int btnH,
             int pillX, int pillY, int pillW, int pillH, double rad,
             double dotCX, int pillMidY,
             float a_r, float a_g, float a_b,
             float s_r, float s_g, float s_b,
             float t_r, float t_g, float t_b,
             float o_r, float o_g, float o_b,
             float pointerX, float pointerY) {
     
    // Button.
    btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                    static_cast<float>(btnW), static_cast<float>(btnH));
    btn.setAccentColor(info.active ? 0.95f : a_r, info.active ? 0.30f : a_g, info.active ? 0.25f : a_b);
    btn.setSurfaceColor(s_r, s_g, s_b);
    btn.setTextColor(t_r, t_g, t_b);
    btn.setOutlineColor(o_r, o_g, o_b);
    btn.setHovered(btn.containsPoint(pointerX, pointerY));

    // Pill background.
    const double pR = s_r * 0.8 + a_r * 0.2;
    const double pG = s_g * 0.8 + a_g * 0.2;
    const double pB = s_b * 0.8 + a_b * 0.2;
    const double pA = 0.35;

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
      box.setColor(r, g, b, static_cast<float>(pA));
      box.setRadius(static_cast<float>(rad));
      box.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                      static_cast<float>(pillW), static_cast<float>(pillH));
      box.paint(cr);
    }
    cairo_round_rect(cr, static_cast<double>(pillX), static_cast<double>(pillY),
                     static_cast<double>(pillW), static_cast<double>(pillH), rad);
    cairo_set_source_rgba(cr, pR, pG, pB, pA);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    // Hover overlay.
    pillHovered = point_in_rect(pointerX, pointerY, pillX, pillY, pillW, pillH);
    if (pillHovered) {
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
        box.setColor(r, g, b, 0.18f);
        box.setRadius(static_cast<float>(rad));
        box.setGeometry(static_cast<float>(pillX), static_cast<float>(pillY),
                        static_cast<float>(pillW), static_cast<float>(pillH));
        box.paint(cr);
      }
      cairo_round_rect(cr, static_cast<double>(pillX), static_cast<double>(pillY),
                       static_cast<double>(pillW), static_cast<double>(pillH), rad);
      cairo_set_source_rgba(cr, a_r, a_g, a_b, 0.35);
      cairo_set_line_width(cr, 1.0);
      cairo_stroke(cr);
    }

    // Status dot.
    if (info.active) {
      cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.85);
    } else {
      cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.35);
    }
    cairo_arc(cr, dotCX, static_cast<double>(pillMidY), 4, 0, 2 * M_PI);
    cairo_fill(cr);

    // VPN name.
    std::string nameText = info.name;
    vpn_ellipsis_fit(cr, &nameText, 13.0f, 400,
                     static_cast<double>(pillX) + static_cast<double>(pillW) - (dotCX + 16.0) -
                         static_cast<double>(kPillInnerPad));
    settings_show_text(cr, dotCX + 16, static_cast<double>(pillMidY + 5), nameText.c_str(), 13, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

    btn.paint(cr);

    // Auto-connect toggle.
    const double auX = static_cast<double>(btnX) - kSpacingM - kAutoTgW;
    const double auY = static_cast<double>(btnY) + (static_cast<double>(btnH) - kAutoTgH) * 0.5;
    autoTg.setSize(m3::Toggle::Size::M);
    autoTg.setGeometry(static_cast<float>(auX), static_cast<float>(auY),
                       static_cast<float>(kAutoTgW), static_cast<float>(kAutoTgH));
    autoTg.setOn(info.autoconnect, 0);
    autoTg.setAccentColor(a_r, a_g, a_b);
    autoTg.setSurfaceColor(s_r, s_g, s_b);
    autoTg.setOutlineColor(o_r, o_g, o_b);
    autoTg.setHovered(autoTg.containsPoint(pointerX, pointerY));
    autoTg.paint(cr, 0);

    int capW = 0, capH = 0;
    measure_btn_text(cr, "auto", 10.f, capW, capH);
    settings_show_text(cr, auX - 6.0 - static_cast<double>(capW),
                       auY + (static_cast<double>(kAutoTgH) - static_cast<double>(capH)) * 0.5 +
                           static_cast<double>(capH) - 2.0,
                       "auto", 10, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.55f);

    // Remove button.
    const double reX = static_cast<double>(btnX) + static_cast<double>(btnW) + static_cast<double>(kSpacingM);
    const double reY = static_cast<double>(btnY) + static_cast<double>(btnH) * 0.5 - static_cast<double>(kRemoveBtnSz) * 0.5;
    const double reSz = static_cast<double>(kRemoveBtnSz);
    removeHovered = point_in_rect(pointerX, pointerY,
                                   static_cast<int>(reX), static_cast<int>(reY),
                                   kRemoveBtnSz, kRemoveBtnSz);
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
      box.setColor(r, g, b, static_cast<float>(removeHovered ? 0.40 : 0.20));
      box.setRadius(7.0f);
      box.setGeometry(static_cast<float>(reX), static_cast<float>(reY),
                      static_cast<float>(reSz), static_cast<float>(reSz));
      box.paint(cr);
    }
    cairo_round_rect(cr, reX, reY, reSz, reSz, 7.0);
    cairo_set_source_rgba(cr, 0.85, 0.25, 0.20, removeHovered ? 0.55 : 0.30);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    material_symbols_draw_glyph(cr, reX + reSz * 0.5, reY + reSz * 0.5, 16.0,
                                "delete", Theme::TextR, Theme::TextG, Theme::TextB,
                                removeHovered ? 0.85 : 0.55);
  }

  bool handleClick(float px, float py) {
     
    if (btn.containsPoint(px, py)) {
      toggle();
      return true;
    }
    if (autoTg.containsPoint(px, py)) {
      auto& nm = eh::net::NetworkManagerService::instance();
      info.autoconnect = !info.autoconnect;
      pendingAuto = true;
      pendingSince = std::chrono::steady_clock::now();
      nm.setVpnAutoconnect(info.uuid, info.autoconnect);
      nm.refreshVpnConnections();
      return true;
    }
    // The remove (delete) button is handled by the caller; do not consume it.
    return false;
  }
};

static std::vector<std::unique_ptr<VpnEntry>> s_entries;
static int s_vpnBtnW = 0;
static int s_vpnBtnH = 0;

void paint_vpn_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
   
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  {
    static auto lastVpnRefresh = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - lastVpnRefresh >= std::chrono::seconds{2}) {
      lastVpnRefresh = now;
      nm.refreshVpnConnections();
    }
  }
  const auto& st = nm.state();

  const int cx = contentX + kCardPad;
  const int cw = contentW - 2 * kCardPad;

  const int hasVpn = static_cast<int>(st.vpnConnections.size());
  const int vpnListH = hasVpn * kVpnRowH;
  const int cardH = 52 + vpnListH + kSpacingS + kVpnRowH + kSpacingL;

  const int cardTop = kContentTop;
  settings_card(app, cr, static_cast<double>(cx), static_cast<double>(cardTop),
                static_cast<double>(cw), static_cast<double>(cardH), glassOv);

  settings_show_text(cr, cx + kCardPad, cardTop + 30, "VPN Connections", 14, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  // Fix the button size from both possible labels at bootstrap.
  if (s_vpnBtnW == 0) {
    int twC = 0, thC = 0, twD = 0, thD = 0;
    measure_btn_text(cr, "Connect", 11.f, twC, thC);
    measure_btn_text(cr, "Disconnect", 11.f, twD, thD);
    int maxTw = std::max(twC, twD);
    s_vpnBtnW = std::max(maxTw + kBtnPad * 2, 64);
    s_vpnBtnH = std::max(thC, thD) + kBtnPad * 2;
  }

  // Reconcile entries by UUID.
  // do_refresh_vpn() returns the list sorted by active state, so matching by
  // index would cross identities between rows whenever a connection goes up
  // or down (toggling the wrong VPN and showing the wrong autoconnect value).
  // Match by UUID instead and keep the optimistic label/toggle until a refresh
  // confirms the new state (or 8s elapse), so clicks feel instant instead of
  // reverting while the async nmcli command is still in flight.
  {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::unique_ptr<VpnEntry>> next;
    next.reserve(static_cast<size_t>(hasVpn));
    for (int vi = 0; vi < hasVpn; ++vi) {
      const auto& snap = st.vpnConnections[static_cast<size_t>(vi)];
      auto it = std::find_if(s_entries.begin(), s_entries.end(),
        [&](const auto& e) { return e->info.uuid == snap.uuid; });
      if (it != s_entries.end()) {
        auto entry = std::move(*it);
        s_entries.erase(it);
        if (entry->pendingActive) {
          if (now - entry->pendingSince >= std::chrono::seconds{8} ||
              snap.active == entry->info.active) {
            entry->pendingActive = false;
          }
        }
        if (!entry->pendingActive) entry->info.active = snap.active;
        if (entry->pendingAuto) {
          if (now - entry->pendingSince >= std::chrono::seconds{8} ||
              snap.autoconnect == entry->info.autoconnect) {
            entry->pendingAuto = false;
          }
        }
        if (!entry->pendingAuto) entry->info.autoconnect = snap.autoconnect;
        entry->btn.setLabel(entry->info.active ? "Disconnect" : "Connect");
        next.push_back(std::move(entry));
      } else {
        next.push_back(std::make_unique<VpnEntry>(snap));
      }
    }
    s_entries = std::move(next);
  }

  const int btnX = cx + cw - kCardPad - s_vpnBtnW - kSpacingM - kRemoveBtnSz;
  for (int vi = 0; vi < hasVpn; ++vi) {
    auto* entry = s_entries[static_cast<size_t>(vi)].get();
    const int vry = cardTop + 52 + vi * kVpnRowH;
    const int btnY = vry + kVpnRowH / 2 - s_vpnBtnH / 2;
    const int pillX = cx + kCardPad;
    const int pillH = kVpnRowH - kSpacingXS;
    const int pillY = vry + kSpacingXS / 2;
    const int pillW = btnX - kSpacingM - kAutoTgW - kSpacingS - pillX;
    const double rad = vpn_pill_radius(static_cast<double>(pillH), static_cast<double>(pillW));
    const int pillMidY = pillY + pillH / 2;
    const double dotCX = static_cast<double>(pillX) + kPillInnerPad + 4;
    entry->paint(app, cr, btnX, btnY, s_vpnBtnW, s_vpnBtnH,
                 pillX, pillY, pillW, pillH, rad,
                 dotCX, pillMidY,
                 a_r, a_g, a_b, s_r, s_g, s_b, t_r, t_g, t_b, o_r, o_g, o_b,
                 app.pointerX, app.pointerY);
  }

  // Add VPN button.
  {
    const auto* addLbl = "+ Add VPN";
    int tw = 0, th = 0;
    measure_btn_text(cr, addLbl, 12.f, tw, th);
    const int addAreaY = cardTop + 52 + vpnListH + kSpacingS;
    const int btnW = std::max(tw + kBtnPad * 2, 64);
    const int btnH = th + kBtnPad * 2;
    const int btnY = addAreaY + (kVpnRowH - btnH) / 2;
    const int btnX = cx + cw - kCardPad - btnW;
    const bool btnHover = point_in_rect(app.pointerX, app.pointerY, btnX, btnY, btnW, btnH);
    m3::Button addBtn;
    addBtn.setMinSize(0, 0);
    addBtn.setLabel(addLbl);
    addBtn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                        static_cast<float>(btnW), static_cast<float>(btnH));
    addBtn.setStyle(m3::Button::Style::Outlined);
    addBtn.setSize(m3::Button::Size::XS);
    addBtn.setAccentColor(a_r, a_g, a_b);
    addBtn.setOutlineColor(o_r, o_g, o_b);
    addBtn.setHovered(btnHover);
    addBtn.paint(cr);
  }
}

bool settings_vpn_consume_pointer_down(App& app, int contentX, int contentW) {
   
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  const auto& st = nm.state();

  const int cx = contentX + kCardPad;
  const int cw = contentW - 2 * kCardPad;

  const int hasVpn = static_cast<int>(st.vpnConnections.size());
  const int vpnListH = hasVpn * kVpnRowH;
  const int cardTop = kContentTop;

  // Add VPN button
  {
    const int addAreaY = cardTop + 52 + vpnListH + kSpacingS;
    const int btnH = 24;
    const int btnY = addAreaY + (kVpnRowH - btnH) / 2;
    const int btnW = 80;
    const int btnX = cx + cw - kCardPad - btnW;
    if (point_in_rect(app.pointerX, app.pointerY, btnX, btnY, btnW, btnH)) {
      vpn_add_dialog_open(app);
      return true;
    }
  }

  // Click on Connect/Disconnect buttons
  if (s_vpnBtnW == 0) {
    s_vpnBtnW = static_cast<int>(std::strlen("Disconnect") * 7 + 8);
    s_vpnBtnH = 24;
  }
  const int btnX = cx + cw - kCardPad - s_vpnBtnW - kSpacingM - kRemoveBtnSz;
  for (int vi = 0; vi < hasVpn; ++vi) {
    if (static_cast<size_t>(vi) >= s_entries.size()) break;
    auto* entry = s_entries[static_cast<size_t>(vi)].get();
    const int vry = cardTop + 52 + vi * kVpnRowH;
    const int btnY = vry + kVpnRowH / 2 - s_vpnBtnH / 2;
    entry->btn.setGeometry(static_cast<float>(btnX), static_cast<float>(btnY),
                           static_cast<float>(s_vpnBtnW), static_cast<float>(s_vpnBtnH));
    if (entry->handleClick(app.pointerX, app.pointerY)) {
      return true;
    }
    const double reX = static_cast<double>(btnX) + static_cast<double>(s_vpnBtnW) + static_cast<double>(kSpacingM);
    const double reY = static_cast<double>(btnY) + static_cast<double>(s_vpnBtnH) * 0.5 - static_cast<double>(kRemoveBtnSz) * 0.5;
    const bool overRemove = point_in_rect(app.pointerX, app.pointerY,
                                          static_cast<int>(reX), static_cast<int>(reY),
                                          kRemoveBtnSz, kRemoveBtnSz);
    if (overRemove) {
      nm.removeVpnConnection(entry->info.uuid);
      nm.refresh();
      return true;
    }
  }

  return false;
}
