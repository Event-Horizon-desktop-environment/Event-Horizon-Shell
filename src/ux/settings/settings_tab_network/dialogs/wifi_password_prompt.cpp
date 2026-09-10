#include <cairo/cairo.h>

#include "m3/core/primitives/box.hpp"
#include "m3/controls/containers/button.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "ux/settings/common/settings_common.hpp"
#include "ux/settings/settings_tab_network/dialogs/wifi_password_prompt.hpp"
#include "ux/settings/settings_tab_network/dialogs/keyring_password_prompt.hpp"
#include "ux/settings/utils/helpers/material_glyphs.hpp"
#include "services/keyring/secret_service_daemon.hpp"
#include "services/network/core/network_manager_service.hpp"

namespace {

bool g_open = false;
std::string g_ssid;
std::uint8_t g_strength = 0;
std::string g_password;
int g_hover_button = 0;
int g_press_button = 0;

double g_hit_cancel[4]{};
double g_hit_confirm[4]{};

constexpr double kCardW = 400.0;
constexpr double kCardH = 245.0;

// Pending SSID to auto-retry after keyring unlock
static std::string g_pendingSsid;
static std::uint8_t g_pendingStrength = 0;

static void retry_pending(App& app) {
   
  if (g_pendingSsid.empty()) return;
  auto ssid = std::move(g_pendingSsid);
  auto strength = g_pendingStrength;
  g_pendingSsid.clear();
  g_pendingStrength = 0;

  try {
    auto& keyring = eh::keyring::SecretServiceDaemon::instance();
    if (keyring.state() != eh::keyring::SecretServiceDaemon::State::Unlocked) return;
    auto storedPw = keyring.lookup_wifi_password(ssid);
    if (storedPw && !storedPw->empty()) {
      auto& nm = eh::net::NetworkManagerService::instance();
      const auto& st = nm.state();
      for (const auto& ap : st.accessPoints) {
        if (ap.ssid == ssid && ap.secured) {
          nm.queue_secret_for_ssid(ssid, *storedPw);
          nm.activateAccessPoint(ap);
          return;
        }
      }
    }
  } catch (const std::exception&) {}

  // No stored password — show the regular WiFi prompt (keyring is now unlocked)
  wifi_password_prompt_open(app, ssid, strength);
}

static void recalc_hit_areas(double W, double H) {
   
  const double cx = (W - kCardW) * 0.5;
  const double cy = (H - kCardH) * 0.5;
  const double pad = 20.0;
  const double btnY = cy + kCardH - pad - 36.0;
  const double btnH = 36.0;
  const double gap = 10.0;
  const double cancelW = 100.0;
  const double confirmW = 130.0;
  const double btnRight = cx + kCardW - pad;
  g_hit_confirm[0] = btnRight - confirmW;
  g_hit_confirm[1] = btnY;
  g_hit_confirm[2] = confirmW;
  g_hit_confirm[3] = btnH;
  g_hit_cancel[0] = g_hit_confirm[0] - gap - cancelW;
  g_hit_cancel[1] = btnY;
  g_hit_cancel[2] = cancelW;
  g_hit_cancel[3] = btnH;
}

static void rr(cairo_t* cr, double rx, double ry, double rw, double rh, double rad) {
   
  constexpr double kPiH = 1.57079632679489661923;
  cairo_new_path(cr);
  const double r = std::min({rad, rw * 0.5, rh * 0.5});
  const double x0 = rx, y0 = ry, x1 = rx + rw, y1 = ry + rh;
  cairo_arc(cr, x1 - r, y0 + r, r, -kPiH, 0);
  cairo_arc(cr, x1 - r, y1 - r, r, 0, kPiH);
  cairo_arc(cr, x0 + r, y1 - r, r, kPiH, 2 * kPiH);
  cairo_arc(cr, x0 + r, y0 + r, r, 2 * kPiH, 3 * kPiH);
  cairo_close_path(cr);
}

static bool point_in_rect_d(double x, double y, const double r[4]) {
  return x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
}

} // anonymous namespace

void wifi_password_prompt_retry_pending(App& app) {
  retry_pending(app);
}

