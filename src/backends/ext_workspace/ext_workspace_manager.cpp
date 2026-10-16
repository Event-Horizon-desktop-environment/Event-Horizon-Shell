#include "backends/ext_workspace/ext_workspace_manager.h"

#include "ext-workspace-v1-client-protocol.h"

#include <algorithm>
#include <unordered_set>

namespace {

// -- protocol listeners ------------------------------------------------------
// Each callback unwraps the manager pointer and forwards to the matching
// table mutation below. Empty capability notifications carry nothing.

void capsCb(void*, ext_workspace_group_handle_v1*, uint32_t) {}

void outEnterCb(void* d, ext_workspace_group_handle_v1* g, wl_output* o) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupOutputEnter(g, o);
}

void outLeaveCb(void* d, ext_workspace_group_handle_v1* g, wl_output* o) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupOutputLeave(g, o);
}

void wsEnterCb(void* d, ext_workspace_group_handle_v1* g, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupWorkspaceEnter(g, w);
}

void wsLeaveCb(void* d, ext_workspace_group_handle_v1* g, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupWorkspaceLeave(g, w);
}

void removedCb(void* d, ext_workspace_group_handle_v1* g) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupRemoved(g);
}

const ext_workspace_group_handle_v1_listener kGroupListener = {
  .capabilities = capsCb,
  .output_enter = outEnterCb,
  .output_leave = outLeaveCb,
  .workspace_enter = wsEnterCb,
  .workspace_leave = wsLeaveCb,
  .removed = removedCb,
};

void idCb(void* d, ext_workspace_handle_v1* w, const char* id) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceIdChanged(w, id);
}

void nameCb(void* d, ext_workspace_handle_v1* w, const char* n) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceNameChanged(w, n);
}

void coordCb(void* d, ext_workspace_handle_v1* w, wl_array* c) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceCoordsChanged(w, c);
}

void stateCb(void* d, ext_workspace_handle_v1* w, uint32_t s) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceStateChanged(w, s);
}

void capsWsCb(void*, ext_workspace_handle_v1*, uint32_t) {}

void removedWsCb(void* d, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceRemoved(w);
}

const ext_workspace_handle_v1_listener kWsListener = {
  .id = idCb,
  .name = nameCb,
  .coordinates = coordCb,
  .state = stateCb,
  .capabilities = capsWsCb,
  .removed = removedWsCb,
};

void mgrGroupCb(void* d, ext_workspace_manager_v1*, ext_workspace_group_handle_v1* g) {
  static_cast<ExtWorkspaceManager*>(d)->noteGroupCreated(g);
}

void mgrWsCb(void* d, ext_workspace_manager_v1*, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->noteWorkspaceCreated(w);
}

void mgrDoneCb(void* d, ext_workspace_manager_v1*) {
  static_cast<ExtWorkspaceManager*>(d)->noteManagerDone();
}

void mgrFinishedCb(void* d, ext_workspace_manager_v1*) {
  static_cast<ExtWorkspaceManager*>(d)->noteManagerFinished();
}

const ext_workspace_manager_v1_listener kMgrListener = {
  .workspace_group = mgrGroupCb,
  .workspace = mgrWsCb,
  .done = mgrDoneCb,
  .finished = mgrFinishedCb,
};

// Coordinates arrive padded (`[N,0,0]` on a flat list); the padding carries
// no information, so comparisons run on the trimmed views.
std::vector<std::uint32_t> stripped(std::vector<std::uint32_t> coords) {
  while (!coords.empty() && coords.back() == 0) { coords.pop_back(); }
  return coords;
}

// Protocol objects must be destroyed exactly once; both the live removal
// events and teardown funnel through these two retire points.
void retireWorkspace(ext_workspace_handle_v1* w) {
  if (w != nullptr) { ext_workspace_handle_v1_destroy(w); }
}

void retireGroup(ext_workspace_group_handle_v1* g) {
  if (g != nullptr) { ext_workspace_group_handle_v1_destroy(g); }
}

// Wayland array -> coordinate path; empty when the array is absent.
std::vector<std::uint32_t> decodePath(wl_array const* coordinates) {
  if (coordinates == nullptr) { return {}; }
  auto const* data = static_cast<std::uint32_t const*>(coordinates->data);
  auto const count = coordinates->size / sizeof(std::uint32_t);
  return {data, data + count};
}

} // namespace

void ExtWorkspaceManager::bindExtProtocol(ext_workspace_manager_v1* manager) {
  if (manager == nullptr) { return; }
  ext_workspace_manager_v1_add_listener(manager, &kMgrListener, this);
  m_mgr = manager;
}

