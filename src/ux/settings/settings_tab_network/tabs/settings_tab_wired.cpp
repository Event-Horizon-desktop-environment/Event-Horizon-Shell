#include <cairo/cairo.h>

#include <string>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_network/settings_tab_network.hpp"
#include "ux/settings/settings_tab_network/tabs/settings_tab_wired.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

extern void draw(App& app);

// M3 sub-tab includes
#include "ux/settings/settings_tab_network/m3/wired_status_m3.hpp"
#include "ux/settings/settings_tab_network/m3/wired_ipv4_m3.hpp"
#include "ux/settings/settings_tab_network/m3/wired_ipv6_m3.hpp"
#include "ux/settings/settings_tab_network/m3/wired_ethernet_m3.hpp"
#include "ux/settings/settings_tab_network/m3/wired_security_m3.hpp"
#include "ux/settings/settings_tab_network/m3/wired_advanced_m3.hpp"

const char* kWiredChildLabels[6] = {
  "Status", "IPv4", "IPv6", "Ethernet", "Security", "Advanced"
};

void paint_wired_child_tab_bar(cairo_t* cr, int contentX, int contentW,
                                float textR, float textG, float textB,
                                int activeTab) {
  constexpr int kTabW = 130;
  constexpr int kChildTabGap = 4;
  const int barX = contentX + 8;
  const int barY = kContentTop;
  const int barW = contentW - 16;
  constexpr int kChildTabH = 34;

  m3::Box bg;
  bg.setColor(textR, textG, textB, 0.04f);
  bg.setRadius(8.0f);
  bg.setGeometry(static_cast<float>(barX), static_cast<float>(barY),
                 static_cast<float>(barW), static_cast<float>(kChildTabH + 6));
  bg.paint(cr);

  const int tabY = barY + 3;
  const int tabH = kChildTabH;
  const int tabsW = kWiredChildCount * kTabW + (kWiredChildCount - 1) * kChildTabGap;
  const int startX = barX + 4 + (tabsW < barW - 8 ? (barW - 8 - tabsW) / 2 : 0);

  for (int i = 0; i < kWiredChildCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
    const bool sel = (i == activeTab);

    if (sel) {
      m3::Box selBg;
      selBg.setColor(textR, textG, textB, 0.10f);
      selBg.setRadius(6.0f);
      selBg.setGeometry(static_cast<float>(tx), static_cast<float>(tabY),
                        static_cast<float>(kTabW), static_cast<float>(tabH));
      selBg.paint(cr);
    }

    m3::Label lbl;
    lbl.setText(kWiredChildLabels[i]);
    lbl.setFontSize(13.0f);
    lbl.setFontWeight(sel ? 600 : 400);
    lbl.setColor(textR, textG, textB, sel ? 0.90f : 0.55f);
    float lw, lh;
    lbl.measureExtents(lw, lh);
    lbl.paintAt(cr, static_cast<float>(tx) + (static_cast<float>(kTabW) - lw) * 0.5f,
                static_cast<float>(tabY) + (static_cast<float>(tabH) - lh) * 0.5f);
  }
}

int wired_hit_child_tab(float px, float py, int contentX, int contentW) {
  constexpr int kTabW = 130;
  constexpr int kChildTabGap = 4;
  const int barX = contentX + 8;
  const int barY = kContentTop + 3;
  const int tabH = 34;
  const int barW = contentW - 16;

  const int tabsW = kWiredChildCount * kTabW + (kWiredChildCount - 1) * kChildTabGap;
  const int startX = barX + 4 + (tabsW < barW - 8 ? (barW - 8 - tabsW) / 2 : 0);

  for (int i = 0; i < kWiredChildCount; ++i) {
    const int tx = startX + i * (kTabW + kChildTabGap);
    if (px >= tx && px < tx + kTabW && py >= barY && py < barY + tabH)
      return i;
  }
  return -1;
}

