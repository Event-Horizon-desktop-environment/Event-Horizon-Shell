#include "backends/mango/mango_workspace_manager.h"
#include "backends/mango/mango_backends.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <poll.h>
#include <iterator>
#include <ranges>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace {

[[nodiscard]] std::string strField(nlohmann::json const& j, const char* k) {
  auto it = j.find(k);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

[[nodiscard]] std::int32_t intField(nlohmann::json const& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) { return 0; }
  if (it->is_number_integer()) { return it->get<std::int32_t>(); }
  if (it->is_number_unsigned()) { return static_cast<std::int32_t>(it->get<std::uint32_t>()); }
  return 0;
}

[[nodiscard]] bool boolField(nlohmann::json const& j, const char* k) {
  auto it = j.find(k);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}

[[nodiscard]] std::vector<std::uint32_t> tagArray(nlohmann::json const& j, const char* k) {
  std::vector<std::uint32_t> result;
  auto it = j.find(k);
  if (it == j.end() || !it->is_array()) { return result; }
  result.reserve(it->size());
  for (auto const& item : *it) {
    if (item.is_number_unsigned()) { result.push_back(item.get<std::uint32_t>()); }
    else if (item.is_number_integer()) {
      auto v = item.get<std::int32_t>();
      if (v > 0) { result.push_back(static_cast<std::uint32_t>(v)); }
    }
  }
  return result;
}

[[nodiscard]] bool writeAll(int fd, std::string_view data) {
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      return false;
    }
    off += static_cast<std::size_t>(n);
  }
  return true;
}

// Connected stream socket for the watch channel; -1 when any step refuses.
int dialMango(std::string const& path) {
  sockaddr_un addr{};
  if (path.size() >= sizeof(addr.sun_path)) { return -1; }

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return -1; }

  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

} // namespace

MangoWorkspaceManager::MangoWorkspaceManager(wspace::mango::MangoSession& rt) : m_backend(rt) {}

bool MangoWorkspaceManager::ready() const noexcept { return m_fd >= 0; }

void MangoWorkspaceManager::onStateChange(IDeskSource::Notify cb) { m_notify = std::move(cb); }

void MangoWorkspaceManager::setOutputResolver(IOutputResolver::Resolver r) { m_resolver = std::move(r); }

bool MangoWorkspaceManager::openConnection() {
  if (m_fd >= 0) { return true; }

  const bool fresh = openSocket();
  if (fresh) { fetchClients(); }
  return fresh;
}

bool MangoWorkspaceManager::openSocket() {
  auto const& path = m_backend.socketPath();
  if (path.empty()) { return false; }

  const int fd = dialMango(path);
  if (fd < 0) { return false; }

  if (!writeAll(fd, "watch all-monitors\n")) { ::close(fd); return false; }

  int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) { (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }

  m_fd = fd;
  return true;
}

void MangoWorkspaceManager::closeSocket() {
  if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
  m_buf.clear();
}

bool MangoWorkspaceManager::consumeLines() {
  bool handled = false;
  std::size_t pos;
  while ((pos = m_buf.find('\n')) != std::string::npos) {
    std::string line = m_buf.substr(0, pos);
    m_buf.erase(0, pos + 1);
    handled = processMsg(line) || handled;
  }
  return handled;
}

void MangoWorkspaceManager::drainSocket() {
  std::array<char, 8192> buf;
  bool changed = false;
  for (;;) {
    ssize_t n = ::recv(m_fd, buf.data(), buf.size(), MSG_DONTWAIT);
    if (n > 0) {
      m_buf.append(buf.data(), static_cast<std::size_t>(n));
      changed = consumeLines() || changed;
      continue;
    }
    if (n == 0) { closeSocket(); changed = true; break; }
    if (errno == EINTR) { continue; }
    if (errno != EAGAIN && errno != EWOULDBLOCK) { closeSocket(); changed = true; }
    break;
  }
  if (changed) { emitChange(); }
}

std::optional<std::unordered_map<std::string, MangoWorkspaceManager::OutputRecord>>
MangoWorkspaceManager::decodeMonitors(nlohmann::json const& payload) {
  auto it = payload.find("monitors");
  if (it == payload.end() || !it->is_array()) { return std::nullopt; }

  std::unordered_map<std::string, OutputRecord> next;
  next.reserve(it->size());
  for (auto const& mon : *it) {
    auto m = decodeMonitor(mon);
    if (m && !m->name.empty()) { next.try_emplace(m->name, std::move(*m)); }
  }
  return next;
}

