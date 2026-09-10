#include "backends/triad/triad_workspace_manager.h"
#include "backends/triad/triad_backends.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

auto constexpr g_retryBase = std::chrono::seconds(2);
auto constexpr g_retryCeiling = std::chrono::seconds(30);
auto constexpr g_readLimit = 1024U * 1024U;

auto constexpr kSubscribePayload =
  "{\"triad\":{\"version\":1,\"request\":\"event-stream\",\"events\":[\"state\",\"layout\",\"window\"]}}\n";

bool pump(int fd, std::string_view data) {
  std::size_t cursor = 0;
  while (cursor < data.size()) {
    auto n = ::write(fd, data.data() + cursor, data.size() - cursor);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      return false;
    }
    cursor += static_cast<std::size_t>(n);
  }
  return true;
}

nlohmann::json const* locateObj(nlohmann::json const& node, char const* field) {
  if (!node.is_object()) return nullptr;
  auto iter = node.find(field);
  return (iter != node.end() && iter->is_object()) ? &*iter : nullptr;
}

nlohmann::json const* locateArr(nlohmann::json const& node, char const* field) {
  if (!node.is_object()) return nullptr;
  auto iter = node.find(field);
  return (iter != node.end() && iter->is_array()) ? &*iter : nullptr;
}

} // namespace

TriadWorkspaceManager::TriadWorkspaceManager(wspace::triad::TriadRuntime& core)
  : m_core(core) {
  if (m_core.canConnect()) {
    requestFresh();
    (void)openConnection();
  }
}

TriadWorkspaceManager::~TriadWorkspaceManager() { teardown(); }

bool TriadWorkspaceManager::ready() const noexcept { return m_core.canConnect(); }
bool TriadWorkspaceManager::supportsOverview() const noexcept { return m_core.canConnect(); }

void TriadWorkspaceManager::onStateChange(IWorkspaceManager::Notify cb) { m_onChange = std::move(cb); }
void TriadWorkspaceManager::onOverviewChange(wspace::IWorkspaceDataBackend::Notify cb) { m_onOverviewChange = std::move(cb); }
void TriadWorkspaceManager::setOutputResolver(Resolver resolver) { m_lookup = std::move(resolver); }

bool TriadWorkspaceManager::openConnection() {
  if (m_fd >= 0) return true;
  auto const& endpoint = m_core.queryEndpoint();
  if (endpoint.empty()) return false;

  auto now = std::chrono::steady_clock::now();
  if (m_nextRetry.time_since_epoch().count() != 0 && now < m_nextRetry) return false;

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { scheduleRetry(); return false; }

  sockaddr_un addr;
  addr.sun_family = AF_UNIX;
  if (endpoint.size() >= sizeof(addr.sun_path)) { ::close(fd); scheduleRetry(); return false; }
  std::memcpy(addr.sun_path, endpoint.c_str(), endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd); scheduleRetry(); return false;
  }

  if (!pump(fd, kSubscribePayload)) {
    ::close(fd); scheduleRetry(); return false;
  }

  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  m_fd = fd;
  m_nextRetry = {};
  m_backoff = g_retryBase;
  m_pending.clear();
  return true;
}

void TriadWorkspaceManager::jumpTo(std::string const& id) {
  auto idx = parseIndex(id);
  if (idx) (void)m_core.dispatchAction("focus-workspace", {{"workspace_idx", *idx}});
}

void TriadWorkspaceManager::jumpToOnOutput(wl_output*, std::string const& id) { jumpTo(id); }

void TriadWorkspaceManager::jumpToOnOutput(wl_output*, DeskRegion const& ws) {
  if (ws.index > 0) { (void)m_core.dispatchAction("focus-workspace", {{"workspace_idx", ws.index}}); return; }
  jumpTo(ws.id);
}

std::vector<DeskRegion> TriadWorkspaceManager::allRegions() const {
  std::vector<DeskRegion> out;
  auto list = orderedDesks();
  out.reserve(list.size());
  for (auto* ds : list) {
    out.push_back(DeskRegion{
      .id = makeKey(*ds),
      .name = ds->label.empty() ? makeKey(*ds) : ds->label,
      .coordinates = {ds->idx},
      .index = ds->idx,
      .active = ds->focused,
      .urgent = ds->attention,
      .occupied = ds->hasContent,
    });
  }
  return out;
}

