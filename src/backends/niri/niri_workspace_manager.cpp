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
#include <ranges>
#include <sys/socket.h>
#include <sys/un.h>
#include <tuple>
#include <unistd.h>

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
  if (!j.is_number_integer()) { return std::nullopt; }
  const auto v = j.get<std::int64_t>();
  if (v < 0) { return std::nullopt; }
  return static_cast<std::uint64_t>(v);
}

std::optional<std::int32_t> jsonI32(nlohmann::json const& j) {
  constexpr auto lo = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min());
  constexpr auto hi = static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
  std::int64_t v = 0;
  if (j.is_number_integer()) {
    v = j.get<std::int64_t>();
  } else if (j.is_number_unsigned()) {
    const auto u = j.get<std::uint64_t>();
    if (u > static_cast<std::uint64_t>(hi)) { return std::nullopt; }
    return static_cast<std::int32_t>(u);
  } else {
    return std::nullopt;
  }
  if (v < lo || v > hi) { return std::nullopt; }
  return static_cast<std::int32_t>(v);
}

std::optional<std::uint64_t> optU64(nlohmann::json const& j, const char* k) {
  if (!j.is_object()) { return std::nullopt; }
  const auto it = j.find(k);
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
  if (!p.is_object()) { return nullptr; }
  const auto it = p.find(k);
  if (it == p.end() || !it->is_array()) { return nullptr; }
  return &(*it);
}

nlohmann::json const* asObject(nlohmann::json const& p, const char* k) {
  if (p.is_object()) {
    auto it = p.find(k);
    if (it != p.end() && it->is_object()) { return &(*it); }
    if (p.contains("id")) { return &p; }
  }
  return nullptr;
}

// niri spells the overview state either as a boolean or as one of these words.
std::optional<bool> wordState(std::string_view v) {
  static constexpr std::pair<std::string_view, bool> kStates[] = {
    {"false", false}, {"closed", false},
    {"true", true}, {"opened", true}, {"open", true},
  };
  for (auto [word, state] : kStates) {
    if (v == word) { return state; }
  }
  return std::nullopt;
}

std::optional<bool> optBool(nlohmann::json const& p, const char* k) {
  if (!p.is_object()) { return std::nullopt; }
  const auto it = p.find(k);
  if (it == p.end()) { return std::nullopt; }
  if (it->is_boolean()) { return it->get<bool>(); }
  if (!it->is_string()) { return std::nullopt; }
  return wordState(it->get<std::string>());
}

// Decodes every element of the array under `key` into an id -> state map.
// Returns false when the payload carries no such array.
template <typename State, typename Decoder>
bool decodeIdMap(nlohmann::json const& payload, const char* key,
                 std::unordered_map<std::uint64_t, State>& out, Decoder&& decode) {
  auto* arr = asArray(payload, key);
  if (arr == nullptr) { return false; }

  std::unordered_map<std::uint64_t, State> fresh;
  fresh.reserve(arr->size());
  for (auto const& item : *arr) {
    if (auto decoded = decode(item); decoded.has_value()) {
      fresh.try_emplace(decoded->first, std::move(decoded->second));
    }
  }
  out = std::move(fresh);
  return true;
}

std::string titleOneLine(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  auto isGap = [](char ch) {
    switch (ch) {
      case ' ':
      case '\n':
      case '\r':
      case '\t':
      case '\v':
      case '\f':
        return true;
      default:
        return std::isspace(static_cast<unsigned char>(ch)) != 0;
    }
  };
  std::size_t i = 0;
  const std::size_t n = text.size();
  while (i < n) {
    while (i < n && isGap(text[i])) { ++i; }
    const std::size_t word = i;
    while (i < n && !isGap(text[i])) { ++i; }
    if (i == word) { break; }
    if (!out.empty()) { out += ' '; }
    out.append(text.substr(word, i - word));
  }
  return out;
}

