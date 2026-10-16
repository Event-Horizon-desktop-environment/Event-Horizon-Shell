#include "backends/hyprland/hyprland_workspace_manager.h"
#include "backends/hyprland/hyprland_backends.h"
#include "desktop_shell/common/log/debug_log.hpp"

#include <algorithm>
#include <ranges>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_set>

HyprlandWorkspaceManager::HyprlandWorkspaceManager(wspace::hyprland::HyprlandSession& runtime)
    : wspace::hyprland::HyprlandEventSink(runtime)
    , m_backend(runtime) {
  openConnection();
}

bool HyprlandWorkspaceManager::ready() const noexcept {
  return m_backend.canConnect();
}

bool HyprlandWorkspaceManager::openConnection() {
  const bool linked = m_backend.startSession();
  if (!linked) { return false; }

  reloadSnapshot();
  return true;
}

void HyprlandWorkspaceManager::onStateChange(IDeskSource::Notify cb) {
  m_notify = std::move(cb);
}

void HyprlandWorkspaceManager::setOutputResolver(IOutputResolver::Resolver r) {
  m_resolver = std::move(r);
}

int HyprlandWorkspaceManager::findIdForName(const std::string& id) const {
  long parsed = -1;
  if (std::sscanf(id.c_str(), "%ld", &parsed) != 1) { return -1; }
  if (parsed < 0 || parsed > 2147483647L) { return -1; }
  return static_cast<int>(parsed);
}

void HyprlandWorkspaceManager::jumpTo(const std::string& id) {
  const int wsId = findIdForName(id);
  if (wsId <= 0) { return; }
  std::string cmd;
  if (m_backend.isLuaConfig()) {
    cmd = std::format("dispatch hl.dsp.focus({{workspace = {}}})", wsId);
  } else {
    cmd = std::format("dispatch workspace {}", wsId);
  }
  const bool answered = m_backend.sendCommand(cmd).has_value();
  if (!answered) {
    debug_log("hyprland", "workspace focus request went unanswered: %s", cmd.c_str());
  }
}

void HyprlandWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
  (void)output;
  jumpTo(id);
}

void HyprlandWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskSlot& ws) {
  (void)output;
  jumpTo(ws.key);
}

static DeskSlot toDeskSlot(const HyprlandWorkspaceManager::WorkspaceRecord& ws) {
  DeskSlot r;
  r.key = std::to_string(ws.id);
  r.label = ws.name.empty() ? std::to_string(ws.id) : ws.name;
  r.ordinal = static_cast<std::uint32_t>(ws.id);
  r.state.selected = ws.active;
  r.state.urgent = ws.urgent;
  r.state.occupied = ws.occupied;
  return r;
}

std::vector<DeskSlot> HyprlandWorkspaceManager::allRegions() const {
  std::vector<const WorkspaceRecord*> ordered;
  for (auto& ws : m_spaces) {
    if (ws.id >= 0) { ordered.push_back(&ws); }
  }
  std::sort(ordered.begin(), ordered.end(),
    [](auto* a, auto* b) { return a->id < b->id; });

  std::vector<DeskSlot> result;
  for (auto* ws : ordered) { result.push_back(toDeskSlot(*ws)); }
  return result;
}

