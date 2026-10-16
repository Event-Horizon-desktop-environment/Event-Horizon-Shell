#include "backends/sway/sway_workspace_manager.h"

#include "backends/sway/sway_backends.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr auto kSwayReconnectInitial = std::chrono::seconds(2);
constexpr auto kSwayReconnectMax = std::chrono::seconds(30);
constexpr std::size_t kSwayReadBufferMaxBytes = 1024U * 1024U;

bool sendAll(int fd, const char* data, std::size_t len) {
   
  std::size_t offset = 0;
  while (offset < len) {
    const ssize_t written = ::send(fd, data + offset, len - offset, MSG_NOSIGNAL);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

constexpr std::size_t kHeadBytes = 14;

struct IpcHead {
  std::uint32_t bytes = 0;
  std::uint32_t type = 0;
};

// The 14-byte sway frame header that follows the magic string.
IpcHead readHead(std::vector<char> const& buf) {
  IpcHead head;
  const auto magic = wspace::sway::SwaySession::kIpcMagic;
  std::memcpy(&head.bytes, buf.data() + magic.size(), sizeof(head.bytes));
  std::memcpy(&head.type, buf.data() + magic.size() + sizeof(head.bytes), sizeof(head.type));
  return head;
}

} // namespace

// Numbered workspaces sort before unnumbered ones, then by number, then by
// discovery order. Ranked form: equal ranks fall through to the next key.
bool SwayWorkspaceManager::swayOrder(WorkspaceRecord const& a, WorkspaceRecord const& b) {
  auto rank = [](int num) { return num >= 0 ? 0 : 1; };
  if (rank(a.num) != rank(b.num)) { return rank(a.num) < rank(b.num); }
  if (rank(a.num) == 0 && a.num != b.num) { return a.num < b.num; }
  return a.ordinal < b.ordinal;
}

SwayWorkspaceManager::SwayWorkspaceManager(wspace::sway::SwaySession& runtime)
    : m_backend(runtime) {
   
  if (m_backend.canConnect()) {
    openStream();
  }
}

SwayWorkspaceManager::~SwayWorkspaceManager() {   teardown(); }

bool SwayWorkspaceManager::ready() const noexcept {
   
  return m_backend.canConnect();
}

bool SwayWorkspaceManager::openConnection() {
   
  if (m_fd >= 0) return true;
  openStream();
  return m_fd >= 0;
}

void SwayWorkspaceManager::onStateChange(IDeskSource::Notify callback) {
   
  m_notify = std::move(callback);
}

void SwayWorkspaceManager::setOutputResolver(Resolver resolver) {
   
  m_resolver = std::move(resolver);
}

int SwayWorkspaceManager::resolveWsId(const std::string& id) const {
  long parsed = -1;
  if (std::sscanf(id.c_str(), "%ld", &parsed) != 1) return -1;
  if (parsed < 0 || parsed > 2147483647L) return -1;
  return static_cast<int>(parsed);
}

void SwayWorkspaceManager::jumpTo(const std::string& id) {
   
  (void)m_backend.sendCommand(wspace::sway::SwaySession::kRunCommand, "workspace " + id);
}

void SwayWorkspaceManager::jumpToOnOutput(wl_output* output, const std::string& id) {
   
  (void)output;
  jumpTo(id);
}

void SwayWorkspaceManager::jumpToOnOutput(wl_output* output, const DeskSlot& workspace) {
   
  (void)output;
  jumpTo(workspace.key);
}

std::vector<DeskSlot> SwayWorkspaceManager::allRegions() const {
    
  auto* self = const_cast<SwayWorkspaceManager*>(this);

  if (!m_backend.canConnect()) return {};

  if (self->m_fd < 0) {
    self->openStream();
    if (self->m_fd < 0) return {};
  }

  self->drainEvents();

  std::vector<const WorkspaceRecord*> ordered;
  ordered.reserve(m_spaces.size());
  for (const auto& ws : m_spaces) {
    ordered.push_back(&ws);
  }

  std::sort(ordered.begin(), ordered.end(),
            [](WorkspaceRecord const* a, WorkspaceRecord const* b) { return swayOrder(*a, *b); });

  std::vector<DeskSlot> result;
  result.reserve(ordered.size());
  for (const auto* ws : ordered) {
    DeskSlot w;
    w.key = ws->name;
    w.label = ws->name;
    w.ordinal = ws->num >= 0 ? static_cast<std::uint32_t>(ws->num) : static_cast<std::uint32_t>(ws->ordinal + 1);
    w.state.selected = ws->visible;
    w.state.urgent = ws->urgent;
    w.state.occupied = ws->occupied;
    result.push_back(std::move(w));
  }
  return result;
}

std::vector<DeskSlot> SwayWorkspaceManager::regionsOnOutput(wl_output* output) const {
    
  if (output == nullptr || !m_resolver) return allRegions();

  const std::string outputName = m_resolver(output);
  if (outputName.empty()) return allRegions();

  auto* self = const_cast<SwayWorkspaceManager*>(this);

  if (!m_backend.canConnect()) return {};

  if (self->m_fd < 0) {
    self->openStream();
    if (self->m_fd < 0) return {};
  }

  self->drainEvents();

  std::vector<const WorkspaceRecord*> ordered;
  ordered.reserve(m_spaces.size());
  for (const auto& ws : m_spaces) {
    if (ws.output == outputName) ordered.push_back(&ws);
  }

  std::sort(ordered.begin(), ordered.end(),
            [](WorkspaceRecord const* a, WorkspaceRecord const* b) { return swayOrder(*a, *b); });

  std::vector<DeskSlot> result;
  result.reserve(ordered.size());
  for (const auto* ws : ordered) {
    DeskSlot w;
    w.key = ws->name;
    w.label = ws->name;
    w.ordinal = ws->num >= 0 ? static_cast<std::uint32_t>(ws->num) : static_cast<std::uint32_t>(ws->ordinal + 1);
    w.state.selected = ws->visible;
    w.state.urgent = ws->urgent;
    w.state.occupied = ws->occupied;
    result.push_back(std::move(w));
  }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
SwayWorkspaceManager::appsByDesk(wl_output* output) const {
   
  (void)output;
  return m_appMap;
}

void SwayWorkspaceManager::teardown() {
   
  closeIfOpen(false);
  m_spaces.clear();
  m_occMap.clear();
  m_buf.clear();
  m_backoff = kSwayReconnectInitial;
}

void SwayWorkspaceManager::openStream() {
   
  const auto& socketPath = m_backend.queryEndpoint();
  if (m_fd >= 0 || socketPath.empty()) return;

  const auto now = std::chrono::steady_clock::now();
  if (m_retryAt.time_since_epoch().count() != 0 && now < m_retryAt) return;

  const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    planRetry();
    return;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socketPath.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    planRetry();
    return;
  }
  std::memcpy(addr.sun_path, socketPath.c_str(), socketPath.size() + 1);

  if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    planRetry();
    return;
  }

  // Subscribe to workspace and window events so we get pushed updates.
  const std::string subscribePayload = R"(["workspace","window"])";
  const auto& magic = wspace::sway::SwaySession::kIpcMagic;
  const std::uint32_t subType = wspace::sway::SwaySession::kSubscribe;
  const std::uint32_t subPayloadLen = static_cast<std::uint32_t>(subscribePayload.size());

  std::vector<char> subMsg;
  subMsg.reserve(magic.size() + sizeof(subPayloadLen) + sizeof(subType) + subscribePayload.size());
  subMsg.insert(subMsg.end(), magic.begin(), magic.end());
  subMsg.insert(subMsg.end(), reinterpret_cast<const char*>(&subPayloadLen),
               reinterpret_cast<const char*>(&subPayloadLen) + sizeof(subPayloadLen));
  subMsg.insert(subMsg.end(), reinterpret_cast<const char*>(&subType),
               reinterpret_cast<const char*>(&subType) + sizeof(subType));
  subMsg.insert(subMsg.end(), subscribePayload.begin(), subscribePayload.end());

  if (!sendAll(fd, subMsg.data(), subMsg.size())) {
    ::close(fd);
    planRetry();
    return;
  }

  // The compositor replies (often with an empty body) to confirm the subscription.
  std::array<char, 32> response;
  ssize_t n = ::recv(fd, response.data(), response.size(), 0);
  if (n <= 0) {
    ::close(fd);
    planRetry();
    return;
  }

  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  m_fd = fd;
  m_retryAt = {};
  m_backoff = kSwayReconnectInitial;
  m_buf.clear();

  fetchSnapshot();
}