std::vector<DeskRegion> TriadWorkspaceManager::regionsOnOutput(wl_output* output) const {
  std::vector<DeskRegion> out;
  auto list = orderedDesks(resolveOutput(output));
  out.reserve(list.size());
  for (auto* ds : list) {
    out.push_back(DeskRegion{
      .id = makeKey(*ds),
      .name = ds->label.empty() ? makeKey(*ds) : ds->label,
      .coordinates = {ds->idx},
      .index = ds->idx,
      .active = ds->focused,
      .urgent = ds->attention,
      .occupied = ds->hasContent,
    });
  }
  return out;
}

std::unordered_map<std::string, std::vector<std::string>>
TriadWorkspaceManager::appsByDesk(wl_output* output) const {
  return appsByDesk(resolveOutput(output));
}

std::unordered_map<std::string, std::vector<std::string>>
TriadWorkspaceManager::appsByDesk(std::string const& monitor) const {
  std::unordered_map<std::uint32_t, DeskState const*> lookup;
  for (auto* ds : orderedDesks(monitor)) lookup.emplace(ds->idx, ds);

  std::unordered_map<std::string, std::vector<std::string>> result;
  std::unordered_map<std::string, std::unordered_set<std::string>> dedup;
  for (auto const& [_, w] : m_entries) {
    if (w.parentWorkspace == 0 || w.app.empty()) continue;
    auto it = lookup.find(w.parentWorkspace);
    if (it == lookup.end()) continue;
    auto key = makeKey(*it->second);
    if (dedup[key].insert(w.app).second) result[key].push_back(w.app);
  }
  return result;
}

std::vector<DeskWindow> TriadWorkspaceManager::windowsOnDesk(wl_output* output) const {
  return windowsOnDesk(resolveOutput(output));
}

std::vector<DeskWindow> TriadWorkspaceManager::windowsOnDesk(std::string const& monitor) const {
  std::unordered_map<std::uint32_t, DeskState const*> lookup;
  for (auto* ds : orderedDesks(monitor)) lookup.emplace(ds->idx, ds);

  std::vector<DeskWindow> out;
  out.reserve(m_entries.size());
  for (auto const& [_, w] : m_entries) {
    auto it = lookup.find(w.parentWorkspace);
    if (it == lookup.end()) continue;
    out.push_back(DeskWindow{
      .windowId = std::to_string(w.id),
      .deskKey = makeKey(*it->second),
      .appId = w.app,
      .title = w.caption,
      .x = w.col,
      .y = w.row,
    });
  }
  return out;
}

void TriadWorkspaceManager::bringToFront(std::string const& windowId) {
  auto id = parseNumeric(windowId);
  if (id) (void)m_core.dispatchAction("focus-window", {{"id", *id}});
}

void TriadWorkspaceManager::teardown() {
  shutdownSocket(false);
  bool prevKnown = m_overviewSeen && m_overviewVisible;
  m_knownOutputs.clear();
  m_catalog.clear();
  m_entries.clear();
  m_overviewSeen = false;
  m_overviewVisible = false;
  m_pending.clear();
  m_backoff = g_retryBase;
  if (prevKnown) fireOverviewChange();
}

int TriadWorkspaceManager::eventTimeout() const noexcept {
  if (m_fd >= 0 || !m_core.canConnect()) return -1;
  if (m_nextRetry.time_since_epoch().count() == 0) return 0;
  auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
    m_nextRetry - std::chrono::steady_clock::now()).count();
  return static_cast<int>(std::max<std::int64_t>(0, remaining));
}

void TriadWorkspaceManager::onEvent(short revents) {
  if (!m_core.canConnect()) return;
  if (m_fd < 0) { (void)openConnection(); return; }
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) { shutdownSocket(true); return; }
  if ((revents & POLLIN) != 0) drainSocket();
}

void TriadWorkspaceManager::sync(std::vector<DeskRegion>& regions, std::string const& monitor) const {
  if (regions.empty() || m_catalog.empty()) return;
  auto candidates = orderedDesks(monitor);
  for (auto& r : regions) {
    auto parsed = parseIndex(r.id);
    auto found = std::find_if(candidates.begin(), candidates.end(), [&](DeskState const* ds) {
      if (parsed && ds->idx == *parsed) return true;
      return !r.name.empty() && r.name == ds->label;
    });
    if (found == candidates.end()) { r.index = 0; r.occupied = false; continue; }
    r.index = (*found)->idx;
    r.occupied = (*found)->hasContent;
    r.urgent = (*found)->attention;
  }
}

