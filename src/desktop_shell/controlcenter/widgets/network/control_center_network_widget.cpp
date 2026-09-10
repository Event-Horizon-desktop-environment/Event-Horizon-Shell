#include "desktop_shell/controlcenter/widgets/network/control_center_network_widget.hpp"
#include "desktop_shell/controlcenter/paint/control_center_paint_utils.hpp"

#include "desktop_shell/common/glyph/material_glyph.hpp"
#include "services/network/core/network_manager_service.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <cairo/cairo.h>

namespace {

std::string trim_copy(const std::string& s) {
  const auto* p = s.data();
  const auto* pe = p + s.size();
  while (p != pe && std::isspace(static_cast<unsigned char>(*p))) ++p;
  while (pe != p && std::isspace(static_cast<unsigned char>(*(pe - 1)))) --pe;
  return std::string(p, pe);
}

} // anonymous namespace

namespace eh::shell::dock::control_center {

using paint_utils::rrect;
using paint_utils::cc_paint_glass_card_mc;
using paint_utils::cc_draw_status_pill;
using paint_utils::kSurfR;
using paint_utils::kSurfG;
using paint_utils::kSurfB;

ControlCenterNetworkState control_center_network_state() {
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();

  const eh::net::Snapshot s = nm.snapshot();
  ControlCenterNetworkState out{};
  out.connected = s.connected;
  out.wifi = (s.kind == eh::net::ConnectivityKind::Wireless);
  out.ethernet = (s.kind == eh::net::ConnectivityKind::Wired);
  out.iface = s.iface.empty() ? std::string("network") : s.iface;
  out.ssid = s.ssid;
  out.wifi_signal_pct = s.signal_pct;

  if (!s.connected) {
    if (!s.wireless_enabled) out.status_text = "Wi-Fi Off";
    else out.status_text = "Disconnected";
    return out;
  }

  if (out.wifi && out.wifi_signal_pct >= 0) {
    out.status_text = "Connected \xe2\x80\xa2 " + std::to_string(std::clamp(out.wifi_signal_pct, 0, 100)) + "%";
  } else {
    out.status_text = "Connected";
  }
  return out;
}

std::vector<ControlCenterWifiAp> control_center_wifi_scan(bool rescan) {
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();
  if (rescan) nm.request_scan();

  const eh::net::Snapshot s = nm.snapshot();
  std::vector<ControlCenterWifiAp> out;
  out.reserve(s.aps.size());

  auto saved = [&](const std::string& ssid) -> bool {
    return std::find(s.saved_ssids.begin(), s.saved_ssids.end(), ssid) != s.saved_ssids.end();
  };

  for (const auto& ap : s.aps) {
    ControlCenterWifiAp row{};
    row.active = ap.active;
    row.ssid = ap.ssid;
    row.signal_pct = ap.strength_pct;
    row.security = ap.secured ? "secured" : "open";
    row.needs_password = ap.secured && !saved(ap.ssid);
    if (!row.ssid.empty()) out.push_back(std::move(row));
  }
  return out;
}

bool control_center_wifi_connect(const std::string& ssid, const std::string& password, std::string* out_error) {
  if (out_error) out_error->clear();
  const std::string want = trim_copy(ssid);
  if (want.empty()) return false;

  auto& nm = eh::net::NetworkManagerService::instance();
  nm.start();

  if (auto req = nm.pending_secret_request(); req && req->ssid == want) {
    const std::string pw = trim_copy(password);
    if (pw.empty()) {
      if (out_error) *out_error = "Password required";
      return false;
    }
    nm.submit_secret(pw);
    return true;
  }

  if (!trim_copy(password).empty()) nm.queue_secret_for_ssid(want, trim_copy(password));

  const eh::net::Snapshot s = nm.snapshot();
  const auto it = std::find_if(s.aps.begin(), s.aps.end(), [&](const eh::net::AccessPoint& ap) { return ap.ssid == want; });
  if (it == s.aps.end()) {
    if (out_error) *out_error = "Network not found";
    return false;
  }
  const bool ok = nm.activate_ap(*it);
  if (!ok && out_error) *out_error = "Failed to start connection";
  return ok;
}

void paint_control_center_network_card(cairo_t* cr, double x, double y, double w, double h,
                                       const ControlCenterNetworkState& ns, double inner_glass_scale) {
  const auto mc = eh::config::derived_chrome_colors(eh::config::shell_config_snapshot().appearance);
  const double s = std::clamp(inner_glass_scale, 0.0, 1.0);
  const double r = std::max(14.0, std::min(w, h) * 0.16);
  cc_paint_glass_card_mc(cr, x, y, w, h, r, s, mc);

  const double icx = x + 24.0;
  const double icy = y + 28.0;
  cairo_save(cr);
  cairo_arc(cr, icx, icy, 14.0, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, mc.accentR, mc.accentG, mc.accentB, ns.connected ? 0.18 * s : 0.08 * s);
  cairo_fill(cr);
  cairo_restore(cr);

  const char* glyph = ns.connected ? (ns.wifi ? "wifi" : "lan") : "signal_wifi_off";
  const double gr = ns.connected ? mc.accentR : 0.65, gg = ns.connected ? mc.accentG : 0.68, gb = ns.connected ? mc.accentB : 0.72;
  eh::shell::draw_material_glyph(cr, icx, icy + 1.0, 20.0, glyph, gr, gg, gb, 0.96);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14.0);
  cairo_set_source_rgba(cr, kSurfR, kSurfG, kSurfB, 0.95);
  cairo_move_to(cr, x + 48.0, y + 30.0);
  const std::string title = ns.wifi ? (ns.ssid.empty() ? "Wi-Fi" : ns.ssid) : (ns.ethernet ? "Ethernet" : "Network");
  cairo_show_text(cr, title.c_str());

  cairo_set_font_size(cr, 11.0);
  cairo_set_source_rgba(cr, mc.outlineR, mc.outlineG, mc.outlineB, 0.90);
  cairo_move_to(cr, x + 48.0, y + 50.0);
  const std::string sub = ns.iface.empty() ? ns.status_text : (ns.iface + (ns.status_text.empty() ? "" : (" \xe2\x80\xa2 " + ns.status_text)));
  cairo_show_text(cr, sub.c_str());

  const std::string pill = ns.connected ? "Connected" : "Disconnected";
  cc_draw_status_pill(cr, x + w - 104.0, y + 18.0, pill, ns.connected, s, mc);
}

} // namespace eh::shell::dock::control_center
