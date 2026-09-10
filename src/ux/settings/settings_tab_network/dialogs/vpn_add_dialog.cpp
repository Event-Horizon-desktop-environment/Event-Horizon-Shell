#include <cairo/cairo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"
#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_network/dialogs/vpn_add_dialog.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"
#include "dialog/file_chooser_dialog.hpp"

namespace {

// Field identifiers — shared value store.
enum FieldId {
  kFName = 0,
  kFPrivKey,
  kFServerPub,
  kFEndpoint,
  kFAllowedIPs,
  kFClientIP,
  kFDns,
  kFRemote,
  kFPort,
  kFUser,
  kFPass,
  kFGateway,
  kFHost,
  kFAddress,
  kFIpsecPsk,
  kFieldCount,
};

struct VpnFieldSpec {
  FieldId id;
  const char* label;
  const char* placeholder;
  bool secret;       // masked display; goes to vpn.secrets for plugin types
  const char* dataKey; // vpn.data key for plugin types; nullptr for WireGuard
};

struct VpnTypeInfo {
  const char* label;
  const char* desc;
  const char* serviceType;  // nullptr → WireGuard (native nmcli path)
  const char* pkg;          // nullptr → no plugin package needed
  bool hasImport;
  const char* importLabel;
  const char* importExt;
  const VpnFieldSpec* fields;
  int fieldCount;
};

// Per-type field forms.
static constexpr VpnFieldSpec kWireGuardFields[] = {
  {kFName,       "Connection Name",    "My WireGuard VPN", false, nullptr},
  {kFPrivKey,    "Private Key",        "Enter private key", true, nullptr},
  {kFServerPub,  "Server Public Key",  "Enter server public key", false, nullptr},
  {kFEndpoint,   "Endpoint",           "host:port", false, nullptr},
  {kFAllowedIPs, "Allowed IPs",        "0.0.0.0/0, ::/0", false, nullptr},
  {kFClientIP,   "Client IP (optional)", "10.0.0.2/32", false, nullptr},
  {kFDns,        "DNS (optional)",     "1.1.1.1", false, nullptr},
};
static constexpr int kWireGuardFieldCount = 7;

static constexpr VpnFieldSpec kOpenVpnFields[] = {
  {kFName,   "Connection Name", "My OpenVPN", false, nullptr},
  {kFRemote, "Server",          "vpn.example.com", false, "remote"},
  {kFPort,   "Port",            "1194", false, "port"},
  {kFUser,   "Username",        "user", false, "username"},
  {kFPass,   "Password",        "password", true, "password"},
};
static constexpr int kOpenVpnFieldCount = 5;

static constexpr VpnFieldSpec kStrongSwanFields[] = {
  {kFName,    "Connection Name", "My IKEv2", false, nullptr},
  {kFAddress, "Server Address",  "vpn.example.com", false, "address"},
  {kFUser,    "Username",        "user", false, "username"},
  {kFPass,    "Password",        "password", true, "password"},
};
static constexpr int kStrongSwanFieldCount = 4;

static constexpr VpnFieldSpec kL2tpFields[] = {
  {kFName,      "Connection Name", "My L2TP", false, nullptr},
  {kFGateway,   "Gateway",         "vpn.example.com", false, "gateway"},
  {kFUser,      "Username",        "user", false, "user"},
  {kFPass,      "Password",        "password", true, "password"},
  {kFIpsecPsk,  "IPsec Pre-Shared Key (optional)", "shared secret", true, "ipsec-psk"},
};
static constexpr int kL2tpFieldCount = 5;

static constexpr VpnFieldSpec kPptpFields[] = {
  {kFName,    "Connection Name", "My PPTP", false, nullptr},
  {kFGateway, "Gateway",         "vpn.example.com", false, "gateway"},
  {kFUser,    "Username",        "user", false, "user"},
  {kFPass,    "Password",        "password", true, "password"},
};
static constexpr int kPptpFieldCount = 4;

static constexpr VpnFieldSpec kAnyConnectFields[] = {
  {kFName,   "Connection Name", "My AnyConnect", false, nullptr},
  {kFHost,   "VPN Host",        "vpn.example.com", false, "vpnhost"},
  {kFUser,   "Username",        "user", false, "user"},
  {kFPass,   "Password",        "password", true, "password"},
};
static constexpr int kAnyConnectFieldCount = 4;

static const VpnTypeInfo kVpnTypes[] = {
  {"WireGuard", "Secure, fast point-to-point tunnel", nullptr, nullptr, true, "Import .conf file", ".conf",
   kWireGuardFields, kWireGuardFieldCount},
  {"OpenVPN", "Open-source VPN protocol", "org.freedesktop.NetworkManager.openvpn", "networkmanager-openvpn",
   true, "Import .ovpn file", ".ovpn", kOpenVpnFields, kOpenVpnFieldCount},
  {"IKEv2", "IPsec-based VPN (strongSwan)", "org.freedesktop.NetworkManager.strongswan", "networkmanager-strongswan",
   false, nullptr, ".conf", kStrongSwanFields, kStrongSwanFieldCount},
  {"L2TP/IPsec", "L2TP tunneled over IPsec", "org.freedesktop.NetworkManager.l2tp", "networkmanager-l2tp",
   false, nullptr, ".conf", kL2tpFields, kL2tpFieldCount},
  {"PPTP", "Legacy point-to-point tunneling", "org.freedesktop.NetworkManager.pptp", "networkmanager-pptp",
   false, nullptr, ".conf", kPptpFields, kPptpFieldCount},
  {"AnyConnect", "Cisco AnyConnect client", "org.freedesktop.NetworkManager.anyconnect", "networkmanager-anyconnect",
   false, nullptr, ".conf", kAnyConnectFields, kAnyConnectFieldCount},
};
static constexpr int kVpnTypeCount = 6;

// Dialog geometry.
// Width and height are computed at open/type-switch time (g_dlgW/g_dlgH) so
// the tabs + form always fit and the panel hugs the active form.
static constexpr double kPad = 20.0;
static constexpr double kFieldH = 34.0;
static constexpr double kFieldLabelH = 13.0;
static constexpr double kFieldGap = 7.0;
static constexpr double kFieldRowGap = 12.0;
static constexpr double kTabH = 32.0;
static constexpr double kTabGap = 8.0;
static constexpr double kTabMinW = 88.0;
static constexpr double kBtnH = 34.0;
static constexpr double kMinBtnW = 90.0;
static constexpr double kTitleBarH = 68.0;
static constexpr double kFooterH = 20.0;
static constexpr double kBottomChromeH = 108.0;
static constexpr double kGenBtnW = 88.0;
static constexpr double kGenGap = 14.0;
static constexpr double kFieldMinW = 460.0;

// State.
bool g_open = false;
int g_vpnType = 0; // index into kVpnTypes
int g_hoverBtn = 0; // 0=none, 1=Add, 2=Cancel, 3=Generate, 4=Import, 5=Close, 10+ = tab hit
int g_pressBtn = 0;
int g_focusField = 0; // 0=none, else FieldId+1

bool g_dragging = false;
double g_dragOffX = 0;
double g_dragOffY = 0;

double g_dlgX = 0;
double g_dlgY = 0;
double g_dlgW = 0;
double g_dlgH = 0;
double g_lastWinW = 0;
double g_lastWinH = 0;

std::array<double, kVpnTypeCount> g_tabX{};
std::array<double, kVpnTypeCount> g_tabW{};

std::array<std::string, kFieldCount> g_value;

std::string g_statusMsg;
double g_statusMsgTimer = 0;

double g_hitAdd[4]{};
std::array<double, 4> g_hitCancel{};
double g_hitGenerate[4]{};
double g_hitImport[4]{};

const VpnTypeInfo& active_type() { return kVpnTypes[g_vpnType]; }

// Helpers.

void rr(cairo_t* cr, double x, double y, double w, double h, double r) {
  const double rad = std::min({r, w * 0.5, h * 0.5});
  cairo_new_path(cr);
  cairo_arc(cr, x + w - rad, y + rad, rad, -M_PI_2, 0);
  cairo_arc(cr, x + w - rad, y + h - rad, rad, 0, M_PI_2);
  cairo_arc(cr, x + rad, y + h - rad, rad, M_PI_2, M_PI);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI_2);
  cairo_close_path(cr);
}

