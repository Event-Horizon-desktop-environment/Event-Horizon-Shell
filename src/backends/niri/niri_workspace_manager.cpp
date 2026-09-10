#include "backends/niri/niri_workspace_manager.h"
#include "backends/niri/niri_backends.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <unordered_set>

namespace {

constexpr auto C_RetryInit = std::chrono::seconds(2);
constexpr auto C_RetryMax = std::chrono::seconds(30);
constexpr std::size_t C_BufMax = 1024U * 1024U;
constexpr std::string_view C_EventReq = "\"EventStream\"\n";

bool writeFully(int fd, std::string_view data) {
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      return false;
    }
    off += static_cast<std::size_t>(n);
  }
  return true;
}

std::optional<std::uint64_t> jsonU64(nlohmann::json const& j) {
  if (j.is_number_unsigned()) { return j.get<std::uint64_t>(); }
  if (j.is_number_integer()) {
    auto v = j.get<std::int64_t>();
    if (v >= 0) { return static_cast<std::uint64_t>(v); }
  }
  return std::nullopt;
}

std::optional<std::int32_t> jsonI32(nlohmann::json const& j) {
  if (j.is_number_integer()) {
    auto v = j.get<std::int64_t>();
    if (v < std::numeric_limits<std::int32_t>::min() || v > std::numeric_limits<std::int32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::int32_t>(v);
  }
  if (j.is_number_unsigned()) {
    auto v = j.get<std::uint64_t>();
    if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) { return std::nullopt; }
    return static_cast<std::int32_t>(v);
  }
  return std::nullopt;
}

std::optional<std::uint64_t> optU64(nlohmann::json const& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || it->is_null()) { return std::nullopt; }
  return jsonU64(*it);
}

std::string optStr(nlohmann::json const& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_string()) { return {}; }
  return it->get<std::string>();
}

nlohmann::json const* asArray(nlohmann::json const& p, const char* k) {
  if (p.is_array()) { return &p; }
  auto it = p.find(k);
  if (p.is_object() && it != p.end() && it->is_array()) { return &(*it); }
  return nullptr;
}

nlohmann::json const* asObject(nlohmann::json const& p, const char* k) {
  if (p.is_object()) {
    auto it = p.find(k);
    if (it != p.end() && it->is_object()) { return &(*it); }
    if (p.contains("id")) { return &p; }
  }
  return nullptr;
}

std::optional<bool> optBool(nlohmann::json const& p, const char* k) {
  if (!p.is_object()) { return std::nullopt; }
  auto it = p.find(k);
  if (it == p.end()) { return std::nullopt; }
  if (it->is_boolean()) { return it->get<bool>(); }
  if (it->is_string()) {
    auto v = it->get<std::string>();
    if (v == "open" || v == "opened" || v == "true") { return true; }
    if (v == "closed" || v == "false") { return false; }
  }
  return std::nullopt;
}

std::string titleOneLine(std::string_view text) {
  if (text.empty()) { return {}; }
  std::string out;
  out.reserve(text.size());
  bool pending = false;
  for (unsigned char ch : text) {
    if (ch == '\n' || ch == '\r' || ch == '\t' || ch == '\v' || ch == '\f' || std::isspace(ch) != 0) {
      pending = !out.empty();
      continue;
    }
    if (pending) { out.push_back(' '); pending = false; }
    out.push_back(static_cast<char>(ch));
  }
  return out;
}

} // namespace

NiriWorkspaceManager::NiriWorkspaceManager(NiriRuntime& rt) : m_backend(rt) {
  if (m_backend.canConnect()) { tryConnect(); }
}

NiriWorkspaceManager::~NiriWorkspaceManager() { teardown(); }

void NiriWorkspaceManager::onStateChange(wspace::IWorkspaceDataBackend::Notify cb) {
  m_notify = std::move(cb);
}

void NiriWorkspaceManager::onOverviewChange(wspace::IWorkspaceDataBackend::Notify cb) {
  m_viewNotify = std::move(cb);
}

bool NiriWorkspaceManager::supportsOverview() const noexcept { return m_backend.canConnect(); }

int NiriWorkspaceManager::eventTimeout() const noexcept {
  if (m_fd >= 0 || !m_backend.canConnect()) { return -1; }
  if (m_retryAt.time_since_epoch().count() == 0) { return 0; }
  auto rem = std::chrono::ceil<std::chrono::milliseconds>(
    m_retryAt - std::chrono::steady_clock::now()).count();
  return static_cast<int>(std::max<std::int64_t>(0, rem));
}