// A usable workspace entry carries both its numeric id and its index.
std::optional<std::pair<std::uint64_t, std::uint64_t>> workspaceOrdinals(nlohmann::json const& j) {
  const auto id = optU64(j, "id");
  const auto idx = optU64(j, "idx");
  if (!id.has_value() || !idx.has_value()) { return std::nullopt; }
  return std::pair{*id, *idx};
}

} // namespace

NiriWorkspaceManager::NiriWorkspaceManager(NiriSession& rt) : m_backend(rt) {
  if (m_backend.canConnect()) { tryConnect(); }
}

NiriWorkspaceManager::~NiriWorkspaceManager() { teardown(); }

void NiriWorkspaceManager::onStateChange(wspace::IDeskCatalog::Notify cb) {
  m_notify = std::move(cb);
}

void NiriWorkspaceManager::onOverviewChange(wspace::IDeskCatalog::Notify cb) {
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

NiriWorkspaceManager::WsState const*
NiriWorkspaceManager::claimMatching(std::vector<WsState const*> const& cand,
                                    std::unordered_map<std::uint64_t, bool>& used,
                                    DeskSlot const& row, std::optional<std::uint64_t> const& byId,
                                    std::optional<std::size_t> const& byIdx) {
  auto takeIf = [&](WsState const* c, bool want) -> WsState const* {
    if (!want || used.contains(c->id)) { return nullptr; }
    used.emplace(c->id, true);
    return c;
  };
  if (byId.has_value()) {
    for (WsState const* c : cand) {
      if (WsState const* hit = takeIf(c, c->id == *byId)) { return hit; }
    }
  }
  if (!row.label.empty()) {
    for (WsState const* c : cand) {
      if (WsState const* hit = takeIf(c, c->name == row.label)) { return hit; }
    }
  }
  if (byIdx.has_value()) {
    for (WsState const* c : cand) {
      if (WsState const* hit = takeIf(c, static_cast<std::size_t>(c->idx) == *byIdx)) { return hit; }
    }
  }
  return nullptr;
}

void NiriWorkspaceManager::sync(std::vector<DeskSlot>& ws, const std::string& outName) const {
  if (!m_backend.canConnect() || ws.empty() || m_workspaces.empty()) { return; }

  const auto cand = sortedWorkspaceCandidates(outName);
  std::vector<WsState const*> matches(ws.size(), nullptr);
  std::unordered_map<std::uint64_t, bool> used;

  for (std::size_t i = 0; i < ws.size(); ++i) {
    const auto idNum = parseUint(ws[i].key);
    auto idxNum = parseLeadingDigits(ws[i].key);
    if (!idxNum.has_value()) { idxNum = parseLeadingDigits(ws[i].label); }
    matches[i] = claimMatching(cand, used, ws[i], idNum, idxNum);
  }

  if (!outName.empty()) {
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < matches.size(); ++i) {
      if (matches[i] != nullptr) { continue; }
      while (cursor < cand.size() && used.contains(cand[cursor]->id)) { ++cursor; }
      if (cursor >= cand.size()) { break; }
      matches[i] = cand[cursor];
      used.emplace(cand[cursor]->id, true);
      ++cursor;
    }
  }

  for (std::size_t i = 0; i < ws.size(); ++i) {
    if (matches[i] == nullptr) {
      ws[i].ordinal = 0;
      ws[i].state.occupied = false;
      continue;
    }
    if (matches[i]->idx > 0) { ws[i].ordinal = matches[i]->idx; }
    const bool live =
        m_occupancy.contains(matches[i]->id) && m_occupancy.at(matches[i]->id) > 0;
    ws[i].state.occupied = live;
  }
}

std::vector<NiriWorkspaceManager::WsState const*>
NiriWorkspaceManager::sortedWorkspaceCandidates(const std::string& outName) const {
  auto onOutput = [&outName](WsState const& ws) {
    return outName.empty() || ws.output == outName;
  };

  std::vector<WsState const*> cand;
  cand.reserve(m_workspaces.size());
  for (auto const& [id, ws] : m_workspaces) {
    (void)id;
    if (onOutput(ws)) { cand.push_back(&ws); }
  }
  std::ranges::sort(cand, [](WsState const* a, WsState const* b) {
    return std::tie(a->idx, a->id) < std::tie(b->idx, b->id);
  });
  return cand;
}