bool pt_in_rect(double px, double py, const double r[4]) {
  return px >= r[0] && px < r[0] + r[2] && py >= r[1] && py < r[1] + r[3];
}

// Frosted glassy surface colour (matches the widget picker cards).
static void vpn_card_src(const App& app, float* r, float* g, float* b) {
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

// Accent-tinted glassy icon tile, same style as the widget picker header.
static void vpn_icon_box(App& app, cairo_t* cr, double x, double y, double sz, const char* ligature) {
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
  const float ir = std::min(1.0f, a_r * 0.45f + 0.42f);
  const float ig = std::min(1.0f, a_g * 0.45f + 0.42f);
  const float ib = std::min(1.0f, a_b * 0.45f + 0.42f);
  m3::Box ibox;
  ibox.setColor(ir, ig, ib, 0.85f);
  ibox.setRadius(10.0f);
  ibox.setGeometry(static_cast<float>(x), static_cast<float>(y),
                   static_cast<float>(sz), static_cast<float>(sz));
  ibox.setGlassy(true);
  ibox.paint(cr);
  material_symbols_draw_glyph(cr, x + sz * 0.5, y + sz * 0.5 + 0.5, sz * 0.6,
                              ligature, t_r, t_g, t_b, 0.95);
}

static double vpn_text_width_px(const char* text, float fontSize, int weight) {
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
  int pw, ph;
  pango_layout_get_pixel_size(layout, &pw, &ph);
  pango_font_description_free(desc);
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  return static_cast<double>(pw);
}

// Does this machine have the NetworkManager plugin for a service type installed?
// (plugin .so names per the NM VPN plugin ABI)
static bool vpn_plugin_available(const VpnTypeInfo& info) {
  if (info.serviceType == nullptr) return true; // WireGuard is native
  const char* soName = nullptr;
  if (std::string(info.serviceType).find("openvpn") != std::string::npos) soName = "libnm-vpn-plugin-openvpn.so";
  else if (std::string(info.serviceType).find("strongswan") != std::string::npos) soName = "libnm-vpn-plugin-strongswan.so";
  else if (std::string(info.serviceType).find("l2tp") != std::string::npos) soName = "libnm-vpn-plugin-l2tp.so";
  else if (std::string(info.serviceType).find("pptp") != std::string::npos) soName = "libnm-vpn-plugin-pptp.so";
  else if (std::string(info.serviceType).find("anyconnect") != std::string::npos) soName = "libnm-vpn-plugin-anyconnect.so";
  if (soName == nullptr) return false;
  const std::string path = std::string("/usr/lib/NetworkManager/VPN/") + soName;
  FILE* f = fopen(path.c_str(), "rb");
  if (f) { fclose(f); return true; }
  return false;
}

void draw_field(App& app, cairo_t* cr, double fx, double fy, double fw, double fh,
                const std::string& rawValue, bool focused, bool secret, const char* placeholder) {
  {
    float fr, fg, fb;
    vpn_card_src(app, &fr, &fg, &fb);
    m3::Box box;
    box.setColor(fr, fg, fb, 1.0f);
    box.setRadius(12.0f);
    box.setGeometry(static_cast<float>(fx), static_cast<float>(fy),
                    static_cast<float>(fw), static_cast<float>(fh));
    box.setGlassy(true);
    box.paint(cr);
  }
  rr(cr, fx, fy, fw, fh, 12.0);
  if (focused) {
    cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 0.8);
    cairo_set_line_width(cr, 1.5);
  } else {
    cairo_set_source_rgba(cr, Theme::TextR, Theme::TextG, Theme::TextB, 0.15);
    cairo_set_line_width(cr, 1.0);
  }
  cairo_stroke(cr);

  std::string display;
  if (secret) {
    const size_t n = std::min<size_t>(rawValue.size(), 16);
    for (size_t i = 0; i < n; ++i) display += "\u2022";
  } else {
    display = rawValue;
  }

  if (rawValue.empty() && !focused) {
    settings_show_text(cr, fx + 8.0, fy + fh * 0.5 + 5.0, placeholder, 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.35);
  } else {
    std::string shown = display;
    if (shown.size() > 50) shown.resize(47), shown += "...";
    settings_show_text(cr, fx + 8.0, fy + fh * 0.5 + 5.0, shown.c_str(), 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.90);
    if (focused) {
      cairo_text_extents_t te;
      cairo_text_extents(cr, shown.c_str(), &te);
      settings_show_text(cr, fx + 8.0 + te.x_advance, fy + fh * 0.5 + 5.0, "|", 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.70);
    }
  }
}