void NiriWorkspaceManager::onEvent(short revents) {
  if (!m_backend.canConnect()) { return; }
  if (m_fd < 0) { tryConnect(); return; }
  if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) { closeFd(true); return; }
  if ((revents & POLLIN) != 0) { readFd(); }
}

void NiriWorkspaceManager::sync(std::vector<DeskRegion>& ws, const std::string& outName) const {
  if (!m_backend.canConnect() || ws.empty() || m_workspaces.empty()) { return; }

  auto cand = sortedWorkspaceCandidates(outName);
  std::vector<WsState const*> matches(ws.size(), nullptr);
  std::unordered_map<std::uint64_t, bool> used;

  for (std::size_t i = 0; i < ws.size(); ++i) {
    auto pid = parseUnsigned(ws[i].id);
    auto pidx = parseLeadingNumber(ws[i].id);
    if (!pidx.has_value()) { pidx = parseLeadingNumber(ws[i].name); }

    auto pick = [&](auto pred) -> WsState const* {
      for (auto* c : cand) {
        if (used.contains(c->id) || !pred(*c)) { continue; }
        used.emplace(c->id, true);
        return c;
      }
      return nullptr;
    };

    if (pid.has_value()) { matches[i] = pick([&](WsState const& c) { return c.id == *pid; }); }
    if (matches[i] == nullptr && !ws[i].name.empty()) {
      matches[i] = pick([&](WsState const& c) { return c.name == ws[i].name; });
    }
    if (matches[i] == nullptr && pidx.has_value()) {
      matches[i] = pick([&](WsState const& c) { return static_cast<std::size_t>(c.idx) == *pidx; });
    }
  }

  if (!outName.empty()) {
    std::size_t next = 0;
    for (std::size_t i = 0; i < matches.size(); ++i) {
      if (matches[i] != nullptr) { continue; }
      while (next < cand.size() && used.contains(cand[next]->id)) { ++next; }
      if (next >= cand.size()) { break; }
      matches[i] = cand[next];
      used.emplace(cand[next]->id, true);
      ++next;
    }
  }

  for (std::size_t i = 0; i < ws.size(); ++i) {
    if (matches[i] != nullptr) {
      if (matches[i]->idx > 0) { ws[i].index = matches[i]->idx; }
      ws[i].occupied = m_occupancy.contains(matches[i]->id) && m_occupancy.at(matches[i]->id) > 0;
    } else {
      ws[i].index = 0;
      ws[i].occupied = false;
    }
  }
}

std::vector<NiriWorkspaceManager::WsState const*>
NiriWorkspaceManager::sortedWorkspaceCandidates(const std::string& outName) const {
  std::vector<WsState const*> cand;
  cand.reserve(m_workspaces.size());
  for (auto const& [id, ws] : m_workspaces) {
    (void)id;
    if (!outName.empty() && ws.output != outName) { continue; }
    cand.push_back(&ws);
  }
  std::sort(cand.begin(), cand.end(), [](WsState const* a, WsState const* b) {
    if (a->idx != b->idx) { return a->idx < b->idx; }
    return a->id < b->id;
  });
  return cand;
}

