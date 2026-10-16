#include "backends/hyprland/hyprland_window_tracker.h"
#include "backends/hyprland/hyprland_backends.h"
#include "hyprland-toplevel-mapping-v1-client-protocol.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace wspace::hyprland {

namespace {

const hyprland_toplevel_window_mapping_handle_v1_listener kListener = {
  .window_address = &HyprlandWindowTracker::onAddress,
  .failed = &HyprlandWindowTracker::onFail,
};

// The listener packs the 64-bit window address as two 32-bit halves.
std::uint64_t coalesceAddress(std::uint32_t hi, std::uint32_t lo) {
  return std::uint64_t{hi} << 32 | std::uint64_t{lo};
}

// Handle -> id lookup shared by both handle families.
template <typename Handle>
std::optional<std::string> idForHandle(Handle* handle,
                                       std::unordered_map<Handle*, std::string> const& byHandle) {
  if (handle == nullptr) { return std::nullopt; }
  auto found = byHandle.find(handle);
  if (found == byHandle.end() || found->second.empty()) { return std::nullopt; }
  return found->second;
}

// id -> handle lookup; an empty canonical form never resolves.
template <typename Handle>
Handle* handleForId(std::string_view id, std::unordered_map<std::string, Handle*> const& byId) {
  const auto key = canonicalWindowId(id);
  if (key.empty()) { return nullptr; }
  auto found = byId.find(key);
  if (found == byId.end()) { return nullptr; }
  return found->second;
}

} // namespace

template <typename Handle, typename SlotFn>
void HyprlandWindowTracker::syncHandles(std::vector<Handle*> const& handles, Kind kind,
                                        std::unordered_map<Handle*, std::string>& toId,
                                        std::unordered_map<std::string, Handle*>& byId,
                                        SlotFn&& slotOf) {
  if (m_mgr == nullptr) { return; }

  std::unordered_set<Handle*> live;
  for (auto* handle : handles) {
    if (handle == nullptr) { continue; }
    live.insert(handle);
    if (toId.contains(handle)) { continue; }

    Pending fresh;
    fresh.kind = kind;
    slotOf(fresh.toplevel) = handle;
    requestMapping(std::move(fresh));
  }

  std::vector<Handle*> dropped;
  for (auto const& entry : toId) {
    if (!live.contains(entry.first)) { dropped.push_back(entry.first); }
  }
  for (auto* handle : dropped) {
    auto found = toId.find(handle);
    if (found == toId.end()) { continue; }
    byId.erase(found->second);
    toId.erase(found);
  }

  std::vector<hyprland_toplevel_window_mapping_handle_v1*> abandoned;
  for (auto& [rh, pending] : m_pending) {
    if (pending.kind != kind) { continue; }
    auto* handle = slotOf(pending.toplevel);
    if (handle != nullptr && !live.contains(handle)) { abandoned.push_back(rh); }
  }
  for (auto* rh : abandoned) { abandon(rh); }
}

template <typename Handle>
void HyprlandWindowTracker::assignId(Handle* handle, std::uint64_t addr,
                                     std::unordered_map<Handle*, std::string>& toId,
                                     std::unordered_map<std::string, Handle*>& byId) {
  if (handle == nullptr) { return; }

  const auto id = fmtAddress(addr);
  if (auto superseded = toId.extract(handle); !superseded.empty()) {
    m_idToWlr.erase(superseded.mapped());
    m_idToExt.erase(superseded.mapped());
  }
  byId.insert_or_assign(id, handle);
  toId.insert_or_assign(handle, id);
  fireCallback();
}

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
  syncHandles(handles, Kind::Wlr, m_wlrToId, m_idToWlr,
              [](auto& toplevel) -> auto& { return toplevel.wlr; });
}

void HyprlandWindowTracker::syncExt(const std::vector<ext_foreign_toplevel_handle_v1*>& handles) {
  syncHandles(handles, Kind::Ext, m_extToId, m_idToExt,
              [](auto& toplevel) -> auto& { return toplevel.ext; });
}

std::optional<std::string> HyprlandWindowTracker::wlrWindowId(zwlr_foreign_toplevel_handle_v1* h) const {
  return idForHandle(h, m_wlrToId);
}

std::optional<std::string> HyprlandWindowTracker::extWindowId(ext_foreign_toplevel_handle_v1* h) const {
  return idForHandle(h, m_extToId);
}

zwlr_foreign_toplevel_handle_v1* HyprlandWindowTracker::wlrHandle(std::string_view id) const {
  return handleForId(id, m_idToWlr);
}

ext_foreign_toplevel_handle_v1* HyprlandWindowTracker::extHandle(std::string_view id) const {
  return handleForId(id, m_idToExt);
}

bool HyprlandWindowTracker::isKnown(std::string_view id) const {
  const auto key = canonicalWindowId(id);
  if (key.empty()) { return false; }
  return m_idToWlr.contains(key) || m_idToExt.contains(key);
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
  auto* request = std::exchange(p.req, nullptr);
  if (request != nullptr) {
    hyprland_toplevel_window_mapping_handle_v1_destroy(request);
  }
  p.toplevel.wlr = nullptr;
}

void HyprlandWindowTracker::fireCallback() {
  if (m_cb) { m_cb(); }
}

std::optional<HyprlandWindowTracker::Pending> HyprlandWindowTracker::takePending(
    hyprland_toplevel_window_mapping_handle_v1* rh) {
  auto arrived = m_pending.find(rh);
  if (arrived == m_pending.end()) { return std::nullopt; }
  auto settled = std::move(arrived->second);
  m_pending.erase(arrived);
  return settled;
}

void HyprlandWindowTracker::abandon(hyprland_toplevel_window_mapping_handle_v1* rh) {
  auto failed = m_pending.find(rh);
  if (failed == m_pending.end()) { return; }
  clear(failed->second);
  m_pending.erase(failed);
}

void HyprlandWindowTracker::onAddress(void* data, hyprland_toplevel_window_mapping_handle_v1* rh,
                                      std::uint32_t hi, std::uint32_t lo) {
  auto* self = static_cast<HyprlandWindowTracker*>(data);
  if (self == nullptr) { return; }

  auto settled = self->takePending(rh);
  if (!settled.has_value()) { return; }

  const auto addr = coalesceAddress(hi, lo);
  if (settled->kind == Kind::Ext && settled->toplevel.ext != nullptr) {
    self->assignId(settled->toplevel.ext, addr, self->m_extToId, self->m_idToExt);
  } else if (settled->kind == Kind::Wlr && settled->toplevel.wlr != nullptr) {
    self->assignId(settled->toplevel.wlr, addr, self->m_wlrToId, self->m_idToWlr);
  }
  self->clear(*settled);
}

void HyprlandWindowTracker::onFail(void* data, hyprland_toplevel_window_mapping_handle_v1* rh) {
  auto* self = static_cast<HyprlandWindowTracker*>(data);
  if (self == nullptr || rh == nullptr) { return; }
  self->abandon(rh);
}

} // namespace wspace::hyprland