void ExtWorkspaceManager::onStateChange(IDeskSource::Notify cb) {
  m_notify = std::move(cb);
}

// -- private lookup helpers --------------------------------------------------

ExtWorkspaceManager::GroupState* ExtWorkspaceManager::locateGroup(ext_workspace_group_handle_v1* h) {
  auto hit = std::ranges::find_if(m_groups, [h](GroupState const& g) { return g.h == h; });
  return hit != m_groups.end() ? &*hit : nullptr;
}

DeskSlot* ExtWorkspaceManager::lookupSlot(ext_workspace_handle_v1* w) {
  auto it = m_regions.find(w);
  return it != m_regions.end() ? &it->second : nullptr;
}

bool ExtWorkspaceManager::groupServesOutput(GroupState const& g, wl_output* output) const {
  return std::find(g.outs.begin(), g.outs.end(), output) != g.outs.end();
}

// -- navigation --------------------------------------------------------------

void ExtWorkspaceManager::jumpTo(const std::string& id) {
  if (m_mgr == nullptr) { return; }
  auto hit = std::ranges::find_if(m_regions,
    [&id](auto const& entry) { return entry.second.key == id; });
  if (hit == m_regions.end()) { return; }
  ext_workspace_handle_v1_activate(hit->first);
  ext_workspace_manager_v1_commit(m_mgr);
}

void ExtWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
  if (m_mgr == nullptr || output == nullptr) { return; }

  for (auto const& g : m_groups) {
    if (!groupServesOutput(g, output)) { continue; }
    for (auto* w : g.ws) {
      auto* slot = lookupSlot(w);
      if (slot == nullptr || slot->key != id) { continue; }
      ext_workspace_handle_v1_activate(w);
      ext_workspace_manager_v1_commit(m_mgr);
      return;
    }
  }

  jumpTo(id);
}

// Match quality for the region form: exact desk first, then key alone, then
// leading coordinate. Lower is better; -1 means no match.
int ExtWorkspaceManager::matchRank(DeskSlot const& cand, DeskSlot const& want) const {
  auto cw = stripped(want.path);
  auto ch = stripped(cand.path);
  if (cand.key == want.key && cand.label == want.label && cw == ch) { return 0; }
  if (!want.key.empty() && cand.key == want.key) { return 1; }
  if (!cw.empty() && !ch.empty() && ch[0] == cw[0]) { return 2; }
  return -1;
}

bool ExtWorkspaceManager::fireOnGroup(GroupState const& g, DeskSlot const& want, int rank) {
  if (m_mgr == nullptr) { return false; }
  for (auto* w : g.ws) {
    auto* slot = lookupSlot(w);
    if (slot == nullptr) { continue; }
    if (matchRank(*slot, want) != rank) { continue; }
    ext_workspace_handle_v1_activate(w);
    ext_workspace_manager_v1_commit(m_mgr);
    return true;
  }
  return false;
}

void ExtWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskSlot& workspace) {
  if (m_mgr == nullptr || output == nullptr) { return; }

  for (auto const& g : m_groups) {
    if (!groupServesOutput(g, output)) { continue; }
    for (int rank = 0; rank <= 2; ++rank) {
      if (fireOnGroup(g, workspace, rank)) { return; }
    }
  }

  if (!workspace.key.empty()) { jumpTo(workspace.key); }
}

void ExtWorkspaceManager::teardown() {
  for (auto const& entry : m_regions) { retireWorkspace(entry.first); }
  m_regions.clear();

  for (auto const& g : m_groups) { retireGroup(g.h); }
  m_groups.clear();

  if (m_mgr == nullptr) { return; }
  ext_workspace_manager_v1_stop(m_mgr);
  ext_workspace_manager_v1_destroy(m_mgr);
  m_mgr = nullptr;
}

// -- queries -----------------------------------------------------------------

void ExtWorkspaceManager::orderRegions(std::vector<DeskSlot>& regions) const {
  std::sort(regions.begin(), regions.end(),
            [](DeskSlot const& a, DeskSlot const& b) { return a.path < b.path; });

  std::uint32_t idx = 1;
  for (auto& ws : regions) { ws.ordinal = idx++; }
}

std::vector<DeskSlot> ExtWorkspaceManager::allRegions() const {
  std::unordered_set<ext_workspace_handle_v1*> seen;
  std::vector<DeskSlot> result;

  auto consider = [&](ext_workspace_handle_v1* w) {
    if (!seen.insert(w).second) { return; }
    auto* slot = const_cast<ExtWorkspaceManager*>(this)->lookupSlot(w);
    if (slot != nullptr && !slot->label.empty()) { result.push_back(*slot); }
  };

  for (auto const& g : m_groups) {
    for (auto* w : g.ws) { consider(w); }
  }
  for (auto const& [h, ws] : m_regions) {
    (void)ws;
    consider(h);
  }

  orderRegions(result);
  return result;
}