void wifi_password_prompt_open(App& app, const std::string& ssid, std::uint8_t strength) {
   
  // If keyring needs setup/unlock, show that prompt instead
  try {
    auto& keyring = eh::keyring::SecretServiceDaemon::instance();
    auto ks = keyring.state();
    if (ks == eh::keyring::SecretServiceDaemon::State::Uninitialized) {
      g_pendingSsid = ssid;
      g_pendingStrength = strength;
      keyring_prompt_open(app, true);
      return;
    }
    if (ks == eh::keyring::SecretServiceDaemon::State::Locked) {
      g_pendingSsid = ssid;
      g_pendingStrength = strength;
      keyring_prompt_open(app, false);
      return;
    }
    // Keyring is unlocked — look up stored password
    auto storedPw = keyring.lookup_wifi_password(ssid);
    if (storedPw && !storedPw->empty()) {
      auto& nm = eh::net::NetworkManagerService::instance();
      const auto& st = nm.state();
      for (const auto& ap : st.accessPoints) {
        if (ap.ssid == ssid && ap.secured) {
          nm.queue_secret_for_ssid(ssid, *storedPw);
          nm.activateAccessPoint(ap);
          return;
        }
      }
    }
  } catch (const std::exception& e) {
    (void)e;
  }

  g_open = true;
  g_ssid = ssid;
  g_strength = strength;
  g_password.clear();
  g_hover_button = 0;
  g_press_button = 0;
  (void)app;
}

void wifi_password_prompt_close(App& app) {
   
  g_open = false;
  g_ssid.clear();
  g_password.clear();
  g_hover_button = 0;
  g_press_button = 0;
  (void)app;
}

bool wifi_password_prompt_visible(const App& app) {
  (void)app;
  return g_open;
}