std::vector<std::string> NiriWorkspaceManager::deskKeys(const std::string& outName) const {
  const auto cand = sortedWorkspaceCandidates(outName);
  std::vector<std::string> keys(cand.size());
  std::ranges::transform(cand, keys.begin(),
    [](WsState const* ws) { return deskFileKey(*ws); });
  return keys;
}

std::unordered_map<std::uint64_t, std::string>
NiriWorkspaceManager::deskKeysByWorkspaceId(const std::string& outName) const {
  std::unordered_map<std::uint64_t, std::string> index;
  index.reserve(m_workspaces.size());
  for (auto const& [id, ws] : m_workspaces) {
    if (!outName.empty() && ws.output != outName) { continue; }
    index.emplace(id, deskFileKey(ws));
  }
  return index;
}

std::unordered_map<std::string, std::vector<std::string>>
NiriWorkspaceManager::appsByDesk(const std::string& outName) const {
  const auto keys = deskKeysByWorkspaceId(outName);

  std::unordered_map<std::string, std::vector<std::string>> result;
  for (auto const& [wid, win] : m_windows) {
    if (win.appId.empty() || !win.wsId.has_value()) { continue; }
    auto key = keys.find(*win.wsId);
    if (key == keys.end()) { continue; }
    auto& apps = result[key->second];
    if (std::ranges::find(apps, win.appId) == apps.end()) { apps.push_back(win.appId); }
  }
  return result;
}

std::vector<ToplevelRef> NiriWorkspaceManager::windowsOnDesk(const std::string& outName) const {
  const auto keys = deskKeysByWorkspaceId(outName);

  std::vector<ToplevelRef> windows;
  windows.reserve(m_windows.size());
  for (auto const& [wid, win] : m_windows) {
    if (win.appId.empty() || !win.wsId.has_value()) { continue; }
    auto key = keys.find(*win.wsId);
    if (key == keys.end()) { continue; }
    windows.push_back(windowRef(wid, win, key->second));
  }
  return windows;
}

ToplevelRef NiriWorkspaceManager::windowRef(std::uint64_t wid, WinState const& win,
                                            std::string const& deskKey) {
  return ToplevelRef{
    .handle = std::to_string(wid), .deskKey = deskKey,
    .app = win.appId, .title = win.title,
    .x = win.x, .y = win.y,
  };
}

void NiriWorkspaceManager::teardown() {
  const bool viewWasOpen = m_knownOverview && m_overviewOpen;
  closeFd(false);
  resetState();
  if (viewWasOpen) { emitOverviewChange(); }
}

void NiriWorkspaceManager::resetState() {
  decltype(m_windows){}.swap(m_windows);
  decltype(m_workspaces){}.swap(m_workspaces);
  decltype(m_buf){}.swap(m_buf);
  m_backoff = C_RetryInit;
  m_knownOverview = false;
  m_overviewOpen = false;
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

  if (k == "WorkspacesChanged") { changed = noteWorkspacesChanged(v); }
  else if (k == "WindowsChanged") { changed = noteWindowsChanged(v); }
  else if (k == "OverviewOpenedOrClosed") {
    if (noteOverviewChanged(v)) { emitOverviewChange(); }
    return true;
  }
  else if (k == "OverviewOpened") {
    if (noteOverviewChanged(nlohmann::json{{"is_open", true}})) { emitOverviewChange(); }
    return true;
  }
  else if (k == "OverviewClosed") {
    if (noteOverviewChanged(nlohmann::json{{"is_open", false}})) { emitOverviewChange(); }
    return true;
  }
  else if (k == "WindowOpenedOrChanged") { changed = handleWindowOpened(v); }
  else if (k == "WindowLayoutsChanged") { changed = handleWindowLayout(v); }
  else if (k == "WindowClosed") { changed = noteWindowClosed(v); }

  if (changed) { emitChange(); }
  return true;
}

