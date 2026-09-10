#include "backends/hyprland/hyprland_window_tracker.h"
#include "backends/hyprland/hyprland_backends.h"
#include "hyprland-toplevel-mapping-v1-client-protocol.h"

#include <vector>

namespace wspace::hyprland {

namespace {

const hyprland_toplevel_window_mapping_handle_v1_listener kListener = {
  .window_address = &HyprlandWindowTracker::onAddress,
  .failed = &HyprlandWindowTracker::onFail,
};

} // namespace

HyprlandWindowTracker::~HyprlandWindowTracker() {
  if (m_mgr != nullptr) {
    hyprland_toplevel_mapping_manager_v1_destroy(m_mgr);
    m_mgr = nullptr;
  }
}

void HyprlandWindowTracker::setup(hyprland_toplevel_mapping_manager_v1* mgr) {
  m_mgr = mgr;
}

void HyprlandWindowTracker::setCallback(Callback cb) {
  m_cb = std::move(cb);
}

void HyprlandWindowTracker::syncWlr(const std::vector<zwlr_foreign_toplevel_handle_v1*>& handles) {
  if (m_mgr == nullptr) { return; }

  std::unordered_map<zwlr_foreign_toplevel_handle_v1*, bool> seen;
  for (auto* h : handles) {
    if (h == nullptr) { continue; }
    seen.emplace(h, true);
    if (!m_wlrToId.contains(h)) {
      requestMapping(Pending{.req = nullptr, .kind = Kind::Wlr, .toplevel = {.wlr = h}});
    }
  }

  std::vector<zwlr_foreign_toplevel_handle_v1*> stale;
  for (auto& [h, _] : m_wlrToId) {
    if (!seen.contains(h)) { stale.push_back(h); }
  }
  for (auto* h : stale) {
    auto it = m_wlrToId.find(h);
    if (it != m_wlrToId.end()) { m_idToWlr.erase(it->second); m_wlrToId.erase(it); }
  }

  std::vector<hyprland_toplevel_window_mapping_handle_v1*> staleReq;
  for (auto& [rh, p] : m_pending) {
    if (p.kind == Kind::Wlr && p.toplevel.wlr != nullptr && !seen.contains(p.toplevel.wlr)) {
      staleReq.push_back(rh);
    }
  }
  for (auto* rh : staleReq) {
    auto it = m_pending.find(rh);
    if (it != m_pending.end()) { clear(it->second); m_pending.erase(it); }
  }
}

void HyprlandWindowTracker::syncExt(const std::vector<ext_foreign_toplevel_handle_v1*>& handles) {
  if (m_mgr == nullptr) { return; }

  std::unordered_map<ext_foreign_toplevel_handle_v1*, bool> seen;
  for (auto* h : handles) {
    if (h == nullptr) { continue; }
    seen.emplace(h, true);
    if (!m_extToId.contains(h)) {
      requestMapping(Pending{.req = nullptr, .kind = Kind::Ext, .toplevel = {.ext = h}});
    }
  }

  std::vector<ext_foreign_toplevel_handle_v1*> stale;
  for (auto& [h, _] : m_extToId) {
    if (!seen.contains(h)) { stale.push_back(h); }
  }
  for (auto* h : stale) {
    auto it = m_extToId.find(h);
    if (it != m_extToId.end()) { m_idToExt.erase(it->second); m_extToId.erase(it); }
  }

  std::vector<hyprland_toplevel_window_mapping_handle_v1*> staleReq;
  for (auto& [rh, p] : m_pending) {
    if (p.kind == Kind::Ext && p.toplevel.ext != nullptr && !seen.contains(p.toplevel.ext)) {
      staleReq.push_back(rh);
    }
  }
  for (auto* rh : staleReq) {
    auto it = m_pending.find(rh);
    if (it != m_pending.end()) { clear(it->second); m_pending.erase(it); }
  }
}

std::optional<std::string> HyprlandWindowTracker::wlrWindowId(zwlr_foreign_toplevel_handle_v1* h) const {
  if (h == nullptr) { return std::nullopt; }
  auto it = m_wlrToId.find(h);
  return (it != m_wlrToId.end() && !it->second.empty()) ? std::optional(it->second) : std::nullopt;
}

std::optional<std::string> HyprlandWindowTracker::extWindowId(ext_foreign_toplevel_handle_v1* h) const {
  if (h == nullptr) { return std::nullopt; }
  auto it = m_extToId.find(h);
  return (it != m_extToId.end() && !it->second.empty()) ? std::optional(it->second) : std::nullopt;
}

zwlr_foreign_toplevel_handle_v1* HyprlandWindowTracker::wlrHandle(std::string_view id) const {
  auto c = canonicalWindowId(id);
  if (c.empty()) { return nullptr; }
  auto it = m_idToWlr.find(c);
  return it != m_idToWlr.end() ? it->second : nullptr;
}

ext_foreign_toplevel_handle_v1* HyprlandWindowTracker::extHandle(std::string_view id) const {
  auto c = canonicalWindowId(id);
  if (c.empty()) { return nullptr; }
  auto it = m_idToExt.find(c);
  return it != m_idToExt.end() ? it->second : nullptr;
}

bool HyprlandWindowTracker::isKnown(std::string_view id) const {
  return wlrHandle(id) != nullptr || extHandle(id) != nullptr;
}

void HyprlandWindowTracker::requestMapping(Pending p) {
  if (m_mgr == nullptr) { return; }
  for (auto& [_, existing] : m_pending) {
    if (existing.kind == p.kind) {
      bool match = p.kind == Kind::Wlr
        ? existing.toplevel.wlr == p.toplevel.wlr
        : existing.toplevel.ext == p.toplevel.ext;
      if (match) { return; }
    }
  }

  auto* rh = p.kind == Kind::Wlr
    ? hyprland_toplevel_mapping_manager_v1_get_window_for_toplevel_wlr(m_mgr, p.toplevel.wlr)
    : hyprland_toplevel_mapping_manager_v1_get_window_for_toplevel(m_mgr, p.toplevel.ext);
  if (rh == nullptr) { return; }

  p.req = rh;
  hyprland_toplevel_window_mapping_handle_v1_add_listener(rh, &kListener, this);
  m_pending.emplace(rh, std::move(p));
}

void HyprlandWindowTracker::clear(Pending& p) {
  if (p.req != nullptr) {
    hyprland_toplevel_window_mapping_handle_v1_destroy(p.req);
    p.req = nullptr;
  }
  p.toplevel.wlr = nullptr;
}

void HyprlandWindowTracker::setWlrId(zwlr_foreign_toplevel_handle_v1* h, std::uint64_t addr) {
  if (h == nullptr) { return; }
  auto id = fmtAddress(addr);
  auto existing = m_wlrToId.find(h);
  if (existing != m_wlrToId.end()) { m_idToWlr.erase(existing->second); m_idToExt.erase(existing->second); }
  m_wlrToId[h] = id;
  m_idToWlr[id] = h;
  fireCallback();
}

void HyprlandWindowTracker::setExtId(ext_foreign_toplevel_handle_v1* h, std::uint64_t addr) {
  if (h == nullptr) { return; }
  auto id = fmtAddress(addr);
  auto existing = m_extToId.find(h);
  if (existing != m_extToId.end()) { m_idToWlr.erase(existing->second); m_idToExt.erase(existing->second); }
  m_extToId[h] = id;
  m_idToExt[id] = h;
  fireCallback();
}

void HyprlandWindowTracker::fireCallback() {
  if (m_cb) { m_cb(); }
}

void HyprlandWindowTracker::onAddress(void* data, hyprland_toplevel_window_mapping_handle_v1* rh,
                                      std::uint32_t hi, std::uint32_t lo) {
  auto* self = static_cast<HyprlandWindowTracker*>(data);
  if (self == nullptr || rh == nullptr) { return; }

  auto it = self->m_pending.find(rh);
  if (it == self->m_pending.end()) { return; }

  std::uint64_t addr = (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
  auto p = std::move(it->second);
  self->m_pending.erase(it);
  auto kind = p.kind;
  self->clear(p);

  if (kind == Kind::Ext && p.toplevel.ext != nullptr) {
    self->setExtId(p.toplevel.ext, addr);
  } else if (kind == Kind::Wlr && p.toplevel.wlr != nullptr) {
    self->setWlrId(p.toplevel.wlr, addr);
  }
}

void HyprlandWindowTracker::onFail(void* data, hyprland_toplevel_window_mapping_handle_v1* rh) {
  auto* self = static_cast<HyprlandWindowTracker*>(data);
  if (self == nullptr || rh == nullptr) { return; }
  auto it = self->m_pending.find(rh);
  if (it == self->m_pending.end()) { return; }
  self->clear(it->second);
  self->m_pending.erase(it);
}

} // namespace wspace::hyprland