void wifi_password_prompt_paint(App& app, cairo_t* cr) {
   
  if (!g_open) return;

  const double W = static_cast<double>(app.width);
  const double H = static_cast<double>(app.height);
  const double cx = (W - kCardW) * 0.5;
  const double cy = (H - kCardH) * 0.5;
  recalc_hit_areas(W, H);

  const double pad = 20.0;

  // Dimmed overlay
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.42);
  cairo_rectangle(cr, 0, 0, W, H);
  cairo_fill(cr);

  // Card shadow
  {
    m3::Box box;
    box.setColor(0, 0, 0, 0.2f);
    box.setRadius(18.0f);
    box.setGeometry(static_cast<float>(cx + 2), static_cast<float>(cy + 3),
                    static_cast<float>(kCardW), static_cast<float>(kCardH));
    box.paint(cr);
  }

  // Card background
  {
    m3::Box box;
    if (app.drawChromeMatugen)
      box.setColor(static_cast<float>(app.drawChrome.panelFillR), static_cast<float>(app.drawChrome.panelFillG),
                   static_cast<float>(app.drawChrome.panelFillB), 0.96f);
    else
      box.setColor(static_cast<float>(Theme::BgR), static_cast<float>(Theme::BgG),
                   static_cast<float>(Theme::BgB), 0.96f);
    box.setRadius(16.0f);
    box.setGeometry(static_cast<float>(cx), static_cast<float>(cy),
                    static_cast<float>(kCardW), static_cast<float>(kCardH));
    box.paint(cr);
  }
  rr(cr, cx, cy, kCardW, kCardH, 16.0);
  paint_src_glass_hi(app, cr, 0.10);
  cairo_set_line_width(cr, 1.0);
  cairo_stroke(cr);

  // Lock icon
  material_symbols_draw_glyph(cr, cx + pad + 11.0, cy + pad + 14.0, 22.0,
                              "lock", Theme::AccR, Theme::AccG, Theme::AccB, 1.0);

  // Title
  settings_show_text(cr, cx + pad + 40.0, cy + pad + 22.0, "Connect to Wi-Fi", 17.0, 700, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  // Close button
  const double closeSz = 28.0;
  double hit_close[4] = {cx + kCardW - pad - closeSz, cy + pad - 4.0, closeSz, closeSz};
  const int cHov = (g_hover_button == 3) ? 1 : 0;
  const int cPrs = (g_press_button == 3) ? 1 : 0;
  {
    m3::Box box;
    if (app.drawChromeMatugen)
      box.setColor(static_cast<float>(app.drawChrome.drawerDimR), static_cast<float>(app.drawChrome.drawerDimG),
                   static_cast<float>(app.drawChrome.drawerDimB), 0.5f);
    else
      box.setColor(static_cast<float>(Theme::BgR * 0.85), static_cast<float>(Theme::BgG * 0.9),
                   static_cast<float>(Theme::BgB * 0.95), 0.5f);
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(hit_close[0]), static_cast<float>(hit_close[1]),
                    static_cast<float>(hit_close[2]), static_cast<float>(hit_close[3]));
    box.paint(cr);
  }
  if (cPrs) {
    m3::Box box;
    box.setColor(0, 0, 0, 0.3f);
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(hit_close[0]), static_cast<float>(hit_close[1]),
                    static_cast<float>(hit_close[2]), static_cast<float>(hit_close[3]));
    box.paint(cr);
  } else if (cHov) {
    m3::Box box;
    box.setColor(1, 1, 1, 0.2f);
    box.setRadius(8.0f);
    box.setGeometry(static_cast<float>(hit_close[0]), static_cast<float>(hit_close[1]),
                    static_cast<float>(hit_close[2]), static_cast<float>(hit_close[3]));
    box.paint(cr);
  }
  const double cGlyphA = cPrs ? 0.6 : (cHov ? 1.0 : 0.8);
  material_symbols_draw_glyph(cr, hit_close[0] + closeSz * 0.5, hit_close[1] + closeSz * 0.5,
                              18.0, "close", Theme::TextR, Theme::TextG, Theme::TextB, cGlyphA);

  // SSID display
  std::string ssid_display = "Network: " + g_ssid;
  settings_show_text(cr, cx + pad, cy + pad + 52.0, ssid_display.c_str(), 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  // Password hint label
  settings_show_text(cr, cx + pad, cy + pad + 74.0, "Enter password", 11.5, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  // Password input field
  const double fieldY = cy + pad + 88.0;
  const double fieldH = 36.0;
  const double fieldW = kCardW - 2.0 * pad;
  {
    m3::Box box;
    if (app.drawChromeMatugen)
      box.setColor(static_cast<float>(app.drawChrome.panelFillR), static_cast<float>(app.drawChrome.panelFillG),
                   static_cast<float>(app.drawChrome.panelFillB), 0.75f);
    else
      box.setColor(static_cast<float>(Theme::BgR), static_cast<float>(Theme::BgG),
                   static_cast<float>(Theme::BgB), 0.75f);
    box.setRadius(10.0f);
    box.setGeometry(static_cast<float>(cx + pad), static_cast<float>(fieldY),
                    static_cast<float>(fieldW), static_cast<float>(fieldH));
    box.paint(cr);
  }
  rr(cr, cx + pad, fieldY, fieldW, fieldH, 10.0);
  cairo_set_source_rgba(cr, Theme::AccR, Theme::AccG, Theme::AccB, 1.0);
  cairo_set_line_width(cr, 1.2);
  cairo_stroke(cr);

  // Password text (show as bullets)
  std::string shown;
  shown.assign(g_password.size(), '*');
  if (shown.size() > 80) shown.resize(77), shown += "...";
  settings_show_text(cr, cx + pad + 8.0, fieldY + 24.0, shown.empty() ? " " : shown.c_str(), 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);

  // Blinking cursor when field is focused
  if (g_password.empty()) {
    settings_show_text(cr, cx + pad + 8.0, fieldY + 24.0, "|", 13.0, 400, Theme::TextR, Theme::TextG, Theme::TextB, 1.0);
  }

  // Buttons — hit areas set by recalc_hit_areas() above
  const bool hasPw = !g_password.empty();

  // Cancel button
  {
    const bool hover = g_hover_button == 1;
    const bool press = g_press_button == 1;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button cancelBtn;
    cancelBtn.setMinSize(0, 0);
    cancelBtn.setLabel("Cancel");
    cancelBtn.setGeometry(static_cast<float>(g_hit_cancel[0]), static_cast<float>(g_hit_cancel[1]),
                          static_cast<float>(g_hit_cancel[2]), static_cast<float>(g_hit_cancel[3]));
    cancelBtn.setStyle(m3::Button::Style::Outlined);
    cancelBtn.setSize(m3::Button::Size::XS);
    cancelBtn.setAccentColor(a_r, a_g, a_b);
    cancelBtn.setOutlineColor(o_r, o_g, o_b);
    cancelBtn.setHovered(hover);
    cancelBtn.setPressed(press);
    cancelBtn.paint(cr);
  }

  // Confirm button
  {
    const bool hover = g_hover_button == 2;
    const bool press = g_press_button == 2;
    float a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b;
    settings_resolve_colors(app, a_r, a_g, a_b, t_r, t_g, t_b, s_r, s_g, s_b, o_r, o_g, o_b);
    m3::Button confirmBtn;
    confirmBtn.setMinSize(0, 0);
    confirmBtn.setLabel("Confirm");
    confirmBtn.setGeometry(static_cast<float>(g_hit_confirm[0]), static_cast<float>(g_hit_confirm[1]),
                           static_cast<float>(g_hit_confirm[2]), static_cast<float>(g_hit_confirm[3]));
    confirmBtn.setStyle(m3::Button::Style::Outlined);
    confirmBtn.setSize(m3::Button::Size::XS);
    confirmBtn.setAccentColor(a_r, a_g, a_b);
    confirmBtn.setOutlineColor(o_r, o_g, o_b);
    confirmBtn.setHovered(hover);
    confirmBtn.setPressed(press);
    confirmBtn.setEnabled(hasPw);
    confirmBtn.paint(cr);
  }
}

bool wifi_password_prompt_consume_pointer_down(App& app) {
   
  if (!g_open) return false;

  recalc_hit_areas(static_cast<double>(app.width), static_cast<double>(app.height));

  if (point_in_rect_d(app.pointerX, app.pointerY, g_hit_cancel)) {
    g_press_button = 1;
    return true;
  }
  if (point_in_rect_d(app.pointerX, app.pointerY, g_hit_confirm)) {
    g_press_button = 2;
    return true;
  }
  // Close button hit
  {
    const double cx = (static_cast<double>(app.width) - kCardW) * 0.5;
    const double cy = (static_cast<double>(app.height) - kCardH) * 0.5;
    const double pad = 20.0;
    const double closeSz = 28.0;
    double hit_close[4] = {cx + kCardW - pad - closeSz, cy + pad - 4.0, closeSz, closeSz};
    if (point_in_rect_d(app.pointerX, app.pointerY, hit_close)) {
      g_press_button = 3;
      return true;
    }
  }

  // Click inside card → consume (prevents clicks passing through to UI behind)
  const double cx = (static_cast<double>(app.width) - kCardW) * 0.5;
  const double cy = (static_cast<double>(app.height) - kCardH) * 0.5;
  if (app.pointerX >= cx && app.pointerX < cx + kCardW &&
      app.pointerY >= cy && app.pointerY < cy + kCardH) {
    return true;
  }

  // Click outside card → close
  wifi_password_prompt_close(app);
  return true;
}

void wifi_password_prompt_consume_pointer_up(App& app) {
   
  if (!g_open) return;
  recalc_hit_areas(static_cast<double>(app.width), static_cast<double>(app.height));
  const int btn = g_press_button;
  g_press_button = 0;

  if (btn == 1 && point_in_rect_d(app.pointerX, app.pointerY, g_hit_cancel)) {
    wifi_password_prompt_close(app);
    return;
  }
  if (btn == 2 && point_in_rect_d(app.pointerX, app.pointerY, g_hit_confirm)) {
    auto& nm = eh::net::NetworkManagerService::instance();
    // Find the matching AP from current state
    const auto& st = nm.state();
    for (const auto& ap : st.accessPoints) {
      if (ap.ssid == g_ssid) {
        nm.activateAccessPointPsk(ap, g_password);
        break;
      }
    }
    wifi_password_prompt_close(app);
    return;
  }
  if (btn == 3) {
    // Close button hit on press; confirm still over it on release
    const double cx = (static_cast<double>(app.width) - kCardW) * 0.5;
    const double cy = (static_cast<double>(app.height) - kCardH) * 0.5;
    const double pad = 20.0;
    const double closeSz = 28.0;
    double hit_close[4] = {cx + kCardW - pad - closeSz, cy + pad - 4.0, closeSz, closeSz};
    if (point_in_rect_d(app.pointerX, app.pointerY, hit_close)) {
      wifi_password_prompt_close(app);
    }
  }
}

void wifi_password_prompt_consume_pointer_motion(App& app) {
   
  if (!g_open) return;
  recalc_hit_areas(static_cast<double>(app.width), static_cast<double>(app.height));
  int old = g_hover_button;
  g_hover_button = 0;
  if (point_in_rect_d(app.pointerX, app.pointerY, g_hit_cancel)) g_hover_button = 1;
  else if (point_in_rect_d(app.pointerX, app.pointerY, g_hit_confirm)) g_hover_button = 2;
  else {
    const double cx = (static_cast<double>(app.width) - kCardW) * 0.5;
    const double cy = (static_cast<double>(app.height) - kCardH) * 0.5;
    const double pad = 20.0;
    const double closeSz = 28.0;
    double hit_close[4] = {cx + kCardW - pad - closeSz, cy + pad - 4.0, closeSz, closeSz};
    if (point_in_rect_d(app.pointerX, app.pointerY, hit_close)) g_hover_button = 3;
  }
  if (old != g_hover_button) {
    // Trigger redraw via pending flag
  }
}

void wifi_password_prompt_handle_key(App& app, uint32_t sym, uint32_t state,
                                     const char* utf8, int utf8_len) {
   
  if (!g_open) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED && state != WL_KEYBOARD_KEY_STATE_REPEATED) return;

  if (sym == XKB_KEY_Escape) {
    wifi_password_prompt_close(app);
    return;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    if (!g_password.empty()) {
      auto& nm = eh::net::NetworkManagerService::instance();
      const auto& st = nm.state();
      for (const auto& ap : st.accessPoints) {
        if (ap.ssid == g_ssid) {
          nm.activateAccessPointPsk(ap, g_password);
          break;
        }
      }
    }
    wifi_password_prompt_close(app);
    return;
  }
  if (sym == XKB_KEY_BackSpace) {
    if (!g_password.empty()) g_password.pop_back();
    return;
  }
  if (utf8_len > 0) {
    g_password.append(utf8, static_cast<size_t>(utf8_len));
  }
}
