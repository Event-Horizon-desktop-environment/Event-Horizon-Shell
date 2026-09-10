#include "desktop_shell/widgets/popup/vpn/vpn_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"
#include "services/network/core/network_manager_service.hpp"
#include "services/network/types/network_types.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace eh::shell::dock::popup::vpn {
namespace {
 
struct PopupVpnEntry {
  std::string uuid;
  std::string name;
  bool active = false;
  bool pending = false;
  std::chrono::steady_clock::time_point pendingSince{};

  void toggle() {
      
    auto& nm = eh::net::NetworkManagerService::instance();
    active = !active;
    pending = true;
    pendingSince = std::chrono::steady_clock::now();
    VpnConnectionInfo vpn{uuid, name, {}, active};
    if (active)
      nm.activateVpnConnection(vpn);
    else
      nm.deactivateVpnConnection(vpn);
  }
};

std::vector<PopupVpnEntry> s_entries;

} // anonymous namespace

void vpn_popup_sync_entries(const std::vector<VpnConnectionInfo>& connections) {
   
  const auto now = std::chrono::steady_clock::now();
  std::vector<PopupVpnEntry> next;
  next.reserve(connections.size());
  for (const auto& v : connections) {
    auto it = std::find_if(s_entries.begin(), s_entries.end(),
      [&](const PopupVpnEntry& e) { return e.uuid == v.uuid; });
    if (it != s_entries.end()) {
      PopupVpnEntry e = *it;
      if (e.pending &&
          (now - e.pendingSince >= std::chrono::seconds{8} || e.active == v.active)) {
        e.pending = false;
      }
      if (!e.pending) e.active = v.active;
      next.push_back(std::move(e));
    } else {
      next.push_back({v.uuid, v.name, v.active, false, {}});
    }
  }
  s_entries = std::move(next);
}

int vpn_popup_entry_count() {
   
  return static_cast<int>(s_entries.size());
}

const std::string& vpn_popup_entry_name(int idx) {
   
  return s_entries[static_cast<size_t>(idx)].name;
}

bool vpn_popup_entry_active(int idx) {
   
  return s_entries[static_cast<size_t>(idx)].active;
}

void vpn_popup_toggle_entry(int idx) {
   
  if (idx >= 0 && static_cast<size_t>(idx) < s_entries.size())
    s_entries[static_cast<size_t>(idx)].toggle();
}

void vpn_popup_remove_entry(int idx) {
   
  if (idx < 0 || static_cast<size_t>(idx) >= s_entries.size()) return;
  auto& nm = eh::net::NetworkManagerService::instance();
  nm.removeVpnConnection(s_entries[static_cast<size_t>(idx)].uuid);
  nm.refresh();
}

void dock_vpn_popup_handle_click(DockApp& app, double x, double y, uint32_t) {
   
  const double W = static_cast<double>(kVpnPopupW);
  const int vpnCount = static_cast<int>(s_entries.size());
  const double kRemoveBtnSz = 22.0;
  const double H = kPad + kHeaderH + static_cast<double>(std::max(vpnCount, 1)) * kRowH + kPad;

  // Close if outside bounds
  if (x < 0 || x >= W || y < 0 || y >= H) {
    popup_close(app);
    return;
  }

  // Close button
  const double cbX = W - kPad - 24.0;
  const double cbY = kPad;
  if (x >= cbX && x < cbX + 24.0 && y >= cbY && y < cbY + 24.0) {
    popup_close(app);
    return;
  }

  // VPN button hits
  for (int vi = 0; vi < vpnCount; ++vi) {
    const double rowY = kPad + kHeaderH + static_cast<double>(vi) * kRowH;
    const double rowMidY = rowY + kRowH * 0.5;
    const double remX = W - kPad - kRemoveBtnSz;
    const double remY = rowMidY - kRemoveBtnSz * 0.5;
    const double btnX = remX - 6.0 - kBtnW;
    const double btnY = rowMidY - kBtnH * 0.5;

    // Remove button
    if (x >= remX && x < remX + kRemoveBtnSz && y >= remY && y < remY + kRemoveBtnSz) {
      vpn_popup_remove_entry(vi);
      popup_draw_surface(app);
      return;
    }

    // Connect / Disconnect button
    if (x >= btnX && x < btnX + kBtnW && y >= btnY && y < btnY + kBtnH) {
      vpn_popup_toggle_entry(vi);
      popup_draw_surface(app);
      return;
    }
  }
}

} // namespace eh::shell::dock::popup::vpn