bool NiriWorkspaceManager::noteWorkspacesChanged(nlohmann::json const& p) {
  std::unordered_map<std::uint64_t, WsState> fresh;
  auto decode = [](nlohmann::json const& item) { return decodeWorkspace(item); };
  if (!decodeIdMap(p, "workspaces", fresh, decode)) { return false; }

  if (fresh == m_workspaces) { return false; }
  m_workspaces = std::move(fresh);
  return true;
}

bool NiriWorkspaceManager::noteWindowsChanged(nlohmann::json const& p) {
  std::unordered_map<std::uint64_t, WinState> fresh;
  auto decode = [](nlohmann::json const& item) { return decodeWindow(item); };
  if (!decodeIdMap(p, "windows", fresh, decode)) { return false; }

  if (fresh == m_windows) { return false; }
  const bool desksShifted = !sameWindowMembership(fresh, m_windows);
  m_windows = std::move(fresh);
  if (desksShifted) { refreshOccupancy(); }
  return desksShifted;
}

bool NiriWorkspaceManager::noteOverviewChanged(nlohmann::json const& p) {
  std::optional<bool> open;
  if (p.is_object()) {
    for (const char* alias : {"is_open", "isOpen", "open"}) {
      open = optBool(p, alias);
      if (open.has_value()) { break; }
    }
  }
  else if (p.is_string()) { open = wordState(p.get<std::string>()); }
  else if (p.is_boolean()) { open = p.get<bool>(); }

  if (!open.has_value()) { return false; }

  const bool fresh = !m_knownOverview;
  const bool flipped = m_overviewOpen != *open;
  m_knownOverview = true;
  m_overviewOpen = *open;
  return fresh || flipped;
}

bool NiriWorkspaceManager::handleWindowOpened(nlohmann::json const& p) {
  auto* obj = asObject(p, "window");
  if (obj == nullptr) { return false; }

  auto id = optU64(*obj, "id");
  if (!id.has_value()) { return false; }

  auto it = m_windows.find(*id);
  WinState st = it != m_windows.end() ? it->second : WinState{};
  if (!mergeWindowFields(*obj, st)) { return false; }
  if (it != m_windows.end() && it->second == st) { return false; }

  bool membersChanged = it == m_windows.end()
    ? (st.wsId.has_value() || !st.appId.empty())
    : !sameWindowMembership(it->second, st);
  m_windows[*id] = st;
  if (membersChanged) { refreshOccupancy(); }
  return membersChanged;
}

bool NiriWorkspaceManager::applyLayoutItem(nlohmann::json const& item) {
  if (!item.is_array() || item.size() < 2) { return false; }
  const auto id = jsonU64(item[0]);
  if (!id.has_value()) { return false; }
  const auto it = m_windows.find(*id);
  if (it == m_windows.end()) { return false; }
  const auto& layout = item[1];
  if (!layout.contains("pos_in_scrolling_layout")) { return false; }
  const auto& pos = layout["pos_in_scrolling_layout"];
  if (!pos.is_array() || pos.size() < 2) { return false; }
  const auto xo = jsonI32(pos[0]);
  const auto yo = jsonI32(pos[1]);
  if (!xo.has_value() || !yo.has_value()) { return false; }
  WinState& st = it->second;
  if (st.x == *xo && st.y == *yo) { return false; }
  st.x = *xo;
  st.y = *yo;
  return true;
}

bool NiriWorkspaceManager::handleWindowLayout(nlohmann::json const& p) {
  const auto* changes = asArray(p, "changes");
  if (changes == nullptr) { return false; }
  bool changed = false;
  for (const auto& item : *changes) {
    if (applyLayoutItem(item)) { changed = true; }
  }
  return changed;
}

