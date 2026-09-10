#include "backends/mango/mango_workspace_manager.h"
#include "backends/mango/mango_backends.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
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

} // namespace

MangoWorkspaceManager::MangoWorkspaceManager(wspace::mango::MangoRuntime& rt) : m_backend(rt) {}

bool MangoWorkspaceManager::ready() const noexcept { return m_fd >= 0; }

void MangoWorkspaceManager::onStateChange(IWorkspaceManager::Notify cb) { m_notify = std::move(cb); }

void MangoWorkspaceManager::setOutputResolver(IOutputNameLookup::Resolver r) { m_resolver = std::move(r); }

bool MangoWorkspaceManager::openConnection() {
  if (m_fd >= 0) { return true; }
  if (!connectSocket()) { return false; }
  fetchClients();
  return true;
}

bool MangoWorkspaceManager::connectSocket() {
  auto const& path = m_backend.socketPath();
  if (path.empty()) { return false; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return false; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); return false; }
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return false;
  }

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

void MangoWorkspaceManager::drainSocket() {
  std::array<char, 8192> buf;
  bool changed = false;
  for (;;) {
    ssize_t n = ::recv(m_fd, buf.data(), buf.size(), MSG_DONTWAIT);
    if (n > 0) {
      m_buf.append(buf.data(), static_cast<std::size_t>(n));
      std::size_t pos;
      while ((pos = m_buf.find('\n')) != std::string::npos) {
        std::string line = m_buf.substr(0, pos);
        m_buf.erase(0, pos + 1);
        changed = processMsg(line) || changed;
      }
      continue;
    }
    if (n == 0) { closeSocket(); changed = true; break; }
    if (errno == EINTR) { continue; }
    if (errno != EAGAIN && errno != EWOULDBLOCK) { closeSocket(); changed = true; }
    break;
  }
  if (changed) { emitChange(); }
}

bool MangoWorkspaceManager::processMsg(std::string_view line) {
  if (line.empty()) { return false; }

  nlohmann::json parsed;
  try { parsed = nlohmann::json::parse(line); }
  catch (nlohmann::json::exception const&) { return false; }

  if (!parsed.is_object()) { return false; }

  auto it = parsed.find("monitors");
  if (it == parsed.end() || !it->is_array()) { return false; }

  std::unordered_map<std::string, OutputState> next;
  for (auto const& mon : *it) {
    auto m = decodeMonitor(mon);
    if (m.has_value() && !m->name.empty()) { next.emplace(m->name, std::move(*m)); }
  }

  m_outputData = std::move(next);
  fetchClients();
  return true;
}