void draw_label(cairo_t* cr, double x, double y, const char* text) {
  settings_show_text(cr, x, y, text, 11.5, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.60);
}

double field_row_y(double fieldTop, int row) {
  return fieldTop + static_cast<double>(row) * (kFieldLabelH + kFieldGap + kFieldH + kFieldRowGap);
}

// Calculate geometry.
void recalc_geom(App& app) {
  const double W = static_cast<double>(app.width);
  const double H = static_cast<double>(app.height);

  // Width is static (tab labels never change) — measured once.
  if (g_dlgW == 0) {
    double tabsW = 0.0;
    for (int i = 0; i < kVpnTypeCount; ++i) {
      const double tw = vpn_text_width_px(kVpnTypes[i].label, 11.0, 500);
      g_tabW[static_cast<size_t>(i)] = std::max(kTabMinW, tw + 28.0);
      tabsW += g_tabW[static_cast<size_t>(i)];
    }
    tabsW += static_cast<double>(kVpnTypeCount - 1) * kTabGap;
    g_dlgW = std::max(kFieldMinW + 2.0 * kPad, tabsW + 2.0 * kPad);
  }

  // Dynamic height: hugs the active form's field count.
  const auto& info = active_type();
  const double fieldTopOff = kTitleBarH + kTabH + kPad;
  const double fieldsEnd =
      fieldTopOff + static_cast<double>(info.fieldCount - 1) * (kFieldLabelH + kFieldGap + kFieldH + kFieldRowGap) +
      kFieldGap + kFieldH;
  const double newH = fieldsEnd + kBottomChromeH;
  const bool heightChanged = std::abs(g_dlgH - newH) > 0.5;
  if (heightChanged) g_dlgH = newH;

  // Re-center when the window size changes (or on first layout / form height
  // change), so the dialog stays centered like the widget picker. Skipped while
  // dragging so a resize can't yank the dialog out of the pointer's grasp.
  const bool sizeChanged = (W != g_lastWinW || H != g_lastWinH);
  if (sizeChanged) {
    g_lastWinW = W;
    g_lastWinH = H;
  }
  const bool firstLayout = (g_dlgX == 0 && g_dlgY == 0);
  if (firstLayout || (sizeChanged && !g_dragging) || heightChanged) {
    // Center within the settings content area (past the sidebar), matching the
    // widget picker.
    const double insetX = static_cast<double>(kSpacingL) + static_cast<double>(kSidebarW) + static_cast<double>(kSpacingL);
    const double contentW = W - insetX - 16.0;
    g_dlgX = insetX + std::max(0.0, contentW - g_dlgW) * 0.5;
    g_dlgY = (H - g_dlgH) * 0.5;
  }

  const double cx = g_dlgX;
  const double cy = g_dlgY;

  const double btnAreaTop = cy + g_dlgH - kFooterH - 16.0 - kBtnH;
  const double btnGap = 10.0;
  const double cancelW = 100.0;
  const double addW = 120.0;
  g_hitAdd[0] = cx + g_dlgW - kPad - addW;
  g_hitAdd[1] = btnAreaTop;
  g_hitAdd[2] = addW;
  g_hitAdd[3] = kBtnH;
  g_hitCancel[0] = g_hitAdd[0] - btnGap - cancelW;
  g_hitCancel[1] = btnAreaTop;
  g_hitCancel[2] = cancelW;
  g_hitCancel[3] = kBtnH;

  // Tab row (gaps between each tab)
  {
    double tx = cx + kPad;
    for (int i = 0; i < kVpnTypeCount; ++i) {
      g_tabX[static_cast<size_t>(i)] = tx;
      tx += g_tabW[static_cast<size_t>(i)] + kTabGap;
    }
  }

  const double fieldTop = cy + fieldTopOff;
  const double fieldW = g_dlgW - 2.0 * kPad;

  // Generate button (right of the WireGuard private key field)
  if (g_vpnType == 0) {
    const int privKeyRow = 1;
    const double privKeyFieldY = field_row_y(fieldTop, privKeyRow) + kFieldGap;
    g_hitGenerate[0] = cx + kPad + fieldW - kGenBtnW;
    g_hitGenerate[1] = privKeyFieldY;
    g_hitGenerate[2] = kGenBtnW;
    g_hitGenerate[3] = kFieldH;
  }

  // Import button (bottom bar, left side)
  if (info.hasImport) {
    const double importBtnW = 170.0;
    g_hitImport[0] = cx + kPad;
    g_hitImport[1] = btnAreaTop;
    g_hitImport[2] = importBtnW;
    g_hitImport[3] = kBtnH;
  }
}

// X25519 keypair generation (no external tools).
// Field arithmetic over p = 2^255 - 19 using 16 little-endian 16-bit limbs.
// All values fit comfortably in int64 (radix-16 "TweetNaCl" style).

