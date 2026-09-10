#include "backends/ext_workspace/ext_workspace_manager.h"

#include "ext-workspace-v1-client-protocol.h"

#include <algorithm>

namespace {

void capsCb(void*, ext_workspace_group_handle_v1*, uint32_t) {}

void outEnterCb(void* d, ext_workspace_group_handle_v1* g, wl_output* o) {
  static_cast<ExtWorkspaceManager*>(d)->onGroupOutputEnter(g, o);
}

void outLeaveCb(void* d, ext_workspace_group_handle_v1* g, wl_output* o) {
  static_cast<ExtWorkspaceManager*>(d)->onGroupOutputLeave(g, o);
}

void wsEnterCb(void* d, ext_workspace_group_handle_v1* g, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->onGroupWorkspaceEnter(g, w);
}

void wsLeaveCb(void* d, ext_workspace_group_handle_v1* g, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->onGroupWorkspaceLeave(g, w);
}

void removedCb(void* d, ext_workspace_group_handle_v1* g) {
  static_cast<ExtWorkspaceManager*>(d)->onGroupRemoved(g);
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
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceIdChanged(w, id);
}

void nameCb(void* d, ext_workspace_handle_v1* w, const char* n) {
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceNameChanged(w, n);
}

void coordCb(void* d, ext_workspace_handle_v1* w, wl_array* c) {
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceCoordinatesChanged(w, c);
}

void stateCb(void* d, ext_workspace_handle_v1* w, uint32_t s) {
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceStateChanged(w, s);
}

void capsWsCb(void*, ext_workspace_handle_v1*, uint32_t) {}

void removedWsCb(void* d, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceRemoved(w);
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
  static_cast<ExtWorkspaceManager*>(d)->onGroupCreated(g);
}

void mgrWsCb(void* d, ext_workspace_manager_v1*, ext_workspace_handle_v1* w) {
  static_cast<ExtWorkspaceManager*>(d)->onWorkspaceCreated(w);
}

void mgrDoneCb(void* d, ext_workspace_manager_v1*) {
  static_cast<ExtWorkspaceManager*>(d)->onManagerDone();
}

void mgrFinishedCb(void* d, ext_workspace_manager_v1*) {
  static_cast<ExtWorkspaceManager*>(d)->onManagerFinished();
}

const ext_workspace_manager_v1_listener kMgrListener = {
  .workspace_group = mgrGroupCb,
  .workspace = mgrWsCb,
  .done = mgrDoneCb,
  .finished = mgrFinishedCb,
};

std::vector<std::uint32_t> trimZeros(std::vector<std::uint32_t> coords) {
  while (!coords.empty() && coords.back() == 0) { coords.pop_back(); }
  return coords;
}

} // namespace

void ExtWorkspaceManager::bindExtProtocol(ext_workspace_manager_v1* manager) {
  m_mgr = manager;
  ext_workspace_manager_v1_add_listener(m_mgr, &kMgrListener, this);
}

void ExtWorkspaceManager::onStateChange(IWorkspaceManager::Notify cb) {
  m_notify = std::move(cb);
}

void ExtWorkspaceManager::jumpTo(const std::string& id) {
  if (m_mgr == nullptr) { return; }
  for (auto const& [h, ws] : m_regions) {
    if (ws.id != id) { continue; }
    ext_workspace_handle_v1_activate(h);
    ext_workspace_manager_v1_commit(m_mgr);
    return;
  }
}

void ExtWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
  if (m_mgr == nullptr || output == nullptr) { return; }

  for (auto const& g : m_groups) {
    bool onOutput = std::find(g.outs.begin(), g.outs.end(), output) != g.outs.end();
    if (!onOutput) { continue; }
    for (auto* w : g.ws) {
      auto it = m_regions.find(w);
      if (it == m_regions.end() || it->second.id != id) { continue; }
      ext_workspace_handle_v1_activate(w);
      ext_workspace_manager_v1_commit(m_mgr);
      return;
    }
  }

  jumpTo(id);
}

void ExtWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskRegion& workspace) {
  if (m_mgr == nullptr || output == nullptr) { return; }

  auto exact = [&](auto const& c) {
    return c.id == workspace.id && c.name == workspace.name
        && trimZeros(c.coordinates) == trimZeros(workspace.coordinates);
  };
  auto byId = [&](auto const& c) { return !workspace.id.empty() && c.id == workspace.id; };
  auto byCoord = [&](auto const& c) {
    auto w = trimZeros(workspace.coordinates);
    auto h = trimZeros(c.coordinates);
    return !w.empty() && !h.empty() && w[0] == h[0];
  };

  for (auto const& g : m_groups) {
    bool onOutput = std::find(g.outs.begin(), g.outs.end(), output) != g.outs.end();
    if (!onOutput) { continue; }

    auto activate = [&](auto pred) {
      for (auto* w : g.ws) {
        auto it = m_regions.find(w);
        if (it == m_regions.end() || !pred(it->second)) { continue; }
        ext_workspace_handle_v1_activate(w);
        ext_workspace_manager_v1_commit(m_mgr);
        return true;
      }
      return false;
    };

    if (activate(exact)) { return; }
    if (activate(byId)) { return; }
    if (activate(byCoord)) { return; }
  }

  if (!workspace.id.empty()) { jumpTo(workspace.id); }
}

void ExtWorkspaceManager::teardown() {
  for (auto& [w, _] : m_regions) {
    if (w != nullptr) { ext_workspace_handle_v1_destroy(w); }
  }
  m_regions.clear();

  for (auto& g : m_groups) {
    if (g.h != nullptr) { ext_workspace_group_handle_v1_destroy(g.h); }
  }
  m_groups.clear();

  if (m_mgr != nullptr) {
    ext_workspace_manager_v1_stop(m_mgr);
    ext_workspace_manager_v1_destroy(m_mgr);
    m_mgr = nullptr;
  }
}

std::vector<DeskRegion> ExtWorkspaceManager::allRegions() const {
  std::vector<DeskRegion> result;
  std::vector<ext_workspace_handle_v1*> seen;

  for (auto const& g : m_groups) {
    for (auto* w : g.ws) {
      if (std::find(seen.begin(), seen.end(), w) != seen.end()) { continue; }
      auto it = m_regions.find(w);
      if (it != m_regions.end() && !it->second.name.empty()) {
        result.push_back(it->second);
        seen.push_back(w);
      }
    }
  }

  for (auto const& [h, ws] : m_regions) {
    if (ws.name.empty() || std::find(seen.begin(), seen.end(), h) != seen.end()) { continue; }
    result.push_back(ws);
  }

  std::sort(result.begin(), result.end(),
    [](auto const& a, auto const& b) { return a.coordinates < b.coordinates; });

  std::uint32_t idx = 1;
  for (auto& ws : result) { ws.index = idx++; }
  return result;
}

std::vector<DeskRegion> ExtWorkspaceManager::regionsOnOutput(wl_output* output) const {
  std::vector<ext_workspace_handle_v1*> handles;
  for (auto const& g : m_groups) {
    bool onOutput = std::find(g.outs.begin(), g.outs.end(), output) != g.outs.end();
    if (onOutput) { handles.insert(handles.end(), g.ws.begin(), g.ws.end()); }
  }

  std::vector<DeskRegion> result;
  for (auto* w : handles) {
    auto it = m_regions.find(w);
    if (it != m_regions.end() && !it->second.name.empty()) { result.push_back(it->second); }
  }

  std::sort(result.begin(), result.end(),
    [](auto const& a, auto const& b) { return a.coordinates < b.coordinates; });

  std::uint32_t idx = 1;
  for (auto& ws : result) { ws.index = idx++; }
  return result;
}