std::optional<MangoWorkspaceManager::OutputState>
MangoWorkspaceManager::decodeMonitor(nlohmann::json const& json) {
  if (!json.is_object()) { return std::nullopt; }

  OutputState st{};
  st.name = strField(json, "name");
  if (st.name.empty()) { return std::nullopt; }

  st.active = boolField(json, "active");
  st.x = intField(json, "x");
  st.y = intField(json, "y");
  st.width = intField(json, "width");
  st.height = intField(json, "height");

  auto ac = json.find("active_client");
  if (ac != json.end() && ac->is_object()) {
    st.clientTitle = strField(*ac, "title");
    st.clientAppId = strField(*ac, "appid");
  }

  auto tg = json.find("tags");
  if (tg != json.end() && tg->is_array()) {
    st.tags.reserve(tg->size());
    for (auto const& t : *tg) {
      if (!t.is_object()) { continue; }
      TagInfo tag{};
      tag.index = static_cast<std::uint32_t>(intField(t, "index"));
      tag.active = boolField(t, "is_active");
      tag.urgent = boolField(t, "is_urgent");
      tag.occupied = intField(t, "client_count") > 0;
      st.tags.push_back(tag);
    }
  }

  auto at = tagArray(json, "active_tags");
  if (!at.empty() && !(at.size() == 1 && at.front() == 0)) {
    for (auto& tag : st.tags) {
      tag.active = std::find(at.begin(), at.end(), tag.index) != at.end();
    }
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
  auto resp = m_backend.request("get all-clients");
  if (!resp.has_value() || !resp->is_object()) { return; }

  auto it = resp->find("clients");
  if (it == resp->end() || !it->is_array()) { return; }

  std::vector<ClientState> next;
  next.reserve(it->size());
  for (auto const& c : *it) {
    auto cl = decodeClient(c);
    if (cl.has_value()) { next.push_back(std::move(*cl)); }
  }
  m_clients = std::move(next);
}

std::optional<MangoWorkspaceManager::ClientState>
MangoWorkspaceManager::decodeClient(nlohmann::json const& json) {
  if (!json.is_object()) { return std::nullopt; }

  ClientState c{};
  auto idIt = json.find("id");
  if (idIt != json.end()) {
    if (idIt->is_number_unsigned()) { c.id = std::to_string(idIt->get<std::uint64_t>()); }
    else if (idIt->is_number_integer()) { c.id = std::to_string(idIt->get<std::int64_t>()); }
    else if (idIt->is_string()) { c.id = idIt->get<std::string>(); }
  }
  if (c.id.empty()) { return std::nullopt; }

  c.title = strField(json, "title");
  c.appId = strField(json, "appid");
  c.outName = strField(json, "monitor");
  c.tags = tagArray(json, "tags");
  c.focused = boolField(json, "is_focused");
  c.x = intField(json, "x");
  c.y = intField(json, "y");
  return c;
}

void MangoWorkspaceManager::jumpTo(const std::string& id) {
  if (!parseIdx(id).has_value()) { return; }

  auto* cur = currentOutput();
  if (cur != nullptr && !cur->name.empty()) {
    (void)m_backend.dispatch("viewcrossmon," + id + "," + cur->name);
    return;
  }
  (void)m_backend.dispatch("view," + id);
}

void MangoWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
  if (!parseIdx(id).has_value()) { return; }

  auto name = resolveName(output);
  if (!name.empty()) {
    (void)m_backend.dispatch("viewcrossmon," + id + "," + name);
    return;
  }
  jumpTo(id);
}

void MangoWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskRegion& ws) {
  auto idx = idxFromRegion(ws);
  if (!idx.has_value()) { return; }
  jumpToOnOutput(output, std::to_string(*idx + 1));
}

void MangoWorkspaceManager::bringToFront(const std::string& windowId) {
  if (!windowId.empty()) { (void)m_backend.dispatch("focusid client," + windowId); }
}

std::vector<DeskRegion> MangoWorkspaceManager::allRegions() const {
  std::unordered_map<std::uint32_t, DeskRegion> byIdx;

  for (auto const& [_, st] : m_outputData) {
    auto si = activeTagIdx(st.tags);
    for (std::size_t i = 0; i < st.tags.size(); ++i) {
      auto const& tag = st.tags[i];
      if (!tag.occupied && !tag.active) { continue; }
      bool isActive = si.has_value() && i == *si;
      byIdx[tag.index] = buildRegion(tag, isActive);
    }
  }

  std::vector<DeskRegion> result;
  result.reserve(byIdx.size());
  for (auto& [_, ws] : byIdx) { result.push_back(std::move(ws)); }
  std::sort(result.begin(), result.end(),
    [](DeskRegion const& a, DeskRegion const& b) { return a.index < b.index; });
  return result;
}