bool MangoWorkspaceManager::processMsg(std::string_view line) {
  if (line.empty()) { return false; }

  auto parsed = nlohmann::json::parse(line, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) { return false; }

  auto monitors = decodeMonitors(parsed);
  if (!monitors) { return false; }

  m_outputData = std::move(*monitors);
  fetchClients();
  return true;
}

MangoWorkspaceManager::TagRecord MangoWorkspaceManager::parseTag(nlohmann::json const& t) {
  TagRecord tag{};
  tag.index = static_cast<std::uint32_t>(intField(t, "index"));
  tag.occupied = intField(t, "client_count") > 0;
  tag.active = boolField(t, "is_active");
  tag.urgent = boolField(t, "is_urgent");
  return tag;
}

std::string MangoWorkspaceManager::resolveClientId(nlohmann::json const& json) {
  auto idIt = json.find("id");
  if (idIt == json.end()) { return {}; }
  if (idIt->is_string()) { return idIt->get<std::string>(); }
  if (idIt->is_number_unsigned()) { return std::to_string(idIt->get<std::uint64_t>()); }
  if (idIt->is_number_integer()) { return std::to_string(idIt->get<std::int64_t>()); }
  return {};
}

void MangoWorkspaceManager::applyActiveTags(OutputRecord& st, std::vector<std::uint32_t> const& at) {
  // A one-entry list holding zero is mango's "nothing selected" and leaves
  // the per-tag flags alone; any other non-empty list wins.
  const bool nothingSelected = at.size() == 1 && at.front() == 0;
  if (at.empty() || nothingSelected) { return; }
  for (auto& tag : st.tags) {
    tag.active = std::find(at.begin(), at.end(), tag.index) != at.end();
  }
}

std::optional<MangoWorkspaceManager::OutputRecord>
MangoWorkspaceManager::decodeMonitor(nlohmann::json const& json) {
  if (!json.is_object()) { return std::nullopt; }

  OutputRecord st{};
  st.name = strField(json, "name");
  if (st.name.empty()) { return std::nullopt; }

  st.width = intField(json, "width");
  st.height = intField(json, "height");
  st.x = intField(json, "x");
  st.y = intField(json, "y");
  st.active = boolField(json, "active");

  auto tg = json.find("tags");
  if (tg != json.end() && tg->is_array()) {
    st.tags.reserve(tg->size());
    for (auto const& t : *tg) {
      if (!t.is_object()) { continue; }
      st.tags.push_back(parseTag(t));
    }
  }

  applyActiveTags(st, tagArray(json, "active_tags"));

  auto ac = json.find("active_client");
  if (ac != json.end() && ac->is_object()) {
    st.clientTitle = strField(*ac, "title");
    st.clientAppId = strField(*ac, "appid");
  }

  for (auto& tag : st.tags) { tag.focused = false; }
  if (ac != json.end() && ac->is_object() && !st.tags.empty()) {
    for (auto& tag : st.tags) {
      if (tag.active) { tag.focused = true; break; }
    }
  }

  return st;
}

void MangoWorkspaceManager::fetchClients() {
  auto resp = m_backend.ipcQuery("get all-clients");
  if (!resp || !resp->is_object()) { return; }

  auto it = resp->find("clients");
  if (it == resp->end() || !it->is_array()) { return; }

  m_clients = *it
    | std::views::transform([](nlohmann::json const& c) { return decodeClient(c); })
    | std::views::filter([](std::optional<ClientRecord> const& c) { return c.has_value(); })
    | std::views::transform([](std::optional<ClientRecord> c) { return std::move(*c); })
    | std::ranges::to<std::vector<ClientRecord>>();
}

std::optional<MangoWorkspaceManager::ClientRecord>
MangoWorkspaceManager::decodeClient(nlohmann::json const& json) {
  if (!json.is_object()) { return std::nullopt; }

  ClientRecord c{};
  c.id = resolveClientId(json);
  if (c.id.empty()) { return std::nullopt; }

  c.outName = strField(json, "monitor");
  c.appId = strField(json, "appid");
  c.title = strField(json, "title");
  c.focused = boolField(json, "is_focused");
  c.tags = tagArray(json, "tags");
  c.x = intField(json, "x");
  c.y = intField(json, "y");
  return c;
}

void MangoWorkspaceManager::jumpTo(const std::string& id) {
  if (!parseIdx(id).has_value()) { return; }

  auto const* cur = currentOutput();
  const bool crossMonitor = cur != nullptr && !cur->name.empty();
  const auto cmd = crossMonitor ? "viewcrossmon," + id + "," + cur->name : "view," + id;
  (void)m_backend.runCommand(cmd);
}

void MangoWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
  // One guard: either the id is bogus or the output has no name, and
  // jumpTo() re-checks both before issuing anything.
  const auto name = resolveName(output);
  if (name.empty() || !parseIdx(id).has_value()) { jumpTo(id); return; }

  const std::string command = "viewcrossmon," + id + "," + name;
  (void)m_backend.runCommand(command);
}

void MangoWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskSlot& ws) {
  const auto idx = idxFromRegion(ws);
  if (idx.has_value()) {
    const std::string target = std::format("{}", 1 + *idx);
    jumpToOnOutput(output, target);
  }
}

void MangoWorkspaceManager::bringToFront(const std::string& windowId) {
  if (windowId.empty()) { return; }
  const std::string cmd = "focusid client," + windowId;
  (void)m_backend.runCommand(cmd);
}

bool MangoWorkspaceManager::tagVisible(TagRecord const& tag) {
  return tag.occupied || tag.active;
}

std::vector<DeskSlot> MangoWorkspaceManager::regionsForTags(std::vector<TagRecord> const& tags) const {
  auto si = activeTagIdx(tags);
  std::vector<DeskSlot> result;
  result.reserve(tags.size());
  for (std::size_t i = 0; i < tags.size(); ++i) {
    if (!tagVisible(tags[i])) { continue; }
    result.push_back(buildRegion(tags[i], si.has_value() && i == *si));
  }
  return result;
}

std::vector<DeskSlot> MangoWorkspaceManager::allRegions() const {
  std::unordered_map<std::uint32_t, DeskSlot> byIdx;

  for (auto const& [_, st] : m_outputData) {
    for (auto& ws : regionsForTags(st.tags)) {
      byIdx[ws.ordinal] = std::move(ws);
    }
  }

  std::vector<DeskSlot> result;
  result.reserve(byIdx.size());
  for (auto& [_, ws] : byIdx) { result.push_back(std::move(ws)); }
  std::sort(result.begin(), result.end(),
    [](DeskSlot const& a, DeskSlot const& b) { return a.ordinal < b.ordinal; });
  return result;
}

std::vector<DeskSlot> MangoWorkspaceManager::regionsOnOutput(wl_output* output) const {
  if (output == nullptr) { return allRegions(); }

  auto name = resolveName(output);
  if (name.empty()) { return allRegions(); }

  auto it = m_outputData.find(name);
  if (it == m_outputData.end()) { return allRegions(); }

  return regionsForTags(it->second.tags);
}

std::string MangoWorkspaceManager::outputFilter(wl_output* output) const {
  if (output == nullptr) { return {}; }
  auto* st = findOutput(output);
  return st != nullptr ? st->name : std::string{};
}

std::unordered_map<std::string, std::vector<std::string>>
MangoWorkspaceManager::appsByDesk(wl_output* output) const {
  std::unordered_map<std::string, std::vector<std::string>> result;

  std::string filter = outputFilter(output);

  for (auto const& c : m_clients) {
    if (c.appId.empty()) { continue; }
    if (!filter.empty() && c.outName != filter) { continue; }
    for (auto const& tag : c.tags) {
      if (tag == 0) { continue; }
      auto& apps = result[std::to_string(tag)];
      if (std::find(apps.begin(), apps.end(), c.appId) == apps.end()) { apps.push_back(c.appId); }
    }
  }

  return result;
}

std::vector<ToplevelRef> MangoWorkspaceManager::windowsOnDesk(wl_output* output) const {
  std::vector<ToplevelRef> result;

  std::string filter = outputFilter(output);

  for (auto const& c : m_clients) {
    if (!filter.empty() && c.outName != filter) { continue; }
    auto liveTags = c.tags | std::views::filter([](std::uint32_t t) { return t != 0; });
    auto refs = liveTags | std::views::transform([&](std::uint32_t t) {
      return ToplevelRef{
        .handle = c.id, .deskKey = std::to_string(t),
        .app = c.appId, .title = c.title,
        .x = c.x, .y = c.y,
      };
    });
    std::ranges::copy(refs, std::back_inserter(result));
  }

  return result;
}

void MangoWorkspaceManager::teardown() {
  closeSocket();
  m_knownOutputs.clear();
  m_outputData.clear();
  m_clients.clear();
}

void MangoWorkspaceManager::outputAttached(wl_output* output) {
  if (output == nullptr) { return; }
  if (std::find(m_knownOutputs.begin(), m_knownOutputs.end(), output) != m_knownOutputs.end()) { return; }
  m_knownOutputs.push_back(output);
}