void ExtWorkspaceManager::onGroupCreated(ext_workspace_group_handle_v1* group) {
  if (group == nullptr) { return; }
  m_groups.push_back(GroupState{.h = group, .outs = {}, .ws = {}});
  ext_workspace_group_handle_v1_add_listener(group, &kGroupListener, this);
}

void ExtWorkspaceManager::onGroupRemoved(ext_workspace_group_handle_v1* group) {
  std::erase_if(m_groups, [group](auto const& g) { return g.h == group; });
  if (group != nullptr) { ext_workspace_group_handle_v1_destroy(group); }
}

void ExtWorkspaceManager::onGroupOutputEnter(ext_workspace_group_handle_v1* group, wl_output* output) {
  for (auto& g : m_groups) {
    if (g.h == group) { g.outs.push_back(output); return; }
  }
}

void ExtWorkspaceManager::onGroupOutputLeave(ext_workspace_group_handle_v1* group, wl_output* output) {
  for (auto& g : m_groups) {
    if (g.h == group) { std::erase(g.outs, output); return; }
  }
}

void ExtWorkspaceManager::onGroupWorkspaceEnter(
    ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace) {
  for (auto& g : m_groups) {
    if (g.h == group) { g.ws.push_back(workspace); return; }
  }
}

void ExtWorkspaceManager::onGroupWorkspaceLeave(
    ext_workspace_group_handle_v1* group, ext_workspace_handle_v1* workspace) {
  for (auto& g : m_groups) {
    if (g.h == group) { std::erase(g.ws, workspace); return; }
  }
}

void ExtWorkspaceManager::onWorkspaceCreated(ext_workspace_handle_v1* workspace) {
  if (workspace == nullptr) { return; }
  m_regions.emplace(workspace, DeskRegion{});
  ext_workspace_handle_v1_add_listener(workspace, &kWsListener, this);
}

void ExtWorkspaceManager::onWorkspaceIdChanged(ext_workspace_handle_v1* workspace, const char* id) {
  auto it = m_regions.find(workspace);
  if (it != m_regions.end()) { it->second.id = id != nullptr ? id : ""; }
}

void ExtWorkspaceManager::onWorkspaceNameChanged(ext_workspace_handle_v1* workspace, const char* name) {
  auto it = m_regions.find(workspace);
  if (it != m_regions.end()) { it->second.name = name != nullptr ? name : ""; }
}

void ExtWorkspaceManager::onWorkspaceCoordinatesChanged(
    ext_workspace_handle_v1* workspace, wl_array* coordinates) {
  auto it = m_regions.find(workspace);
  if (it == m_regions.end()) { return; }

  it->second.coordinates.clear();
  if (coordinates != nullptr) {
    auto* data = static_cast<std::uint32_t*>(coordinates->data);
    auto count = coordinates->size / sizeof(std::uint32_t);
    it->second.coordinates.assign(data, data + count);
  }
}

void ExtWorkspaceManager::onWorkspaceStateChanged(ext_workspace_handle_v1* workspace, std::uint32_t state) {
  auto it = m_regions.find(workspace);
  if (it == m_regions.end()) { return; }

  bool active = (state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE) != 0;
  bool urgent = (state & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT) != 0;
  if (it->second.active == active && it->second.urgent == urgent) { return; }

  it->second.active = active;
  it->second.urgent = urgent;
  if (m_notify) { m_notify(); }
}

void ExtWorkspaceManager::onWorkspaceRemoved(ext_workspace_handle_v1* workspace) {
  m_regions.erase(workspace);
  if (workspace != nullptr) { ext_workspace_handle_v1_destroy(workspace); }
}

void ExtWorkspaceManager::onManagerDone() {
  if (m_notify) { m_notify(); }
}

void ExtWorkspaceManager::onManagerFinished() {
  m_mgr = nullptr;
  m_regions.clear();
  m_groups.clear();
}