static std::string b64_encode(const std::string& in) {
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  while (i + 3 <= in.size()) {
    uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += tbl[v & 63];
    i += 3;
  }
  if (i + 1 == in.size()) {
    uint32_t v = static_cast<uint8_t>(in[i]) << 16;
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == in.size()) {
    uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    out += tbl[(v >> 18) & 63];
    out += tbl[(v >> 12) & 63];
    out += tbl[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

static void fe_copy(std::array<long long, 16>& r, const std::array<long long, 16>& a) {
  for (int i = 0; i < 16; ++i) r[i] = a[i];
}

static void fe_add(std::array<long long, 16>& r, const std::array<long long, 16>& a,
                   const std::array<long long, 16>& b) {
  for (int i = 0; i < 16; ++i) r[i] = a[i] + b[i];
}

static void fe_sub(std::array<long long, 16>& r, const std::array<long long, 16>& a,
                   const std::array<long long, 16>& b) {
  for (int i = 0; i < 16; ++i) r[i] = a[i] - b[i];
}

// Canonicalize a 16-limb value: carry within limbs 0..14, then fold limb 15's
// high bits as scalars into limb 0 (2^256 mod p = 38, 2^255 mod p = 19).
static void fe_carry_modp(std::array<long long, 16>& t) {
  for (int pass = 0; pass < 6; ++pass) {
    for (int i = 0; i < 15; ++i) { t[i + 1] += t[i] >> 16; t[i] &= 0xFFFF; }
    const long long q16 = t[15] >> 16;
    const long long q15 = (t[15] >> 15) & 1;
    t[15] &= 0x7FFF;
    if (q16 != 0 || q15 != 0) {
      t[0] += 19 * q15 + 38 * q16;
    } else {
      break;
    }
  }
}

static void fe_mul(std::array<long long, 16>& r, const std::array<long long, 16>& a,
                   const std::array<long long, 16>& b) {
  std::array<long long, 31> t{};
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j)
      t[i + j] += a[i] * b[j];
  // Fold limbs 16..30 by 38 (2^256 mod p = 38).
  for (int i = 16; i < 31; ++i) t[i - 16] += 38 * t[i];
  for (int i = 0; i < 16; ++i) r[i] = t[i];
  fe_carry_modp(r);
}

static void fe_sq(std::array<long long, 16>& r, const std::array<long long, 16>& a) {
  fe_mul(r, a, a);
}

static void fe_invert(std::array<long long, 16>& r, const std::array<long long, 16>& a) {
  // r = a^(2^255 - 21) = a^(p-2). Exponent (little-endian): 0xEB, 0xFF * 30, 0x7F.
  static constexpr int kExp[32] = {
    0xEB, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F,
  };
  std::array<long long, 16> base{};
  fe_copy(base, a);
  std::array<long long, 16> res{};
  res[0] = 1;
  for (int bit = 0; bit < 255; ++bit) {
    if (((kExp[bit >> 3] >> (bit & 7)) & 1) != 0) fe_mul(res, res, base);
    fe_sq(base, base);
  }
  fe_copy(r, res);
}

static void fe_frombytes(std::array<long long, 16>& r, const uint8_t* s) {
  for (int i = 0; i < 16; ++i)
    r[i] = static_cast<long long>(s[2 * i]) | (static_cast<long long>(s[2 * i + 1]) << 8);
}

static void fe_tobytes(uint8_t* s, const std::array<long long, 16>& h) {
  std::array<long long, 16> t{};
  fe_copy(t, h);
  fe_carry_modp(t);
  // Conditional subtraction of p (limbs: 0xFFED, 0xFFFF*14, 0x7FFF).
  bool ge = true;
  for (int i = 15; i >= 0 && ge; --i) {
    const long long pi = (i == 15) ? 0x7FFF : (i == 0 ? 0xFFED : 0xFFFF);
    if (t[i] > pi) break;
    if (t[i] < pi) ge = false;
  }
  if (ge) {
    for (int i = 0; i < 16; ++i) {
      const long long pi = (i == 15) ? 0x7FFF : (i == 0 ? 0xFFED : 0xFFFF);
      t[i] -= pi;
    }
    for (int i = 0; i < 15; ++i) {
      if (t[i] < 0) { t[i] += 0x10000; t[i + 1] -= 1; }
    }
  }
  for (int i = 0; i < 16; ++i) {
    s[2 * i] = static_cast<uint8_t>(t[i] & 0xFF);
    s[2 * i + 1] = static_cast<uint8_t>((t[i] >> 8) & 0xFF);
  }
}

static void fe_mul_small(std::array<long long, 16>& r, const std::array<long long, 16>& a,
                         long long s) {
  for (int i = 0; i < 16; ++i) r[i] = a[i] * s;
}

static void x25519_scalar_base(uint8_t out[32], const uint8_t scalar[32]) {
  // Montgomery u-coordinate ladder; constant base point u = 9.
  constexpr long long kA24 = 121665;
  uint8_t k[32];
  std::memcpy(k, scalar, 32);
  k[0] &= 248;
  k[31] &= 127;
  k[31] |= 64;
  std::array<long long, 16> x1{}, x2{}, z2{}, x3{}, z3{};
  uint8_t base[32]{};
  base[0] = 9;
  fe_frombytes(x1, base);
  x2[0] = 1;
  z2[0] = 0;
  fe_copy(x3, x1);
  z3[0] = 1;
  bool swap = false;
  std::array<long long, 16> A, B, AA, BB, E, C, D, DA, CB, sm;
  for (int t = 254; t >= 0; --t) {
    const bool kt = ((k[t >> 3] >> (t & 7)) & 1) != 0;
    swap = swap != kt;
    if (swap) { std::swap(x2, x3); std::swap(z2, z3); }
    swap = kt;

    fe_add(A, x2, z2);
    fe_sq(AA, A);
    fe_sub(B, x2, z2);
    fe_sq(BB, B);
    fe_sub(E, AA, BB);
    fe_add(C, x3, z3);
    fe_sub(D, x3, z3);
    fe_mul(DA, D, A);
    fe_mul(CB, C, B);

    fe_add(sm, DA, CB);
    fe_sq(x3, sm);
    fe_sub(sm, DA, CB);
    fe_sq(sm, sm);
    fe_mul(z3, x1, sm);

    fe_mul(x2, AA, BB);
    fe_mul_small(sm, E, kA24);
    fe_add(sm, sm, AA);
    fe_mul(z2, E, sm);
  }
  if (swap) { std::swap(x2, x3); std::swap(z2, z3); }

  std::array<long long, 16> zi, outFe;
  fe_invert(zi, z2);
  fe_mul(outFe, x2, zi);
  fe_tobytes(out, outFe);
}

bool vpn_gen_keypair(std::string& privB64, std::string& pubB64) {
  uint8_t priv[32]{};
  FILE* f = fopen("/dev/urandom", "rb");
  if (!f) return false;
  const size_t got = fread(priv, 1, 32, f);
  fclose(f);
  if (got != 32) return false;
  priv[0] &= 248;
  priv[31] &= 127;
  priv[31] |= 64;
  uint8_t pub[32];
  x25519_scalar_base(pub, priv);
  privB64 = b64_encode(std::string(reinterpret_cast<const char*>(priv), 32));
  pubB64 = b64_encode(std::string(reinterpret_cast<const char*>(pub), 32));
  return true;
}

} // anonymous namespace

// Public API.

void vpn_add_dialog_open(App& app) {
  g_open = true;
  g_vpnType = 0;
  g_hoverBtn = 0;
  g_pressBtn = 0;
  g_focusField = 0;
  g_dragging = false;
  g_value = {};
  g_value[kFAllowedIPs] = "0.0.0.0/0, ::/0";
  g_statusMsg.clear();
  g_statusMsgTimer = 0;
  g_dlgX = 0;
  g_dlgY = 0;
  (void)app;
}

void vpn_add_dialog_close(App& app) {
  g_open = false;
  (void)app;
}

bool vpn_add_dialog_visible(const App& app) {
  (void)app;
  return g_open;
}

// Paint.

void vpn_add_dialog_paint(App& app, cairo_t* cr) {
  if (!g_open) return;

  recalc_geom(app);

  const double W = static_cast<double>(app.width);
  const double H = static_cast<double>(app.height);
  const double cx = g_dlgX;
  const double cy = g_dlgY;
  const auto& info = active_type();

  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.42);
  cairo_rectangle(cr, 0, 0, W, H);
  cairo_fill(cr);

  // Shadow
  {
    m3::Box box;
    box.setColor(0, 0, 0, 0.25f);
    box.setRadius(18.0f);
    box.setGeometry(static_cast<float>(cx + 2), static_cast<float>(cy + 3),
                    static_cast<float>(g_dlgW), static_cast<float>(g_dlgH));
    box.paint(cr);
  }

  // Card background
  {
    float bgR, bgG, bgB;
    if (app.drawChromeMatugen) {
      bgR = static_cast<float>(app.drawChrome.panelFillR);
      bgG = static_cast<float>(app.drawChrome.panelFillG);
      bgB = static_cast<float>(app.drawChrome.panelFillB);
    } else {
      bgR = static_cast<float>(Theme::BgR);
      bgG = static_cast<float>(Theme::BgG);
      bgB = static_cast<float>(Theme::BgB);
    }
    m3::Box box;
    box.setColor(bgR, bgG, bgB, 0.96f);
    box.setRadius(16.0f);
    box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                    static_cast<float>(g_dlgW), static_cast<float>(g_dlgH));
    box.setGlassy(true);
    box.paint(cr);
  }
  rr(cr, cx, cy, g_dlgW, g_dlgH, 16.0);
  paint_src_glass_hi(app, cr, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Header (draggable).
  float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
  settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);

  vpn_icon_box(app, cr, cx + kPad, cy + kPad + 3.0, 38.0, "vpn_key");
  settings_show_text(cr, cx + kPad + 38.0 + 12.0, cy + kPad + 23.0, "Add VPN Connection", 16, 700, t_r, t_g, t_b, 0.95);
  settings_show_text(cr, cx + kPad + 38.0 + 12.0, cy + kPad + 40.0, info.desc, 11, 400, t_r, t_g, t_b, 0.55);

  const double closeSz = 28.0;
  const double closeX = cx + g_dlgW - kPad - closeSz;
  const double closeY = cy + kPad - 6.0;
  const bool closeHov = (g_hoverBtn == 5);
  {
    m3::Button closeBtn;
    closeBtn.setMinSize(0, 0);
    closeBtn.setGlyph("close");
    closeBtn.setGeometry(static_cast<float>(closeX), static_cast<float>(closeY),
                         static_cast<float>(closeSz), static_cast<float>(closeSz));
    closeBtn.setStyle(m3::Button::Style::Outlined);
    closeBtn.setSize(m3::Button::Size::XS);
    closeBtn.setAccentColor(a_r, a_g, a_b);
    closeBtn.setOutlineColor(o_r, o_g, o_b);
    closeBtn.setHovered(closeHov);
    closeBtn.setPressed(g_pressBtn == 5);
    closeBtn.paint(cr);
  }

  // VPN type tabs.
  const double tabTop = cy + kTitleBarH + 4.0;
  for (int i = 0; i < kVpnTypeCount; ++i) {
    const double tx = g_tabX[static_cast<size_t>(i)];
    const double tabW = g_tabW[static_cast<size_t>(i)];
    const bool sel = (i == g_vpnType);
    const bool hov = (g_hoverBtn == 10 + i);
    if (sel || hov) {
      float ar, ag, ab;
      if (app.drawChromeMatugen) {
        ar = static_cast<float>(app.drawChrome.accentR);
        ag = static_cast<float>(app.drawChrome.accentG);
        ab = static_cast<float>(app.drawChrome.accentB);
      } else {
        ar = static_cast<float>(Theme::AccR);
        ag = static_cast<float>(Theme::AccG);
        ab = static_cast<float>(Theme::AccB);
      }
      m3::Box box;
      box.setColor(ar, ag, ab, sel ? 0.22f : 0.10f);
      box.setRadius(8.0f);
      box.setGeometry(static_cast<float>(tx), static_cast<float>(tabTop),
                      static_cast<float>(tabW), static_cast<float>(kTabH));
      box.setGlassy(true);
      box.paint(cr);
    }
    const double tw = vpn_text_width_px(kVpnTypes[i].label, sel ? 11.0 : 10.5, sel ? 700 : 400);
    settings_show_text(cr, tx + (tabW - tw) * 0.5, tabTop + 20.0, kVpnTypes[i].label, sel ? 11.0 : 10.5, sel ? 700 : 400, Theme::TextR, Theme::TextG, Theme::TextB, sel ? 0.95 : 0.50);
  }

  // Plugin availability hint (non-WireGuard types).
  if (g_vpnType != 0 && info.pkg != nullptr && !vpn_plugin_available(info)) {
    const double hy = cy + kTitleBarH + kTabH + kPad - 16.0;
    std::string hint = std::string("Install \"") + info.pkg + "\" to enable " + info.label + " VPN support.";
    settings_show_text(cr, cx + kPad, hy, hint.c_str(), 10.5, 400, Theme::TextR, Theme::TextG, Theme::TextB, 0.45);
  }

  // Form fields.
  const double fieldTop = cy + kTitleBarH + kTabH + kPad;
  const double fieldW = g_dlgW - 2.0 * kPad;
  const int focusId = (g_focusField > 0) ? (g_focusField - 1) : -1;

  for (int i = 0; i < info.fieldCount; ++i) {
    const auto& fd = info.fields[i];
    const double fy = field_row_y(fieldTop, i);
    const bool isPrivKey = (g_vpnType == 0 && fd.id == kFPrivKey);
    double fw = fieldW;
    double fx = cx + kPad;
    if (isPrivKey) {
      fw = fieldW - kGenBtnW - kGenGap;
    }
    draw_label(cr, fx, fy, fd.label);
    draw_field(app, cr, fx, fy + kFieldGap, fw, kFieldH,
               g_value[fd.id], focusId == static_cast<int>(fd.id), fd.secret, fd.placeholder);

    if (isPrivKey) {
      const double genBtnX = cx + kPad + fieldW - kGenBtnW;
      const double genBtnY = fy + kFieldGap;
      const bool genHov = (g_hoverBtn == 3);
      const bool genPrs = (g_pressBtn == 3);
      float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
      settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
      m3::Button genBtn;
      genBtn.setMinSize(0, 0);
      genBtn.setLabel("Generate");
      genBtn.setGeometry(static_cast<float>(genBtnX), static_cast<float>(genBtnY),
                         static_cast<float>(kGenBtnW), static_cast<float>(kFieldH));
      genBtn.setStyle(m3::Button::Style::Outlined);
      genBtn.setSize(m3::Button::Size::XS);
      genBtn.setAccentColor(a_r, a_g, a_b);
      genBtn.setOutlineColor(o_r, o_g, o_b);
      genBtn.setHovered(genHov);
      genBtn.setPressed(genPrs);
      genBtn.paint(cr);
    }
  }

  // Status message.
  if (!g_statusMsg.empty() && g_statusMsgTimer > 0) {
    settings_show_text(cr, cx + kPad, cy + g_dlgH - kFooterH - 16.0 - kBtnH - 22.0, g_statusMsg.c_str(), 11.0, 400, Theme::AccR, Theme::AccG, Theme::AccB, 0.9);
  }

  // Bottom bar.

  if (info.hasImport) {
    const bool importHov = (g_hoverBtn == 4);
    const bool importPrs = (g_pressBtn == 4);
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button importBtn;
    importBtn.setMinSize(0, 0);
    importBtn.setGlyph("file_open");
    importBtn.setLabel(info.importLabel);
    importBtn.setGeometry(static_cast<float>(g_hitImport[0]), static_cast<float>(g_hitImport[1]),
                          static_cast<float>(g_hitImport[2]), static_cast<float>(g_hitImport[3]));
    importBtn.setStyle(m3::Button::Style::Outlined);
    importBtn.setSize(m3::Button::Size::XS);
    importBtn.setAccentColor(a_r, a_g, a_b);
    importBtn.setOutlineColor(o_r, o_g, o_b);
    importBtn.setHovered(importHov);
    importBtn.setPressed(importPrs);
    importBtn.paint(cr);
  }

  {
    const bool hov = (g_hoverBtn == 2);
    const bool prs = (g_pressBtn == 2);
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button cancelBtn;
    cancelBtn.setMinSize(0, 0);
    cancelBtn.setLabel("Cancel");
    cancelBtn.setGeometry(static_cast<float>(g_hitCancel[0]), static_cast<float>(g_hitCancel[1]),
                          static_cast<float>(g_hitCancel[2]), static_cast<float>(g_hitCancel[3]));
    cancelBtn.setStyle(m3::Button::Style::Outlined);
    cancelBtn.setSize(m3::Button::Size::XS);
    cancelBtn.setAccentColor(a_r, a_g, a_b);
    cancelBtn.setOutlineColor(o_r, o_g, o_b);
    cancelBtn.setHovered(hov);
    cancelBtn.setPressed(prs);
    cancelBtn.paint(cr);
  }

  {
    const bool hov = (g_hoverBtn == 1);
    const bool prs = (g_pressBtn == 1);
    const bool hasName = !g_value[kFName].empty();
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button addBtn;
    addBtn.setMinSize(0, 0);
    addBtn.setLabel("Add");
    addBtn.setGeometry(static_cast<float>(g_hitAdd[0]), static_cast<float>(g_hitAdd[1]),
                       static_cast<float>(g_hitAdd[2]), static_cast<float>(g_hitAdd[3]));
    addBtn.setStyle(m3::Button::Style::Filled);
    addBtn.setSize(m3::Button::Size::XS);
    addBtn.setAccentColor(a_r, a_g, a_b);
    addBtn.setHovered(hov);
    addBtn.setPressed(prs);
    addBtn.setEnabled(hasName);
    addBtn.paint(cr);
  }

  // Footer hint.
  {
    const char* hint = "Esc or click outside to close";
    cairo_text_extents_t te;
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11.0);
    cairo_text_extents(cr, hint, &te);
    settings_show_text(cr, cx + (g_dlgW - te.x_advance) * 0.5, cy + g_dlgH - kFooterH + 2.0,
                       hint, 11, 400, t_r, t_g, t_b, 0.45);
  }
}