void MangoWorkspaceManager::outputDetached(wl_output* output) {
  m_knownOutputs.erase(std::remove(m_knownOutputs.begin(), m_knownOutputs.end(), output), m_knownOutputs.end());
}

int MangoWorkspaceManager::eventFd() const noexcept { return m_fd; }

int MangoWorkspaceManager::eventTimeout() const noexcept { return m_fd >= 0 ? -1 : 2000; }

void MangoWorkspaceManager::onEvent(short revents) {
  if (m_fd < 0) { (void)openConnection(); return; }

  constexpr short kFatal = POLLHUP | POLLERR | POLLNVAL;
  if ((revents & kFatal) == 0) {
    if ((revents & POLLIN) != 0) { drainSocket(); }
    return;
  }
  closeSocket();
  emitChange();
}

wl_output* MangoWorkspaceManager::chosenOutput() const {
  for (auto const& entry : m_outputData) {
    auto const& st = entry.second;
    if (!st.active) { continue; }
    auto match = std::ranges::find_if(m_knownOutputs,
      [&](wl_output* out) { return resolveName(out) == st.name; });
    if (match != m_knownOutputs.end()) { return *match; }
  }
  return nullptr;
}

std::optional<std::pair<std::string, std::string>>
MangoWorkspaceManager::focusedToplevel(wl_output* output) const {
  auto const* st = findOutput(output);
  st = (st != nullptr) ? st : currentOutput();
  if (st != nullptr) {
    return std::pair{st->clientTitle, st->clientAppId};
  }
  return std::nullopt;
}

void MangoWorkspaceManager::emitChange() {
  if (m_notify) { m_notify(); }
}

std::string MangoWorkspaceManager::resolveName(wl_output* output) const {
  if (output == nullptr || !m_resolver) { return {}; }
  return m_resolver(output);
}

MangoWorkspaceManager::OutputRecord* MangoWorkspaceManager::currentOutput() {
  auto pick = m_outputData | std::views::values;
  auto it = std::ranges::find_if(pick, [](OutputRecord const& st) { return st.active; });
  if (it != pick.end()) { return &*it; }
  return m_outputData.empty() ? nullptr : &m_outputData.begin()->second;
}

MangoWorkspaceManager::OutputRecord const* MangoWorkspaceManager::currentOutput() const {
  auto pick = m_outputData | std::views::values;
  auto it = std::ranges::find_if(pick, [](OutputRecord const& st) { return st.active; });
  if (it != pick.end()) { return &*it; }
  return m_outputData.empty() ? nullptr : &m_outputData.begin()->second;
}

MangoWorkspaceManager::OutputRecord const* MangoWorkspaceManager::findOutput(wl_output* output) const {
  auto name = resolveName(output);
  if (name.empty()) { return nullptr; }
  auto it = m_outputData.find(name);
  if (it == m_outputData.end()) { return nullptr; }
  return &it->second;
}

MangoWorkspaceManager::OutputRecord* MangoWorkspaceManager::findOutput(wl_output* output) {
  return const_cast<OutputRecord*>(std::as_const(*this).findOutput(output));
}

std::optional<std::size_t> MangoWorkspaceManager::idxFromRegion(DeskSlot const& ws) {
  if (!ws.path.empty()) { return static_cast<std::size_t>(ws.path[0]); }
  return parseIdx(ws.key.empty() ? ws.label : ws.key);
}

std::optional<std::size_t> MangoWorkspaceManager::parseIdx(std::string const& id) {
  if (id.empty()) { return std::nullopt; }

  std::size_t val = 0;
  auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), val);
  if (ec != std::errc{} || ptr != id.data() + id.size() || val == 0) { return std::nullopt; }
  return val - 1;
}

std::optional<std::size_t> MangoWorkspaceManager::activeTagIdx(std::vector<TagRecord> const& tags) const {
  std::vector<std::size_t> active;
  active.reserve(tags.size());
  for (std::size_t i = 0; i < tags.size(); ++i) {
    if (tags[i].active) { active.push_back(i); }
  }
  if (active.empty()) { return std::nullopt; }
  if (active.size() == 1) { return active.front(); }
  for (auto i : active) {
    if (tags[i].focused) { return i; }
  }
  return active.front();
}

DeskSlot MangoWorkspaceManager::buildRegion(TagRecord const& tag, bool shellActive) {
  DeskSlot slot;
  slot.key = std::to_string(tag.index);
  slot.label = slot.key;
  slot.path = {tag.index > 0 ? tag.index - 1 : 0};
  slot.ordinal = tag.index;
  slot.state = {.selected = shellActive, .urgent = tag.urgent, .occupied = tag.occupied};
  return slot;
}