std::vector<std::string> TriadWorkspaceManager::deskKeys(std::string const& monitor) const {
  auto list = orderedDesks(monitor);
  std::vector<std::string> keys;
  keys.reserve(list.size());
  for (auto* ds : list) keys.push_back(makeKey(*ds));
  return keys;
}

std::optional<std::string> TriadWorkspaceManager::fetchFocusedWindow() const {
  for (auto const& [_, ds] : m_catalog) {
    if (ds.globallyActive && ds.activeChild) return std::to_string(*ds.activeChild);
  }
  return std::nullopt;
}

void TriadWorkspaceManager::shutdownSocket(bool requeue) {
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  if (requeue) scheduleRetry(); else m_nextRetry = {};
}

void TriadWorkspaceManager::scheduleRetry() {
  m_nextRetry = std::chrono::steady_clock::now() + m_backoff;
  m_backoff = std::min(m_backoff * 2, g_retryCeiling);
}

void TriadWorkspaceManager::drainSocket() {
  std::array<char, 4096> buf{};
  for (;;) {
    auto n = ::read(m_fd, buf.data(), buf.size());
    if (n > 0) {
      m_pending.insert(m_pending.end(), buf.begin(), buf.begin() + n);
      if (m_pending.size() > g_readLimit) { shutdownSocket(true); m_pending.clear(); return; }
      continue;
    }
    if (n == 0) { shutdownSocket(true); return; }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    shutdownSocket(true); return;
  }
  processLines();
}

void TriadWorkspaceManager::processLines() {
  auto start = m_pending.begin();
  for (auto cur = m_pending.begin(); cur != m_pending.end(); ++cur) {
    if (*cur != '\n') continue;
    std::string line(start, cur);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && !ingestLine(line)) { m_pending.clear(); return; }
    start = std::next(cur);
  }
  if (start != m_pending.begin()) m_pending.erase(m_pending.begin(), start);
}

bool TriadWorkspaceManager::ingestLine(std::string_view line) {
  nlohmann::json msg;
  try { msg = nlohmann::json::parse(line); }
  catch (nlohmann::json::exception const&) { return true; }

  auto* triad = locateObj(msg, "triad");
  if (!triad) return true;

  auto okIt = triad->find("ok");
  if (okIt != triad->end() && okIt->is_boolean() && !okIt->get<bool>()) {
    shutdownSocket(true); return false;
  }

  bool changed = false;
  bool overviewToggled = false;

  auto stateIt = triad->find("state");
  if (stateIt != triad->end() && stateIt->is_object()) {
    bool beforeKnown = m_overviewSeen;
    bool beforeOpen = m_overviewVisible;
    changed = applyFullState(*stateIt);
    overviewToggled = (beforeKnown != m_overviewSeen || beforeOpen != m_overviewVisible);
  } else {
    auto winIt = triad->find("window");
    if (winIt != triad->end() && winIt->is_object()) changed = applySingleWindow(*winIt);
  }

  if (changed) fireChange();
  if (overviewToggled) fireOverviewChange();
  return true;
}

bool TriadWorkspaceManager::applyFullState(nlohmann::json const& payload) {
  bool changed = false;

  auto* overview = locateObj(payload, "overview");
  if (overview) {
    auto openIt = overview->find("is_open");
    if (openIt != overview->end() && openIt->is_boolean()) {
      bool open = openIt->get<bool>();
      changed = changed || !m_overviewSeen || m_overviewVisible != open;
      m_overviewSeen = true;
      m_overviewVisible = open;
    }
  }

  if (auto* outputs = locateArr(payload, "outputs")) {
    if (applyOutputList(*outputs)) changed = true;
  }
  if (applyLayoutSection(payload)) changed = true;
  if (auto* layout = locateObj(payload, "layout")) {
    if (applyLayoutSection(*layout)) changed = true;
  }
  if (auto* windows = locateArr(payload, "windows")) {
    if (applyWindowList(*windows)) changed = true;
  }
  return changed;
}

bool TriadWorkspaceManager::applyOutputList(nlohmann::json const& list) {
  if (!list.is_array()) return false;
  std::unordered_set<std::string> next;
  for (auto const& item : list) {
    auto name = extractStr(item, "name");
    if (!name.empty()) next.insert(std::move(name));
  }
  if (next == m_knownOutputs) return false;
  m_knownOutputs = std::move(next);
  return true;
}