std::vector<DeskRegion> MangoWorkspaceManager::regionsOnOutput(wl_output* output) const {
  if (output == nullptr) { return allRegions(); }

  auto name = resolveName(output);
  if (name.empty()) { return allRegions(); }

  auto it = m_outputData.find(name);
  if (it == m_outputData.end()) { return allRegions(); }

  auto const& st = it->second;
  auto si = activeTagIdx(st.tags);

  std::vector<DeskRegion> result;
  result.reserve(st.tags.size());
  for (std::size_t i = 0; i < st.tags.size(); ++i) {
    auto const& tag = st.tags[i];
    if (!tag.occupied && !tag.active) { continue; }
    result.push_back(buildRegion(tag, si.has_value() && i == *si));
  }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
MangoWorkspaceManager::appsByDesk(wl_output* output) const {
  std::unordered_map<std::string, std::vector<std::string>> result;

  auto* st = output != nullptr ? findOutput(output) : nullptr;
  std::string filter = st != nullptr ? st->name : std::string{};

  for (auto const& c : m_clients) {
    if (!filter.empty() && c.outName != filter) { continue; }
    if (c.appId.empty()) { continue; }
    for (auto const& tag : c.tags) {
      if (tag == 0) { continue; }
      auto& apps = result[std::to_string(tag)];
      if (std::find(apps.begin(), apps.end(), c.appId) == apps.end()) { apps.push_back(c.appId); }
    }
  }

  return result;
}

std::vector<DeskWindow> MangoWorkspaceManager::windowsOnDesk(wl_output* output) const {
  std::vector<DeskWindow> result;

  auto* st = output != nullptr ? findOutput(output) : nullptr;
  std::string filter = st != nullptr ? st->name : std::string{};

  for (auto const& c : m_clients) {
    if (!filter.empty() && c.outName != filter) { continue; }
    for (auto const& tag : c.tags) {
      if (tag == 0) { continue; }
      result.push_back(DeskWindow{
        .windowId = c.id, .deskKey = std::to_string(tag),
        .appId = c.appId, .title = c.title,
        .x = c.x, .y = c.y,
      });
    }
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

  if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
    closeSocket();
    emitChange();
    return;
  }

  if (revents & POLLIN) { drainSocket(); }
}

wl_output* MangoWorkspaceManager::ipcSelectedOutput() const {
  for (auto const& [_, st] : m_outputData) {
    if (!st.active) { continue; }
    for (auto* out : m_knownOutputs) {
      if (resolveName(out) == st.name) { return out; }
    }
  }
  return nullptr;
}

std::optional<std::pair<std::string, std::string>>
MangoWorkspaceManager::ipcFocusedClientForOutput(wl_output* output) const {
  auto* st = findOutput(output);
  if (st == nullptr) { st = currentOutput(); }
  if (st == nullptr) { return std::nullopt; }

  return std::pair{st->clientTitle, st->clientAppId};
}

void MangoWorkspaceManager::emitChange() {
  if (m_notify) { m_notify(); }
}

std::string MangoWorkspaceManager::resolveName(wl_output* output) const {
  return m_resolver && output != nullptr ? m_resolver(output) : std::string{};
}

MangoWorkspaceManager::OutputState* MangoWorkspaceManager::currentOutput() {
  for (auto& [_, st] : m_outputData) {
    if (st.active) { return &st; }
  }
  return !m_outputData.empty() ? &m_outputData.begin()->second : nullptr;
}

MangoWorkspaceManager::OutputState const* MangoWorkspaceManager::currentOutput() const {
  for (auto const& [_, st] : m_outputData) {
    if (st.active) { return &st; }
  }
  return !m_outputData.empty() ? &m_outputData.begin()->second : nullptr;
}

MangoWorkspaceManager::OutputState* MangoWorkspaceManager::findOutput(wl_output* output) {
  auto name = resolveName(output);
  if (name.empty()) { return nullptr; }
  auto it = m_outputData.find(name);
  return it != m_outputData.end() ? &it->second : nullptr;
}

MangoWorkspaceManager::OutputState const* MangoWorkspaceManager::findOutput(wl_output* output) const {
  auto name = resolveName(output);
  if (name.empty()) { return nullptr; }
  auto it = m_outputData.find(name);
  return it != m_outputData.end() ? &it->second : nullptr;
}

std::optional<std::size_t> MangoWorkspaceManager::idxFromRegion(DeskRegion const& ws) {
  if (!ws.coordinates.empty()) { return static_cast<std::size_t>(ws.coordinates[0]); }
  return parseIdx(ws.id.empty() ? ws.name : ws.id);
}

std::optional<std::size_t> MangoWorkspaceManager::parseIdx(std::string const& id) {
  if (id.empty()) { return std::nullopt; }

  std::size_t val = 0;
  auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), val);
  if (ec != std::errc{} || ptr != id.data() + id.size() || val == 0) { return std::nullopt; }
  return val - 1;
}

std::optional<std::size_t> MangoWorkspaceManager::activeTagIdx(std::vector<TagInfo> const& tags) const {
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

DeskRegion MangoWorkspaceManager::buildRegion(TagInfo const& tag, bool shellActive) {
  return DeskRegion{
    .id = std::to_string(tag.index),
    .name = std::to_string(tag.index),
    .coordinates = {tag.index > 0 ? tag.index - 1 : 0},
    .index = tag.index,
    .active = shellActive,
    .urgent = tag.urgent,
    .occupied = tag.occupied,
  };
}