// Build + submit helpers.

static void show_error(const std::string& msg) {
  g_statusMsg = msg;
  if (g_statusMsg.size() > 140) g_statusMsg.resize(140), g_statusMsg += "...";
  g_statusMsgTimer = 5000;
}

static bool submit_add(App& app) {
  auto& nm = eh::net::NetworkManagerService::instance();
  std::string err;

  if (g_vpnType == 0) {
    if (g_value[kFName].empty()) { show_error("Enter a connection name."); return false; }
    if (g_value[kFPrivKey].empty()) { show_error("Enter or generate a private key."); return false; }
    if (g_value[kFServerPub].empty()) { show_error("Enter the server public key."); return false; }
    if (g_value[kFEndpoint].empty()) { show_error("Enter the endpoint (host:port)."); return false; }
    if (g_value[kFAllowedIPs].empty()) { show_error("Enter the allowed IPs."); return false; }

    WireGuardConfig cfg;
    cfg.connectionName = g_value[kFName];
    cfg.privateKey = g_value[kFPrivKey];
    cfg.serverPublicKey = g_value[kFServerPub];
    cfg.endpoint = g_value[kFEndpoint];
    cfg.allowedIPs = g_value[kFAllowedIPs];
    cfg.clientIP = g_value[kFClientIP];
    cfg.dns = g_value[kFDns];
    if (!nm.addWireguardConnection(cfg, err)) {
      show_error(err.empty() ? "Failed to add WireGuard connection." : err);
      return false;
    }
  } else {
    const auto& info = active_type();
    if (g_value[kFName].empty()) { show_error("Enter a connection name."); return false; }
    if (info.fields != nullptr) {
      for (int i = 0; i < info.fieldCount; ++i) {
        const auto& fd = info.fields[i];
        if (fd.id == kFName || fd.id == kFPort || fd.id == kFPass || fd.id == kFUser ||
            fd.id == kFIpsecPsk) continue;
        if (g_value[fd.id].empty()) {
          show_error(std::string("Enter the ") + fd.label + ".");
          return false;
        }
      }
    }

    VpnAddParams p;
    p.name = g_value[kFName];
    p.serviceType = info.serviceType;
    for (int i = 0; i < info.fieldCount; ++i) {
      const auto& fd = info.fields[i];
      if (fd.id == kFName || fd.dataKey == nullptr) continue;
      if (g_value[fd.id].empty()) continue;
      if (fd.secret) p.secrets.emplace_back(fd.dataKey, g_value[fd.id]);
      else p.data.emplace_back(fd.dataKey, g_value[fd.id]);
    }
    if (g_vpnType == 3 && !g_value[kFIpsecPsk].empty()) { // L2TP with IPsec
      bool hasIpsec = false;
      for (const auto& kv : p.data) if (kv.first == "ipsec-enabled") hasIpsec = true;
      if (!hasIpsec) p.data.emplace_back("ipsec-enabled", "yes");
    }
    if (g_vpnType == 1 && g_value[kFPort].empty()) { // OpenVPN default port
      p.data.emplace_back("port", "1194");
    }
    if (!nm.addVpnConnection(p, err)) {
      if (err.find("unknown VPN plugin") != std::string::npos && info.pkg != nullptr) {
        show_error(std::string("VPN plugin not installed. Install the \"") + info.pkg + "\" package, then retry.");
      } else {
        show_error(err.empty() ? "Failed to add VPN connection." : err);
      }
      return false;
    }
  }

  vpn_add_dialog_close(app);
  nm.refresh();
  return true;
}