bool TriadWorkspaceManager::applyLayoutSection(nlohmann::json const& section) {
  auto* list = locateArr(section, "workspaces");
  if (!list) return false;

  std::unordered_map<std::uint32_t, DeskState> next;
  for (auto const& item : *list) {
    auto ds = decodeDesk(item);
    if (ds && ds->idx > 0) next.emplace(ds->idx, *ds);
  }

  if (next.size() == m_catalog.size()) {
    bool same = true;
    for (auto const& [idx, ds] : next) {
      auto old = m_catalog.find(idx);
      if (old == m_catalog.end()
          || old->second.label != ds.label
          || old->second.monitor != ds.monitor
          || old->second.focused != ds.focused
          || old->second.globallyActive != ds.globallyActive
          || old->second.attention != ds.attention
          || old->second.hasContent != ds.hasContent
          || old->second.activeChild != ds.activeChild) {
        same = false; break;
      }
    }
    if (same) return false;
  }

  m_catalog = std::move(next);
  return true;
}

bool TriadWorkspaceManager::applyWindowList(nlohmann::json const& list) {
  if (!list.is_array()) return false;

  std::unordered_map<std::uint64_t, WindowEntry> next;
  for (auto const& item : list) {
    auto w = decodeWindow(item);
    if (w) next.emplace(w->id, *w);
  }

  if (next.size() == m_entries.size()) {
    bool same = true;
    for (auto const& [id, w] : next) {
      auto old = m_entries.find(id);
      if (old == m_entries.end()
          || old->second.parentWorkspace != w.parentWorkspace
          || old->second.monitor != w.monitor
          || old->second.app != w.app
          || old->second.caption != w.caption
          || old->second.col != w.col
          || old->second.row != w.row) {
        same = false; break;
      }
    }
    if (same) return false;
  }

  m_entries = std::move(next);
  return true;
}

bool TriadWorkspaceManager::applySingleWindow(nlohmann::json const& entry) {
  auto parsed = decodeWindow(entry);
  if (!parsed) return false;

  auto old = m_entries.find(parsed->id);
  if (old != m_entries.end()
      && old->second.parentWorkspace == parsed->parentWorkspace
      && old->second.monitor == parsed->monitor
      && old->second.app == parsed->app
      && old->second.caption == parsed->caption
      && old->second.col == parsed->col
      && old->second.row == parsed->row) {
    return false;
  }
  m_entries[parsed->id] = *parsed;
  return true;
}