void SwayWorkspaceManager::closeIfOpen(bool scheduleReconnectFlag) {
   
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
  if (scheduleReconnectFlag) {
    planRetry();
  } else {
    m_retryAt = {};
  }
}

void SwayWorkspaceManager::planRetry() {
   
  const auto now = std::chrono::steady_clock::now();
  m_retryAt = now + m_backoff;
  m_backoff = std::min(m_backoff * 2, kSwayReconnectMax);
}

void SwayWorkspaceManager::drainEvents() {
   
  std::array<char, 8192> buffer;
  while (true) {
    const ssize_t n = ::recv(m_fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
    if (n > 0) {
      m_buf.insert(m_buf.end(), buffer.data(), buffer.data() + n);
      if (m_buf.size() > kSwayReadBufferMaxBytes) {
        closeIfOpen(true);
        m_buf.clear();
        return;
      }
      continue;
    }
    if (n == 0) {
      closeIfOpen(true);
      return;
    }
    if (errno == EINTR) continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    closeIfOpen(true);
    return;
  }
  processMessages();
}

void SwayWorkspaceManager::processMessages() {
  const auto& magic = wspace::sway::SwaySession::kIpcMagic;

  bool changed = false;
  while (m_buf.size() >= kHeadBytes) {
    if (!std::equal(magic.begin(), magic.end(), m_buf.begin())) {
      closeIfOpen(true);
      return;
    }

    const IpcHead head = readHead(m_buf);
    const std::size_t totalFrame = kHeadBytes + head.bytes;
    if (m_buf.size() < totalFrame) return;

    const std::string payload(m_buf.begin() + static_cast<std::ptrdiff_t>(kHeadBytes),
                              m_buf.begin() + static_cast<std::ptrdiff_t>(kHeadBytes + head.bytes));
    m_buf.erase(m_buf.begin(),
                m_buf.begin() + static_cast<std::ptrdiff_t>(totalFrame));

    if (onIpcMessage(head.type, payload)) changed = true;
  }

  if (changed) emitChange();
}

bool SwayWorkspaceManager::onIpcMessage(std::uint32_t type, const std::string& payload) {
   
  using RT = wspace::sway::SwaySession;

  if (type == RT::kGetWorkspaces) return loadWorkspaceList(payload);
  if (type == RT::kGetTree) return loadTree(payload);
  if (type == RT::kWorkspaceEvent) {
    fetchSnapshot();
    return false;
  }
  if (type == RT::kWindowEvent) {
    fetchTree();
    return false;
  }
  return false;
}

bool SwayWorkspaceManager::loadWorkspaceList(const std::string& payload) {
   
  try {
    const auto json = nlohmann::json::parse(payload);
    if (!json.is_array()) return false;

    std::vector<WorkspaceRecord> next;
    next.reserve(json.size());
    std::size_t ordinal = 0;
    for (const auto& item : json) {
      if (!item.is_object()) continue;
      WorkspaceRecord ws;
      ws.name = item.value("name", std::string{});
      ws.output = item.value("output", std::string{});
      ws.visible = item.value("visible", false);
      ws.urgent = item.value("urgent", false);
      ws.num = item.value("num", -1);
      ws.ordinal = ordinal++;
      auto occupancy = m_occMap.find(ws.name);
      ws.occupied = occupancy != m_occMap.end() && occupancy->second > 0;
      if (!ws.name.empty()) next.push_back(std::move(ws));
    }

    std::sort(next.begin(), next.end(), swayOrder);

    if (next == m_spaces) return false;
    m_spaces = std::move(next);
    return true;
  } catch (const nlohmann::json::exception&) {
    return false;
  }
}

namespace {

void tallyOccupancy(const nlohmann::json& node, const std::string& currentWorkspace,
                    std::unordered_map<std::string, std::size_t>& counts,
                    std::unordered_map<std::string, std::vector<std::string>>& appIds) {
   
  if (!node.is_object()) return;

  std::string wsName = currentWorkspace;
  if (const auto typeIt = node.find("type"); typeIt != node.end() && typeIt->is_string()) {
    if (typeIt->get<std::string>() == "workspace" && node.contains("name") && node["name"].is_string()) {
      wsName = node["name"].get<std::string>();
    }
  }

  const auto nodesIt = node.find("nodes");
  const auto floatingIt = node.find("floating_nodes");
  const bool hasNodes = nodesIt != node.end() && nodesIt->is_array();
  const bool hasFloating = floatingIt != node.end() && floatingIt->is_array();
  const bool isLeaf = (!hasNodes || nodesIt->empty()) && (!hasFloating || floatingIt->empty());

  if (isLeaf && !wsName.empty()) {
    bool hasWindow = false;
    std::string appId;
    if (node.contains("app_id") && node["app_id"].is_string()) {
      appId = node["app_id"].get<std::string>();
      if (!appId.empty()) hasWindow = true;
    }
    if (!hasWindow && node.contains("window_properties") && node["window_properties"].is_object()) {
      const auto& wp = node["window_properties"];
      if (wp.contains("class") && wp["class"].is_string()) {
        appId = wp["class"].get<std::string>();
        if (!appId.empty()) hasWindow = true;
      }
    }
    if (!hasWindow && node.contains("window") && !node["window"].is_null()) {
      hasWindow = true;
    }
    if (hasWindow) {
      ++counts[wsName];
      if (!appId.empty()) {
        auto& apps = appIds[wsName];
        if (std::find(apps.begin(), apps.end(), appId) == apps.end()) {
          apps.push_back(std::move(appId));
        }
      }
    }
  }

  if (hasNodes) {
    for (const auto& child : *nodesIt) tallyOccupancy(child, wsName, counts, appIds);
  }
  if (hasFloating) {
    for (const auto& child : *floatingIt) tallyOccupancy(child, wsName, counts, appIds);
  }
}

} // namespace

bool SwayWorkspaceManager::loadTree(const std::string& payload) {
   
  try {
    const auto json = nlohmann::json::parse(payload);
    if (!json.is_object()) return false;

    std::unordered_map<std::string, std::size_t> occupancy;
    std::unordered_map<std::string, std::vector<std::string>> appIds;
    tallyOccupancy(json, std::string{}, occupancy, appIds);
    m_occMap = std::move(occupancy);
    m_appMap = std::move(appIds);

    bool changed = false;
    for (auto& ws : m_spaces) {
      auto it = m_occMap.find(ws.name);
      const bool nowOccupied = it != m_occMap.end() && it->second > 0;
      if (ws.occupied != nowOccupied) {
        ws.occupied = nowOccupied;
        changed = true;
      }
    }
    return changed;
  } catch (const nlohmann::json::exception&) {
    return false;
  }
}

void SwayWorkspaceManager::fetchSnapshot() {
   
  writeMessage(wspace::sway::SwaySession::kGetWorkspaces, {});
  writeMessage(wspace::sway::SwaySession::kGetTree, {});
}

void SwayWorkspaceManager::fetchTree() {
   
  writeMessage(wspace::sway::SwaySession::kGetTree, {});
}

void SwayWorkspaceManager::writeMessage(std::uint32_t type, std::string_view payload) {
   
  if (m_fd < 0) return;

  const auto& magic = wspace::sway::SwaySession::kIpcMagic;
  const std::uint32_t payloadLen = static_cast<std::uint32_t>(payload.size());

  std::vector<char> msg;
  msg.reserve(magic.size() + sizeof(payloadLen) + sizeof(type) + payload.size());
  msg.insert(msg.end(), magic.begin(), magic.end());
  msg.insert(msg.end(), reinterpret_cast<const char*>(&payloadLen),
             reinterpret_cast<const char*>(&payloadLen) + sizeof(payloadLen));
  msg.insert(msg.end(), reinterpret_cast<const char*>(&type),
             reinterpret_cast<const char*>(&type) + sizeof(type));
  msg.insert(msg.end(), payload.begin(), payload.end());

  if (!sendAll(m_fd, msg.data(), msg.size())) {
    closeIfOpen(true);
  }
}

void SwayWorkspaceManager::emitChange() const {
   
  if (m_notify) {
    m_notify();
  }
}

namespace wspace::sway {

} // namespace wspace::sway
