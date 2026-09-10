#include "backends/niri/niri_backends.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct NiriRuntime::ReplyInfo {
  enum class Tag { None, WriteFail, ReadFail, NoContent, BadJson, Ok };
  Tag t = Tag::None;
  std::optional<nlohmann::json> data;
};

bool NiriRuntime::canConnect() const {
  resolveIfPending();
  return !m_endpoint.empty();
}

const std::string& NiriRuntime::queryEndpoint() const {
  resolveIfPending();
  return m_endpoint;
}

std::optional<nlohmann::json> NiriRuntime::sendQuery(std::string_view req) const {
  return transmit(req).data;
}

bool NiriRuntime::sendCheck(std::string_view req, bool allowNoResponse) const {
  auto reply = transmit(req);
  if (reply.t == ReplyInfo::Tag::NoContent) { return allowNoResponse; }
  return reply.data.has_value() && reply.data->is_object() && reply.data->contains("Ok");
}

bool NiriRuntime::dispatchAction(const nlohmann::json& action, bool allowNoResponse) const {
  nlohmann::json req = nlohmann::json::object();
  req["Action"] = action;
  auto payload = req.dump();
  payload.push_back('\n');
  return sendCheck(payload, allowNoResponse);
}

NiriRuntime::ReplyInfo NiriRuntime::transmit(std::string_view req) const {
  resolveIfPending();
  if (m_endpoint.empty() || req.empty()) { return {ReplyInfo::Tag::None, std::nullopt}; }

  int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) { return {ReplyInfo::Tag::None, std::nullopt}; }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (m_endpoint.size() >= sizeof(addr.sun_path)) { ::close(fd); return {ReplyInfo::Tag::None, std::nullopt}; }
  std::memcpy(addr.sun_path, m_endpoint.c_str(), m_endpoint.size() + 1);

  if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return {ReplyInfo::Tag::None, std::nullopt};
  }

  std::size_t off = 0;
  while (off < req.size()) {
    ssize_t n = ::write(fd, req.data() + off, req.size() - off);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) { continue; }
      ::close(fd);
      return {ReplyInfo::Tag::WriteFail, std::nullopt};
    }
    off += static_cast<std::size_t>(n);
  }

  std::string resp;
  std::array<char, 4096> buf;
  for (;;) {
    ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n > 0) {
      resp.append(buf.data(), static_cast<std::size_t>(n));
      if (resp.find('\n') != std::string::npos) { break; }
      continue;
    }
    if (n == 0) { break; }
    if (errno == EINTR) { continue; }
    ::close(fd);
    return {ReplyInfo::Tag::ReadFail, std::nullopt};
  }

  ::close(fd);

  auto nl = resp.find('\n');
  if (nl != std::string::npos) { resp.resize(nl); }
  if (resp.empty()) { return {ReplyInfo::Tag::NoContent, std::nullopt}; }

  try { return {ReplyInfo::Tag::Ok, nlohmann::json::parse(resp)}; }
  catch (nlohmann::json::exception const&) { return {ReplyInfo::Tag::BadJson, std::nullopt}; }
}

void NiriRuntime::rescan() {
  m_endpoint.clear();
  m_resolved = false;
  locateEndpoint();
}

void NiriRuntime::resolveIfPending() const {
  if (!m_resolved) { locateEndpoint(); }
}

void NiriRuntime::locateEndpoint() const {
  m_resolved = true;
  auto* p = std::getenv("NIRI_SOCKET");
  if (p != nullptr && p[0] != '\0') { m_endpoint = p; }
}