std::vector<std::string> NiriWorkspaceManager::deskKeys(const std::string& outName) const {
  auto cand = sortedWorkspaceCandidates(outName);
  std::vector<std::string> result;
  result.reserve(cand.size());
  for (auto* ws : cand) { result.push_back(workspaceKey(*ws)); }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
NiriWorkspaceManager::appsByDesk(const std::string& outName) const {
  std::unordered_map<std::uint64_t, WsState const*> byId;
  for (auto const& [id, ws] : m_workspaces) {
    if (!outName.empty() && ws.output != outName) { continue; }
    byId.emplace(id, &ws);
  }

  std::unordered_map<std::string, std::vector<std::string>> result;
  std::unordered_map<std::string, std::unordered_set<std::string>> seen;
  for (auto const& [wid, win] : m_windows) {
    (void)wid;
    if (!win.wsId.has_value() || win.appId.empty()) { continue; }
    auto it = byId.find(*win.wsId);
    if (it == byId.end()) { continue; }
    auto key = workspaceKey(*it->second);
    if (seen[key].insert(win.appId).second) { result[key].push_back(win.appId); }
  }
  return result;
}

std::vector<DeskWindow> NiriWorkspaceManager::windowsOnDesk(const std::string& outName) const {
  std::unordered_map<std::uint64_t, WsState const*> byId;
  for (auto const& [id, ws] : m_workspaces) {
    if (!outName.empty() && ws.output != outName) { continue; }
    byId.emplace(id, &ws);
  }

  std::vector<DeskWindow> result;
  result.reserve(m_windows.size());
  for (auto const& [wid, win] : m_windows) {
    (void)wid;
    if (!win.wsId.has_value() || win.appId.empty()) { continue; }
    auto it = byId.find(*win.wsId);
    if (it == byId.end()) { continue; }
    result.push_back(DeskWindow{
      .windowId = std::to_string(wid), .deskKey = workspaceKey(*it->second),
      .appId = win.appId, .title = win.title,
      .x = win.x, .y = win.y,
    });
  }
  return result;
}

void NiriWorkspaceManager::teardown() {
  closeFd(false);
  bool viewWasOpen = m_knownOverview && m_overviewOpen;
  m_windows.clear();
  m_workspaces.clear();
  m_knownOverview = false;
  m_overviewOpen = false;
  m_buf.clear();
  m_backoff = C_RetryInit;
  if (viewWasOpen) { emitOverviewChange(); }
}

void NiriWorkspaceManager::tryConnect() {
  auto const& path = m_backend.queryEndpoint();
  if (m_fd >= 0 || path.empty()) { return; }

  auto now = std::chrono::steady_clock::now();
  if (m_retryAt.time_since_epoch().count() != 0 && now < m_retryAt) { return; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { scheduleRetry(); return; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); scheduleRetry(); return; }
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd); scheduleRetry(); return;
  }

  if (!writeFully(fd, C_EventReq)) { ::close(fd); scheduleRetry(); return; }

  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) { (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }

  m_fd = fd;
  m_retryAt = {};
  m_backoff = C_RetryInit;
  m_buf.clear();
}

void NiriWorkspaceManager::closeFd(bool doReconnect) {
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  if (doReconnect) { scheduleRetry(); }
  else { m_retryAt = {}; }
}

void NiriWorkspaceManager::scheduleRetry() {
  m_retryAt = std::chrono::steady_clock::now() + m_backoff;
  m_backoff = std::min(m_backoff * 2, C_RetryMax);
}

void NiriWorkspaceManager::readFd() {
  std::array<char, 4096> buf{};
  for (;;) {
    ssize_t n = ::read(m_fd, buf.data(), buf.size());
    if (n > 0) {
      m_buf.insert(m_buf.end(), buf.begin(), buf.begin() + n);
      if (m_buf.size() > C_BufMax) { closeFd(true); m_buf.clear(); return; }
      continue;
    }
    if (n == 0) { closeFd(true); return; }
    if (errno == EINTR) { continue; }
    if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
    closeFd(true);
    return;
  }
  parseLines();
}

void NiriWorkspaceManager::parseLines() {
  auto start = m_buf.begin();
  for (auto it = m_buf.begin(); it != m_buf.end(); ++it) {
    if (*it != '\n') { continue; }
    std::string line(start, it);
    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
    if (!line.empty() && !dispatchMsg(line)) { m_buf.clear(); return; }
    start = std::next(it);
  }
  if (start != m_buf.begin()) { m_buf.erase(m_buf.begin(), start); }
}

bool NiriWorkspaceManager::dispatchMsg(std::string_view line) {
  nlohmann::json j;
  try { j = nlohmann::json::parse(line); }
  catch (nlohmann::json::exception const&) { return true; }

  if (!j.is_object()) { return true; }
  if (j.contains("Ok")) { return true; }
  if (j.contains("Err")) { closeFd(true); return false; }
  if (j.size() != 1) { return true; }

  auto it = j.begin();
  bool changed = false;
  auto const& k = it.key();
  auto const& v = it.value();

  if (k == "WorkspacesChanged") { changed = handleWorkspacesChanged(v); }
  else if (k == "WindowsChanged") { changed = handleWindowsChanged(v); }
  else if (k == "OverviewOpenedOrClosed") {
    if (handleOverviewChanged(v)) { emitOverviewChange(); }
    return true;
  }
  else if (k == "OverviewOpened") {
    if (handleOverviewChanged(nlohmann::json{{"is_open", true}})) { emitOverviewChange(); }
    return true;
  }
  else if (k == "OverviewClosed") {
    if (handleOverviewChanged(nlohmann::json{{"is_open", false}})) { emitOverviewChange(); }
    return true;
  }
  else if (k == "WindowOpenedOrChanged") { changed = handleWindowOpened(v); }
  else if (k == "WindowLayoutsChanged") { changed = handleWindowLayout(v); }
  else if (k == "WindowClosed") { changed = handleWindowClosed(v); }

  if (changed) { emitChange(); }
  return true;
}