static void submit_import(App& app) {
  auto& nm = eh::net::NetworkManagerService::instance();
  const auto& info = active_type();

  std::string filePath;
  {
    using namespace eh::dialog;
    FileChooserDialog dlg(FileChooserDialog::Mode::Open,
                           info.importLabel ? info.importLabel : "Import VPN file", "", false);
    auto res = dlg.run();
    if (res.response == 0 && !res.uris.empty()) {
      filePath = file_uri_to_local_path(res.uris[0]);
    }
  }
  if (filePath.empty()) return;

  // Friendly extension sanity check.
  if (info.importExt != nullptr) {
    const auto pos = filePath.find_last_of('.');
    std::string ext = (pos == std::string::npos) ? "" : filePath.substr(pos);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != info.importExt) {
      show_error("Expected a " + std::string(info.importExt) + " file.");
      return;
    }
  }

  const char* nmType = (g_vpnType == 0) ? "wireguard" : "openvpn";
  std::string err;
  if (!nm.importVpnFile(nmType, filePath, err)) {
    if (err.find("unknown VPN plugin") != std::string::npos && info.pkg != nullptr) {
      show_error(std::string("VPN plugin not installed. Install the \"") + info.pkg + "\" package, then retry.");
    } else {
      show_error(err.empty() ? "Failed to import VPN file." : err);
    }
    return;
  }
  vpn_add_dialog_close(app);
  nm.refresh();
}