std::vector<DeskSlot> HyprlandWorkspaceManager::regionsOnOutput(wl_output* output) const {
  if (output == nullptr || !m_resolver) { return allRegions(); }
  auto name = m_resolver(output);
  if (name.empty()) { return allRegions(); }

  std::vector<const WorkspaceRecord*> ordered;
  for (auto& ws : m_spaces) {
    if (ws.id >= 0 && ws.monitor == name) { ordered.push_back(&ws); }
  }
  std::sort(ordered.begin(), ordered.end(),
    [](auto* a, auto* b) { return a->id < b->id; });

  std::vector<DeskSlot> result;
  for (auto* ws : ordered) { result.push_back(toDeskSlot(*ws)); }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
HyprlandWorkspaceManager::appsByDesk(wl_output* output) const {
  (void)output;
  std::unordered_map<std::string, std::vector<std::string>> result;
  std::unordered_map<int, std::unordered_set<std::string>> seen;
  for (auto& [_, t] : m_windows) {
    if (t.appId.empty()) { continue; }
    if (seen[t.workspaceId].insert(t.appId).second) {
      result[std::to_string(t.workspaceId)].push_back(t.appId);
    }
  }
  return result;
}

std::optional<std::string> HyprlandWorkspaceManager::focusedWindowTitle() const {
  for (auto& [_, t] : m_windows) {
    (void)_;
    return t.title;
  }
  return std::nullopt;
}

void HyprlandWorkspaceManager::teardown() {
  m_backend.shutdown();
}

void HyprlandWorkspaceManager::onCleanup() {
  m_spaces.clear();
  m_windows.clear();
  m_activeMap.clear();
}

void HyprlandWorkspaceManager::onStateChange() {
  if (m_notify) { m_notify(); }
}

void HyprlandWorkspaceManager::reloadSnapshot() {
  pullSnapshot();
  refreshFlags();
  onStateChange();
}

void HyprlandWorkspaceManager::loadWorkspaces() {
  auto json = m_backend.sendQuery("j/workspaces");
  if (!json || !json->is_array()) { return; }

  std::vector<WorkspaceRecord> next;
  for (auto& item : *json) {
    if (!item.is_object()) { continue; }
    WorkspaceRecord ws;
    ws.id = item.value("id", -1);
    ws.name = item.value("name", "");
    ws.monitor = item.value("monitor", "");
    if (ws.id >= 0) { next.push_back(std::move(ws)); }
  }
  m_spaces = std::move(next);
}

void HyprlandWorkspaceManager::loadMonitors() {
  auto json = m_backend.sendQuery("j/monitors");
  if (!json || !json->is_array()) { return; }

  std::unordered_map<std::string, int> byMon;
  for (auto& item : *json) {
    if (!item.is_object()) { continue; }
    auto name = item.value("name", std::string{});
    if (name.empty()) { continue; }
    auto aw = item.find("activeWorkspace");
    if (aw != item.end() && aw->is_object()) {
      auto idIt = aw->find("id");
      if (idIt != aw->end() && idIt->is_number_integer()) {
        byMon[name] = idIt->get<int>();
      }
    }
  }
  if (!byMon.empty()) { m_activeMap = std::move(byMon); }
}

void HyprlandWorkspaceManager::loadClients() {
  auto json = m_backend.sendQuery("j/clients");
  if (!json || !json->is_array()) { return; }

  std::unordered_map<std::uint64_t, ToplevelRecord> next;
  for (auto& item : *json) {
    if (!item.is_object()) { continue; }

    std::string addrStr;
    auto addrIt = item.find("address");
    if (addrIt != item.end() && addrIt->is_string()) { addrStr = addrIt->get<std::string>(); }

    auto addr = decodeHex(addrStr);
    if (!addr.has_value()) { continue; }

    ToplevelRecord st;
    auto wsIt = item.find("workspace");
    st.workspaceId = (wsIt != item.end() && wsIt->is_object()) ? wsIt->value("id", -1) : -1;
    st.appId = item.value("class", "");
    if (st.appId.empty()) { st.appId = item.value("initialClass", ""); }
    st.title = item.value("title", "");

    auto urgentIt = item.find("urgent");
    if (urgentIt != item.end() && urgentIt->is_boolean()) {
      st.urgent = urgentIt->get<bool>();
    } else {
      auto existing = m_windows.find(*addr);
      if (existing != m_windows.end()) { st.urgent = existing->second.urgent; }
    }

    next.emplace(*addr, std::move(st));
  }
  m_windows = std::move(next);
}

void HyprlandWorkspaceManager::pullSnapshot() {
  loadClients();
  loadMonitors();
  loadWorkspaces();
}

void HyprlandWorkspaceManager::afterFocusChange(int wsId) {
  dismissUrgent(wsId);
  refreshFlags();
  onStateChange();
}

void HyprlandWorkspaceManager::refreshFlags() {
  std::unordered_map<int, std::size_t> occupied;
  std::unordered_set<int> urgentWs;

  for (auto const& entry : m_windows) {
    auto const& t = entry.second;
    ++occupied[t.workspaceId];
    if (t.urgent) { urgentWs.insert(t.workspaceId); }
  }

  for (auto& ws : m_spaces) {
    ws.occupied = occupied[ws.id] > 0;
    ws.urgent = urgentWs.contains(ws.id);
    if (ws.monitor.empty()) {
      ws.active = false;
      continue;
    }
    auto act = m_activeMap.find(ws.monitor);
    ws.active = act != m_activeMap.end() && act->second == ws.id;
  }
}

void HyprlandWorkspaceManager::onEvent(std::string_view event, std::string_view data) {
  if (event == "configreloaded") { reloadSnapshot(); return; }

  if (event == "focusedmonv2") {
    auto args = splitByN(data, 2);
    auto id = decodeInt(args[1]);
    if (!id.has_value()) { return; }
    handleFocusChange(args[0], *id);
    return;
  }

  if (event == "workspacev2") {
    auto args = splitByN(data, 2);
    auto id = decodeInt(args[0]);
    if (!id.has_value()) { return; }
    loadMonitors();
    dismissUrgent(*id);
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "createworkspacev2") {
    auto args = splitByN(data, 2);
    auto id = decodeInt(args[0]);
    std::string name(args[1]);
    if (!id.has_value() || name.empty()) { return; }
    auto* ws = findWorkspace(*id);
    if (ws == nullptr) {
      m_spaces.push_back(WorkspaceRecord{.id = *id, .name = name, .monitor = {}});
    } else {
      ws->name = name;
    }
    loadWorkspaces();
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "destroyworkspacev2") {
    auto args = splitByN(data, 2);
    auto id = decodeInt(args[0]);
    if (!id.has_value()) { return; }
    m_spaces.erase(
      std::remove_if(m_spaces.begin(), m_spaces.end(),
        [&](auto& ws) { return ws.id == *id; }),
      m_spaces.end());
    for (auto it = m_windows.begin(); it != m_windows.end();) {
      if (it->second.workspaceId == *id) { it = m_windows.erase(it); }
      else { ++it; }
    }
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "renameworkspace") {
    auto args = splitByN(data, 2);
    auto id = decodeInt(args[0]);
    std::string newName(args[1]);
    if (!id.has_value() || newName.empty()) { return; }
    auto* ws = findWorkspace(*id);
    if (ws == nullptr) { loadWorkspaces(); }
    else { ws->name = newName; }
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "moveworkspacev2") {
    auto args = splitByN(data, 3);
    auto id = decodeInt(args[0]);
    std::string monitor(args[2]);
    if (!id.has_value()) { return; }
    auto* ws = findWorkspace(*id);
    if (ws != nullptr && !monitor.empty()) {
      ws->monitor = monitor;
      refreshFlags();
      onStateChange();
    }
    return;
  }

  if (event == "openwindow") {
    auto args = splitByN(data, 4);
    auto addr = decodeHex(args[0]);
    if (!addr.has_value()) { return; }
    relocateWindow(*addr, -1);
    auto it = m_windows.find(*addr);
    if (it != m_windows.end()) {
      it->second.appId = std::string(args[2]);
      it->second.title = std::string(args[3]);
    }
    loadClients();
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "closewindow") {
    auto args = splitByN(data, 1);
    auto addr = decodeHex(args[0]);
    if (!addr.has_value()) { return; }
    m_windows.erase(*addr);
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "movewindowv2") {
    auto args = splitByN(data, 3);
    auto addr = decodeHex(args[0]);
    auto id = decodeInt(args[2]);
    if (!addr.has_value() || !id.has_value()) { return; }
    relocateWindow(*addr, *id);
    refreshFlags();
    onStateChange();
    return;
  }

  if (event == "urgent") {
    auto args = splitByN(data, 1);
    auto addr = decodeHex(args[0]);
    if (!addr.has_value()) { return; }
    auto it = m_windows.find(*addr);
    if (it == m_windows.end()) {
      m_windows.emplace(*addr, ToplevelRecord{.workspaceId = -1, .appId = {}, .title = {}, .urgent = true});
    } else {
      it->second.urgent = true;
    }
    refreshFlags();
    onStateChange();
  }
}

void HyprlandWorkspaceManager::handleFocusChange(std::string_view mon, int wsId) {
  if (mon.empty()) { return; }
  m_activeMap.insert_or_assign(std::string(mon), wsId);
  afterFocusChange(wsId);
}

void HyprlandWorkspaceManager::dismissUrgent(int wsId) {
  auto affected = m_windows | std::views::values
                | std::views::filter([wsId](ToplevelRecord const& t) { return t.workspaceId == wsId; });
  for (auto& t : affected) { t.urgent = false; }
}

void HyprlandWorkspaceManager::relocateWindow(std::uint64_t addr, int wsId) {
  m_windows[addr].workspaceId = wsId;
}

HyprlandWorkspaceManager::WorkspaceRecord* HyprlandWorkspaceManager::findWorkspace(int id) {
  auto it = std::ranges::find_if(m_spaces, [id](WorkspaceRecord const& ws) { return ws.id == id; });
  return it != m_spaces.end() ? &*it : nullptr;
}

std::optional<std::uint64_t> HyprlandWorkspaceManager::decodeHex(std::string_view val) {
  if (val.empty()) { return std::nullopt; }
  if (val.size() > 2 && val[0] == '0' && (val[1] == 'x' || val[1] == 'X')) { val = val.substr(2); }
  std::uint64_t result = 0;
  auto [end, ec] = std::from_chars(val.data(), val.data() + val.size(), result, 16);
  if (ec != std::errc{} || end != val.data() + val.size()) { return std::nullopt; }
  return result;
}

std::optional<int> HyprlandWorkspaceManager::decodeInt(std::string_view val) {
  if (val.empty()) { return std::nullopt; }
  int result = 0;
  auto [end, ec] = std::from_chars(val.data(), val.data() + val.size(), result);
  if (ec != std::errc{} || end != val.data() + val.size()) { return std::nullopt; }
  return result;
}

std::vector<std::string_view> HyprlandWorkspaceManager::splitByN(std::string_view data, std::size_t count) {
  std::vector<std::string_view> args;
  args.reserve(count);
  std::string_view rest = data;
  while (args.size() + 1 < count) {
    const auto pos = rest.find(',');
    if (pos == std::string_view::npos) { break; }
    args.push_back(rest.substr(0, pos));
    rest.remove_prefix(pos + 1);
  }
  args.push_back(rest);
  args.resize(count);
  return args;
}