std::optional<TriadWorkspaceManager::DeskState>
TriadWorkspaceManager::decodeDesk(nlohmann::json const& src) {
  if (!src.is_object()) return std::nullopt;

  auto idxIt = src.find("workspace_idx");
  if (idxIt == src.end()) return std::nullopt;
  auto idxVal = parseUnsigned(*idxIt);
  if (!idxVal || *idxVal == 0 || *idxVal > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;

  DeskState ds;
  ds.idx = static_cast<std::uint32_t>(*idxVal);
  auto tagIt = src.find("tag_id");
  if (tagIt != src.end()) ds.tag = parseUnsigned(*tagIt).value_or(0);
  ds.label = extractStr(src, "name");
  ds.monitor = extractStr(src, "output");
  ds.focused = extractBool(src, "is_output_visible");
  ds.globallyActive = extractBool(src, "is_active");
  ds.attention = extractBool(src, "is_urgent");
  ds.hasContent = extractBool(src, "occupied");
  auto focusIt = src.find("focused_window_id");
  if (focusIt != src.end() && !focusIt->is_null()) ds.activeChild = parseUnsigned(*focusIt);
  return ds;
}

std::optional<TriadWorkspaceManager::WindowEntry>
TriadWorkspaceManager::decodeWindow(nlohmann::json const& src) {
  if (!src.is_object()) return std::nullopt;

  auto idIt = src.find("id");
  auto wsIt = src.find("workspace_idx");
  if (idIt == src.end() || wsIt == src.end()) return std::nullopt;

  auto idVal = parseUnsigned(*idIt);
  auto wsVal = parseUnsigned(*wsIt);
  if (!idVal || !wsVal || *wsVal > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;

  WindowEntry w;
  w.id = *idVal;
  w.parentWorkspace = static_cast<std::uint32_t>(*wsVal);
  w.monitor = extractStr(src, "output");
  w.app = extractStr(src, "app_id");
  w.caption = extractStr(src, "title");

  auto* pos = locateObj(src, "position");
  if (pos) {
    auto col = pos->find("column_idx");
    if (col != pos->end()) w.col = parseInt32(*col).value_or(0);
    auto row = pos->find("window_idx");
    if (row != pos->end()) w.row = parseInt32(*row).value_or(0);
  }
  return w;
}

std::string TriadWorkspaceManager::makeKey(DeskState const& ds) {
  return ds.idx > 0 ? std::to_string(ds.idx) : std::string{};
}

bool TriadWorkspaceManager::isPhantom(DeskState const& ds) {
  return ds.monitor.rfind("triad-", 0) == 0 && !ds.focused && !ds.hasContent && !ds.attention;
}

std::optional<std::uint64_t> TriadWorkspaceManager::parseUnsigned(nlohmann::json const& v) {
  if (v.is_number_unsigned()) return v.get<std::uint64_t>();
  if (v.is_number_integer()) {
    auto signedVal = v.get<std::int64_t>();
    if (signedVal >= 0) return static_cast<std::uint64_t>(signedVal);
  }
  return std::nullopt;
}

std::optional<std::int32_t> TriadWorkspaceManager::parseInt32(nlohmann::json const& v) {
  if (v.is_number_integer()) {
    auto big = v.get<std::int64_t>();
    if (big >= std::numeric_limits<std::int32_t>::min() && big <= std::numeric_limits<std::int32_t>::max())
      return static_cast<std::int32_t>(big);
  }
  if (v.is_number_unsigned()) {
    auto big = v.get<std::uint64_t>();
    if (big <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
      return static_cast<std::int32_t>(big);
  }
  return std::nullopt;
}

std::string TriadWorkspaceManager::extractStr(nlohmann::json const& obj, char const* key) {
  if (!obj.is_object()) return {};
  auto it = obj.find(key);
  return (it != obj.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

bool TriadWorkspaceManager::extractBool(nlohmann::json const& obj, char const* key) {
  if (!obj.is_object()) return false;
  auto it = obj.find(key);
  return it != obj.end() && it->is_boolean() && it->get<bool>();
}

std::string TriadWorkspaceManager::resolveOutput(wl_output* output) const {
  return (output && m_lookup) ? m_lookup(output) : std::string{};
}

bool TriadWorkspaceManager::shouldShow(DeskState const& ds, std::string const& monitor) const {
  if (!monitor.empty()) return ds.monitor == monitor;
  if (isPhantom(ds)) return false;
  if (m_knownOutputs.empty() || ds.monitor.empty()) return true;
  if (m_knownOutputs.contains(ds.monitor)) return true;
  return ds.focused || ds.globallyActive || ds.hasContent || ds.attention;
}

std::vector<TriadWorkspaceManager::DeskState const*>
TriadWorkspaceManager::orderedDesks(std::string const& monitor) const {
  std::vector<DeskState const*> out;
  out.reserve(m_catalog.size());
  for (auto const& [idx, ds] : m_catalog) {
    (void)idx;
    if (!shouldShow(ds, monitor)) continue;
    out.push_back(&ds);
  }
  std::sort(out.begin(), out.end(), [](DeskState const* a, DeskState const* b) {
    if (a->idx != b->idx) return a->idx < b->idx;
    return a->tag < b->tag;
  });
  return out;
}

std::optional<std::uint32_t> TriadWorkspaceManager::parseIndex(std::string const& id) const {
  auto val = parseNumeric(id);
  if (!val || *val == 0 || *val > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
  return static_cast<std::uint32_t>(*val);
}

std::optional<std::uint64_t> TriadWorkspaceManager::parseNumeric(std::string const& s) {
  if (s.empty()) return std::nullopt;
  std::uint64_t val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec != std::errc{} || ptr != s.data() + s.size() || val == 0) return std::nullopt;
  return val;
}

void TriadWorkspaceManager::requestFresh() {
  auto resp = m_core.sendQuery("state");
  if (!resp) return;
  auto* triad = locateObj(*resp, "triad");
  if (!triad) return;
  auto stateIt = triad->find("state");
  if (stateIt != triad->end() && stateIt->is_object()) applyFullState(*stateIt);
}

void TriadWorkspaceManager::fireChange() { if (m_onChange) m_onChange(); }
void TriadWorkspaceManager::fireOverviewChange() { if (m_onOverviewChange) m_onOverviewChange(); }