// Pointer down.

bool vpn_add_dialog_consume_pointer_down(App& app) {
  if (!g_open) return false;
  recalc_geom(app);

  const double px = app.pointerX;
  const double py = app.pointerY;

  if (px < g_dlgX || px >= g_dlgX + g_dlgW || py < g_dlgY || py >= g_dlgY + g_dlgH) {
    vpn_add_dialog_close(app);
    return true;
  }

  {
    const double closeSz = 28.0;
    const double closeX = g_dlgX + g_dlgW - kPad - closeSz;
    const double closeY = g_dlgY + kPad - 6.0;
    if (px >= closeX && px < closeX + closeSz && py >= closeY && py < closeY + closeSz) {
      g_pressBtn = 5;
      return true;
    }
  }

  if (py >= g_dlgY && py < g_dlgY + kTitleBarH) {
    g_dragging = true;
    g_dragOffX = px - g_dlgX;
    g_dragOffY = py - g_dlgY;
    g_pressBtn = 0;
    return true;
  }

  {
    const double tabTop = g_dlgY + kTitleBarH + 4.0;
    for (int i = 0; i < kVpnTypeCount; ++i) {
      const double tx = g_tabX[static_cast<size_t>(i)];
      const double tabW = g_tabW[static_cast<size_t>(i)];
      if (px >= tx && px < tx + tabW && py >= tabTop && py < tabTop + kTabH) {
        g_vpnType = i;
        g_pressBtn = 0;
        g_focusField = 0;
        g_statusMsg.clear();
        g_statusMsgTimer = 0;
        return true;
      }
    }
  }

  // Field focus
  {
    const auto& info = active_type();
    const double fieldTop = g_dlgY + kTitleBarH + kTabH + kPad;
    for (int i = 0; i < info.fieldCount; ++i) {
      const auto& fd = info.fields[i];
      const double fy = field_row_y(fieldTop, i);
      double fw = g_dlgW - 2.0 * kPad;
      if (g_vpnType == 0 && fd.id == kFPrivKey) fw -= kGenBtnW + kGenGap;
      if (px >= g_dlgX + kPad && px < g_dlgX + kPad + fw &&
          py >= fy + kFieldGap && py < fy + kFieldGap + kFieldH) {
        g_focusField = static_cast<int>(fd.id) + 1;
        g_pressBtn = 0;
        return true;
      }
    }
  }

  if (g_vpnType == 0 && g_hitGenerate[0] > 0 && pt_in_rect(px, py, g_hitGenerate)) {
    g_pressBtn = 3;
    return true;
  }

  if (active_type().hasImport && g_hitImport[0] > 0 && pt_in_rect(px, py, g_hitImport)) {
    g_pressBtn = 4;
    return true;
  }

  g_focusField = 0;

  if (pt_in_rect(px, py, g_hitCancel.data())) {
    g_pressBtn = 2;
    return true;
  }

  if (pt_in_rect(px, py, g_hitAdd)) {
    g_pressBtn = 1;
    return true;
  }

  return true;
}