bool NiriWorkspaceManager::handleWorkspacesChanged(nlohmann::json const& p) {
  auto* arr = asArray(p, "workspaces");
  if (arr == nullptr) { return false; }

  std::unordered_map<std::uint64_t, WsState> next;
  for (auto const& item : *arr) {
    auto parsed = parseWorkspace(item);
    if (parsed.has_value()) { next.emplace(parsed->id, *parsed); }
  }

  if (next == m_workspaces) { return false; }
  m_workspaces = std::move(next);
  return true;
}

bool NiriWorkspaceManager::handleWindowsChanged(nlohmann::json const& p) {
  auto* arr = asArray(p, "windows");
  if (arr == nullptr) { return false; }

  std::unordered_map<std::uint64_t, WinState> next;
  for (auto const& item : *arr) {
    auto parsed = parseWindow(item);
    if (parsed.has_value()) { next.emplace(parsed->first, parsed->second); }
  }

  if (next == m_windows) { return false; }
  bool membersChanged = !sameWindowMembership(next, m_windows);
  m_windows = std::move(next);
  if (membersChanged) { recomputeOccupancy(); }
  return membersChanged;
}

bool NiriWorkspaceManager::handleOverviewChanged(nlohmann::json const& p) {
  std::optional<bool> open;
  if (p.is_boolean()) { open = p.get<bool>(); }
  else if (p.is_string()) {
    auto v = p.get<std::string>();
    if (v == "open" || v == "opened" || v == "true") { open = true; }
    else if (v == "closed" || v == "false") { open = false; }
  }
  else if (p.is_object()) {
    open = optBool(p, "is_open");
    if (!open.has_value()) { open = optBool(p, "isOpen"); }
    if (!open.has_value()) { open = optBool(p, "open"); }
  }

  if (!open.has_value()) { return false; }
  bool changed = !m_knownOverview || m_overviewOpen != *open;
  m_knownOverview = true;
  m_overviewOpen = *open;
  return changed;
}

bool NiriWorkspaceManager::handleWindowOpened(nlohmann::json const& p) {
  auto* obj = asObject(p, "window");
  if (obj == nullptr) { return false; }

  auto id = optU64(*obj, "id");
  if (!id.has_value()) { return false; }

  auto it = m_windows.find(*id);
  WinState st = it != m_windows.end() ? it->second : WinState{};
  if (!applyWindowFields(*obj, st)) { return false; }
  if (it != m_windows.end() && it->second == st) { return false; }

  bool membersChanged = it == m_windows.end()
    ? (st.wsId.has_value() || !st.appId.empty())
    : !sameWindowMembership(it->second, st);
  m_windows[*id] = st;
  if (membersChanged) { recomputeOccupancy(); }
  return membersChanged;
}

bool NiriWorkspaceManager::handleWindowLayout(nlohmann::json const& p) {
  auto* changes = asArray(p, "changes");
  if (changes == nullptr) { return false; }

  bool changed = false;
  for (auto const& item : *changes) {
    if (!item.is_array() || item.size() < 2) { continue; }
    auto idOpt = jsonU64(item[0]);
    if (!idOpt.has_value()) { continue; }
    std::uint64_t id = *idOpt;
    auto const& layout = item[1];

    auto it = m_windows.find(id);
    if (it == m_windows.end()) { continue; }

    if (layout.contains("pos_in_scrolling_layout")) {
      auto const& pos = layout["pos_in_scrolling_layout"];
      if (pos.is_array() && pos.size() >= 2) {
        auto xo = jsonI32(pos[0]);
        auto yo = jsonI32(pos[1]);
        if (!xo.has_value() || !yo.has_value()) { continue; }
        if (it->second.x != *xo || it->second.y != *yo) {
          it->second.x = *xo; it->second.y = *yo;
          changed = true;
        }
      }
    }
  }
  return changed;
}