void paint_wired_tab(App& app, cairo_t* cr, int contentX, int contentW, double glassOv) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  paint_wired_child_tab_bar(cr, contentX, contentW, t_r, t_g, t_b, app.wiredChildTab);
  const int ct = kNetworkWiredTop;

  switch (app.wiredChildTab) {
    case kWiredChildStatus:
      m3::detail::wiredStatusM3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
    case kWiredChildIPv4:
      m3::detail::wiredIpv4M3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
    case kWiredChildIPv6:
      m3::detail::wiredIpv6M3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
    case kWiredChildEthernet:
      m3::detail::wiredEthernetM3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
    case kWiredChildSecurity:
      m3::detail::wiredSecurityM3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
    case kWiredChildAdvanced:
      m3::detail::wiredAdvancedM3().paint(app, cr, contentX, contentW, glassOv, ct);
      break;
  }
}

bool settings_wired_consume_pointer_down(App& app, int contentX, int contentW) {
  float px = static_cast<float>(app.pointerX);
  float py = static_cast<float>(app.pointerY);
  int tabHit = wired_hit_child_tab(px, py, contentX, contentW);
  if (tabHit >= 0 && tabHit != app.wiredChildTab) {
    app.wiredChildTab = tabHit;
    draw(app);
    return true;
  }

  switch (app.wiredChildTab) {
    case kWiredChildStatus:
      return m3::detail::wiredStatusM3().handlePointerDown(app, px, py);
    case kWiredChildIPv4:
      return m3::detail::wiredIpv4M3().handlePointerDown(app, px, py);
    case kWiredChildIPv6:
      return m3::detail::wiredIpv6M3().handlePointerDown(app, px, py);
    case kWiredChildEthernet:
      return m3::detail::wiredEthernetM3().handlePointerDown(app, px, py);
    case kWiredChildSecurity:
      return m3::detail::wiredSecurityM3().handlePointerDown(app, px, py);
    case kWiredChildAdvanced:
      return m3::detail::wiredAdvancedM3().handlePointerDown(app, px, py);
    default:
      return false;
  }
}

bool settings_wired_consume_pointer_up(App& app, int contentX, int contentW) {
  (void)contentX; (void)contentW;
  float px = static_cast<float>(app.pointerX);
  float py = static_cast<float>(app.pointerY);

  switch (app.wiredChildTab) {
    case kWiredChildStatus:
      return m3::detail::wiredStatusM3().handlePointerUp(app, px, py);
    case kWiredChildIPv4:
      return m3::detail::wiredIpv4M3().handlePointerUp(app, px, py);
    case kWiredChildIPv6:
      return m3::detail::wiredIpv6M3().handlePointerUp(app, px, py);
    case kWiredChildEthernet:
      return m3::detail::wiredEthernetM3().handlePointerUp(app, px, py);
    case kWiredChildSecurity:
      return m3::detail::wiredSecurityM3().handlePointerUp(app, px, py);
    case kWiredChildAdvanced:
      return m3::detail::wiredAdvancedM3().handlePointerUp(app, px, py);
    default:
      return false;
  }
}

void settings_wired_handle_pointer_leave(App& app) {
  switch (app.wiredChildTab) {
    case kWiredChildStatus:
      m3::detail::wiredStatusM3().handlePointerLeave(); break;
    case kWiredChildIPv4:
      m3::detail::wiredIpv4M3().handlePointerLeave(); break;
    case kWiredChildIPv6:
      m3::detail::wiredIpv6M3().handlePointerLeave(); break;
    case kWiredChildEthernet:
      m3::detail::wiredEthernetM3().handlePointerLeave(); break;
    case kWiredChildSecurity:
      m3::detail::wiredSecurityM3().handlePointerLeave(); break;
    case kWiredChildAdvanced:
      m3::detail::wiredAdvancedM3().handlePointerLeave(); break;
  }
}