// Pointer up.

void vpn_add_dialog_consume_pointer_up(App& app) {
  if (!g_open) return;
  recalc_geom(app);

  g_dragging = false;

  const int btn = g_pressBtn;
  g_pressBtn = 0;

  if (btn == 1 && pt_in_rect(app.pointerX, app.pointerY, g_hitAdd)) {
    submit_add(app);
    return;
  }

  if (btn == 2 && pt_in_rect(app.pointerX, app.pointerY, g_hitCancel.data())) {
    vpn_add_dialog_close(app);
    return;
  }

  if (btn == 3 && pt_in_rect(app.pointerX, app.pointerY, g_hitGenerate)) {
    std::string priv, pub;
    if (vpn_gen_keypair(priv, pub)) {
      g_value[kFPrivKey] = priv;
      g_statusMsg = "Key generated. Your public key (send to the server): " + pub;
      g_statusMsgTimer = 8000;
    } else {
      show_error("Failed to generate key pair (no /dev/urandom?).");
    }
    return;
  }

  if (btn == 4 && active_type().hasImport && pt_in_rect(app.pointerX, app.pointerY, g_hitImport)) {
    submit_import(app);
    return;
  }

  if (btn == 5) {
    const double closeSz = 28.0;
    const double closeX = g_dlgX + g_dlgW - kPad - closeSz;
    const double closeY = g_dlgY + kPad - 6.0;
    if (app.pointerX >= closeX && app.pointerX < closeX + closeSz &&
        app.pointerY >= closeY && app.pointerY < closeY + closeSz) {
      vpn_add_dialog_close(app);
    }
    return;
  }
}

// Pointer motion.

void vpn_add_dialog_consume_pointer_motion(App& app) {
  if (!g_open) return;
  recalc_geom(app);

  if (g_dragging && g_pressBtn == 0) {
    g_dlgX = app.pointerX - g_dragOffX;
    g_dlgY = app.pointerY - g_dragOffY;
    g_dlgX = std::max(20.0, std::min(g_dlgX, static_cast<double>(app.width) - g_dlgW - 20.0));
    g_dlgY = std::max(20.0, std::min(g_dlgY, static_cast<double>(app.height) - g_dlgH - 20.0));
    return;
  }

  int oldHover = g_hoverBtn;
  g_hoverBtn = 0;

  if (pt_in_rect(app.pointerX, app.pointerY, g_hitAdd)) g_hoverBtn = 1;
  else if (pt_in_rect(app.pointerX, app.pointerY, g_hitCancel.data())) g_hoverBtn = 2;
  else if (g_vpnType == 0 && pt_in_rect(app.pointerX, app.pointerY, g_hitGenerate)) g_hoverBtn = 3;
  else if (active_type().hasImport && pt_in_rect(app.pointerX, app.pointerY, g_hitImport)) g_hoverBtn = 4;
  else {
    const double closeSz = 28.0;
    const double closeX = g_dlgX + g_dlgW - kPad - closeSz;
    const double closeY = g_dlgY + kPad - 6.0;
    if (app.pointerX >= closeX && app.pointerX < closeX + closeSz &&
        app.pointerY >= closeY && app.pointerY < closeY + closeSz)
      g_hoverBtn = 5;
  }

  if (app.pointerY >= g_dlgY + kTitleBarH + 4.0 &&
      app.pointerY < g_dlgY + kTitleBarH + 4.0 + kTabH) {
    for (int i = 0; i < kVpnTypeCount; ++i) {
      double tx = g_tabX[static_cast<size_t>(i)];
      double tabW = g_tabW[static_cast<size_t>(i)];
      if (app.pointerX >= tx && app.pointerX < tx + tabW) {
        g_hoverBtn = 10 + i;
        break;
      }
    }
  }

  if (oldHover != g_hoverBtn) {
    // redraw triggered by paint loop
  }
}

// Keyboard handling.

void vpn_add_dialog_handle_key(App& app, uint32_t sym, uint32_t state,
                               const char* utf8, int utf8_len) {
  if (!g_open) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  if (sym == XKB_KEY_Escape) {
    vpn_add_dialog_close(app);
    return;
  }

  if (sym == XKB_KEY_Tab) {
    const auto& info = active_type();
    std::vector<FieldId> order;
    order.reserve(static_cast<size_t>(info.fieldCount));
    for (int i = 0; i < info.fieldCount; ++i) order.push_back(info.fields[i].id);
    if (order.empty()) return;
    if (g_focusField == 0) {
      g_focusField = static_cast<int>(order[0]) + 1;
    } else {
      int idx = 0;
      for (size_t i = 0; i < order.size(); ++i) {
        if (static_cast<int>(order[i]) + 1 == g_focusField) { idx = static_cast<int>(i); break; }
      }
      g_focusField = static_cast<int>(order[(idx + 1) % order.size()]) + 1;
    }
    return;
  }

  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (!g_value[kFName].empty()) submit_add(app);
    return;
  }

  if (g_focusField <= 0) return;

  std::string* target = nullptr;
  const FieldId fid = static_cast<FieldId>(g_focusField - 1);
  if (fid >= kFName && fid < kFieldCount) target = &g_value[fid];
  if (!target) return;

  if (sym == XKB_KEY_BackSpace) {
    if (!target->empty()) target->pop_back();
    return;
  }

  if (utf8_len > 0) {
    target->append(utf8, static_cast<size_t>(utf8_len));
  }
}