bool NiriWorkspaceManager::handleWindowClosed(nlohmann::json const& p) {
  std::optional<std::uint64_t> wid = jsonU64(p);
  if (!wid.has_value() && p.is_object()) {
    wid = optU64(p, "id");
    if (!wid.has_value()) { wid = optU64(p, "window_id"); }
  }
  if (!wid.has_value()) { return false; }

  if (m_windows.erase(*wid) == 0) { return false; }
  recomputeOccupancy();
  return true;
}

std::optional<NiriWorkspaceManager::WsState> NiriWorkspaceManager::parseWorkspace(nlohmann::json const& j) {
  if (!j.is_object()) { return std::nullopt; }
  auto id = optU64(j, "id");
  auto idx = optU64(j, "idx");
  if (!id.has_value() || !idx.has_value()) { return std::nullopt; }
  return WsState{
    .id = *id, .idx = static_cast<std::uint8_t>(*idx),
    .name = optStr(j, "name"), .output = optStr(j, "output"),
  };
}

std::optional<std::pair<std::uint64_t, NiriWorkspaceManager::WinState>>
NiriWorkspaceManager::parseWindow(nlohmann::json const& j) {
  if (!j.is_object()) { return std::nullopt; }
  auto id = optU64(j, "id");
  if (!id.has_value()) { return std::nullopt; }
  WinState st;
  (void)applyWindowFields(j, st);
  return std::pair{*id, st};
}

bool NiriWorkspaceManager::applyWindowFields(nlohmann::json const& j, WinState& st) {
  if (!j.is_object()) { return false; }

  bool changed = false;
  if (j.contains("workspace_id")) {
    auto wsId = optU64(j, "workspace_id");
    if (wsId.has_value()) { st.wsId = wsId; changed = true; }
  }

  if (j.contains("app_id") || j.contains("class")) {
    auto appId = optStr(j, "app_id");
    if (appId.empty()) { appId = optStr(j, "class"); }
    st.appId = std::move(appId);
    changed = true;
  }

  if (j.contains("title")) {
    st.title = titleOneLine(optStr(j, "title"));
    changed = true;
  }

  if (j.contains("layout")) {
    auto const& layout = j["layout"];
    if (layout.contains("pos_in_scrolling_layout")) {
      auto const& pos = layout["pos_in_scrolling_layout"];
      if (pos.is_array() && pos.size() >= 2) {
        st.x = pos[0].get<std::int32_t>();
        st.y = pos[1].get<std::int32_t>();
        changed = true;
      }
    }
  }

  return changed;
}

bool NiriWorkspaceManager::sameWindowMembership(WinState const& a, WinState const& b) noexcept {
  return a.wsId == b.wsId && a.appId == b.appId;
}

bool NiriWorkspaceManager::sameWindowMembership(
    std::unordered_map<std::uint64_t, WinState> const& a,
    std::unordered_map<std::uint64_t, WinState> const& b) noexcept {
  if (a.size() != b.size()) { return false; }
  for (auto const& [id, aw] : a) {
    auto it = b.find(id);
    if (it == b.end() || !sameWindowMembership(aw, it->second)) { return false; }
  }
  return true;
}

std::optional<std::uint64_t> NiriWorkspaceManager::parseUnsigned(const std::string& s) {
  if (s.empty()) { return std::nullopt; }
  std::uint64_t v = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || ptr != s.data() + s.size()) { return std::nullopt; }
  return v;
}

std::optional<std::size_t> NiriWorkspaceManager::parseLeadingNumber(const std::string& s) {
  if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front()))) { return std::nullopt; }
  std::size_t v = 0;
  std::size_t i = 0;
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    v = (v * 10) + static_cast<std::size_t>(s[i] - '0');
    ++i;
  }
  return v > 0 ? std::optional<std::size_t>(v) : std::nullopt;
}

std::string NiriWorkspaceManager::workspaceKey(WsState const& ws) {
  if (ws.idx > 0) { return std::to_string(ws.idx); }
  if (ws.id > 0) { return std::to_string(ws.id); }
  return {};
}

void NiriWorkspaceManager::recomputeOccupancy() {
  m_occupancy.clear();
  for (auto const& [id, win] : m_windows) {
    (void)id;
    if (win.wsId.has_value()) { ++m_occupancy[*win.wsId]; }
  }
}

void NiriWorkspaceManager::emitChange() const {
  if (m_notify) { m_notify(); }
}

void NiriWorkspaceManager::emitOverviewChange() const {
  if (m_viewNotify) { m_viewNotify(); }
}