bool NiriWorkspaceManager::noteWindowClosed(nlohmann::json const& p) {
  std::optional<std::uint64_t> wid = jsonU64(p);
  if (!wid.has_value() && p.is_object()) {
    wid = optU64(p, "id");
    if (!wid.has_value()) { wid = optU64(p, "window_id"); }
  }
  auto gone = wid.has_value() ? m_windows.find(*wid) : m_windows.end();
  if (gone == m_windows.end()) { return false; }
  m_windows.erase(gone);
  refreshOccupancy();
  return true;
}

std::optional<std::pair<std::uint64_t, NiriWorkspaceManager::WsState>>
NiriWorkspaceManager::decodeWorkspace(nlohmann::json const& j) {
  if (!j.is_object()) { return std::nullopt; }
  const auto ord = workspaceOrdinals(j);
  if (!ord.has_value()) { return std::nullopt; }

  WsState st;
  st.output = optStr(j, "output");
  st.name = optStr(j, "name");
  st.idx = static_cast<std::uint8_t>(ord->second);
  st.id = ord->first;
  return std::pair{ord->first, std::move(st)};
}

std::optional<std::pair<std::uint64_t, NiriWorkspaceManager::WinState>>
NiriWorkspaceManager::decodeWindow(nlohmann::json const& j) {
  if (!j.is_object()) { return std::nullopt; }
  auto id = optU64(j, "id");
  if (!id.has_value()) { return std::nullopt; }

  auto entry = std::pair{*id, WinState{}};
  (void)mergeWindowFields(j, entry.second);
  return entry;
}

bool NiriWorkspaceManager::mergeWindowFields(nlohmann::json const& j, WinState& st) {
  if (!j.is_object()) { return false; }

  bool changed = false;
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

  if (j.contains("title")) {
    st.title = titleOneLine(optStr(j, "title"));
    changed = true;
  }

  if (j.contains("app_id") || j.contains("class")) {
    auto appId = optStr(j, "app_id");
    if (appId.empty()) { appId = optStr(j, "class"); }
    st.appId = std::move(appId);
    changed = true;
  }

  if (j.contains("workspace_id")) {
    auto wsId = optU64(j, "workspace_id");
    if (wsId.has_value()) { st.wsId = wsId; changed = true; }
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

std::optional<std::uint64_t> NiriWorkspaceManager::parseUint(const std::string& s) {
  if (s.empty()) { return std::nullopt; }
  std::uint64_t v = 0;
  const char* first = s.data();
  const auto res = std::from_chars(first, first + s.size(), v);
  const bool full = res.ec == std::errc{} && res.ptr == first + s.size();
  return full ? std::optional<std::uint64_t>(v) : std::nullopt;
}

std::optional<std::size_t> NiriWorkspaceManager::parseLeadingDigits(const std::string& s) {
  auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
  if (s.empty() || !isDigit(s.front())) { return std::nullopt; }
  std::size_t v = 0;
  const auto runEnd = std::find_if_not(s.begin(), s.end(), isDigit);
  for (char c : std::ranges::subrange(s.begin(), runEnd)) {
    v = v * 10 + static_cast<std::size_t>(c - '0');
  }
  return v > 0 ? std::optional<std::size_t>(v) : std::nullopt;
}

std::string NiriWorkspaceManager::deskFileKey(WsState const& ws) {
  const std::uint64_t primary = ws.idx > 0 ? ws.idx : ws.id;
  return primary > 0 ? std::to_string(primary) : std::string{};
}

void NiriWorkspaceManager::refreshOccupancy() {
  m_occupancy.clear();
  auto parked = m_windows | std::views::values
              | std::views::filter([](WinState const& w) { return w.wsId.has_value(); });
  for (auto const& win : parked) { ++m_occupancy[*win.wsId]; }
}

void NiriWorkspaceManager::emitChange() const {
  if (m_notify) { m_notify(); }
}

void NiriWorkspaceManager::emitOverviewChange() const {
  if (m_viewNotify) { m_viewNotify(); }
}