std::vector<DeskSlot> ExtWorkspaceManager::regionsOnOutput(wl_output* output) const {
  std::vector<DeskSlot> result;
  for (auto const& g : m_groups) {
    if (!groupServesOutput(g, output)) { continue; }
    for (auto* w : g.ws) {
      auto* slot = const_cast<ExtWorkspaceManager*>(this)->lookupSlot(w);
      if (slot != nullptr && !slot->label.empty()) { result.push_back(*slot); }
    }
  }

  orderRegions(result);
  return result;
}

// -- table mutations ---------------------------------------------------------

void ExtWorkspaceManager::noteGroupCreated(ext_workspace_group_handle_v1* group) {
  if (group == nullptr) { return; }
  ext_workspace_group_handle_v1_add_listener(group, &kGroupListener, this);
  m_groups.push_back(GroupState{.outs = {}, .ws = {}, .h = group});
}

void ExtWorkspaceManager::noteGroupRemoved(ext_workspace_group_handle_v1* group) {
  if (group == nullptr) { return; }
  std::erase_if(m_groups, [group](auto const& g) { return g.h == group; });
  retireGroup(group);
}

void ExtWorkspaceManager::noteGroupOutputEnter(ext_workspace_group_handle_v1* group, wl_output* output) {
  if (auto* g = locateGroup(group)) { g->outs.push_back(output); }
}

void ExtWorkspaceManager::noteGroupOutputLeave(ext_workspace_group_handle_v1* group, wl_output* output) {
  if (auto* g = locateGroup(group)) { std::erase(g->outs, output); }
}

void ExtWorkspaceManager::noteGroupWorkspaceEnter(
    ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace) {
  if (auto* g = locateGroup(group)) { g->ws.push_back(workspace); }
}

void ExtWorkspaceManager::noteGroupWorkspaceLeave(
    ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace) {
  if (auto* g = locateGroup(group)) { std::erase(g->ws, workspace); }
}

void ExtWorkspaceManager::noteWorkspaceCreated(ext_workspace_handle_v1* workspace) {
  if (workspace == nullptr) { return; }
  ext_workspace_handle_v1_add_listener(workspace, &kWsListener, this);
  m_regions.emplace(workspace, DeskSlot{});
}

void ExtWorkspaceManager::setSlotText(ext_workspace_handle_v1* workspace, const char* text,
                                      std::string DeskSlot::*field) {
  auto* slot = lookupSlot(workspace);
  if (slot == nullptr) { return; }
  (slot->*field).assign(text != nullptr ? text : "");
}

void ExtWorkspaceManager::noteWorkspaceIdChanged(ext_workspace_handle_v1* workspace, const char* id) {
  setSlotText(workspace, id, &DeskSlot::key);
}

void ExtWorkspaceManager::noteWorkspaceNameChanged(ext_workspace_handle_v1* workspace, const char* name) {
  setSlotText(workspace, name, &DeskSlot::label);
}

void ExtWorkspaceManager::noteWorkspaceCoordsChanged(
    ext_workspace_handle_v1* workspace, wl_array* coordinates) {
  auto* slot = lookupSlot(workspace);
  if (slot == nullptr) { return; }

  slot->path = decodePath(coordinates);
}

void ExtWorkspaceManager::noteWorkspaceStateChanged(ext_workspace_handle_v1* workspace, std::uint32_t state) {
  auto* slot = lookupSlot(workspace);
  if (slot == nullptr) { return; }

  const auto bit = [state](std::uint32_t flag) { return (state & flag) != 0; };
  const bool active = bit(EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
  const bool urgent = bit(EXT_WORKSPACE_HANDLE_V1_STATE_URGENT);
  const bool same = slot->state.selected == active && slot->state.urgent == urgent;
  if (same) { return; }

  slot->state.urgent = urgent;
  slot->state.selected = active;
  if (m_notify) { m_notify(); }
}

void ExtWorkspaceManager::noteWorkspaceRemoved(ext_workspace_handle_v1* workspace) {
  if (workspace != nullptr) {
    m_regions.erase(workspace);
    retireWorkspace(workspace);
  }
}

void ExtWorkspaceManager::noteManagerDone() {
  if (m_notify) { m_notify(); }
}

void ExtWorkspaceManager::noteManagerFinished() {
  m_mgr = nullptr;
  m_regions.clear();
  m_groups.clear();
}
