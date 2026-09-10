#include "services/ipc/ipc_protocol.hpp"

#include <cstring>

namespace eh::ipc {

void encode_frame(const IpcFrame& frame, std::vector<uint8_t>& out) {
  const uint32_t len = static_cast<uint32_t>(kHeaderSize + frame.body.size());
  out.push_back(kFrameMagic);
  out.push_back(static_cast<uint8_t>(len & 0xFFu));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
  out.push_back(static_cast<uint8_t>((len >> 16) & 0xFFu));
  out.push_back(static_cast<uint8_t>((len >> 24) & 0xFFu));

  const auto* h = reinterpret_cast<const uint8_t*>(&frame.header);
  out.insert(out.end(), h, h + sizeof(frame.header));
  out.insert(out.end(), frame.body.begin(), frame.body.end());
}

bool decode_frame(const uint8_t* data, size_t len, size_t& consumed, IpcFrame& out) {
  // `data` is a raw wire buffer that begins with the magic byte. `consumed`
  // includes the magic, so callers can drain `consumed` bytes from their buffer.
  consumed = 0;
  if (len == 0) return false;
  if (data[0] != kFrameMagic) {
    // Not a framed message (should not happen on a framed connection).
    consumed = len;
    return false;
  }
  if (len < 5) return false;  // magic + 4-byte length
  const uint32_t frameLen = static_cast<uint32_t>(data[1]) |
                            (static_cast<uint32_t>(data[2]) << 8) |
                            (static_cast<uint32_t>(data[3]) << 16) |
                            (static_cast<uint32_t>(data[4]) << 24);
  if (frameLen < kHeaderSize || frameLen > kMaxFrameBody + kHeaderSize) {
    consumed = len;
    return false;
  }
  const size_t total = 5u + frameLen;
  if (len < total) return false;  // need more data

  std::memcpy(&out.header, data + 5, sizeof(out.header));
  out.body.assign(data + 5 + sizeof(out.header), data + total);
  consumed = total;
  return true;
}

IpcFrame make_request(uint64_t id, const std::string& command_line) {
  IpcFrame f;
  f.header.version = kProtocolVersion;
  f.header.type    = kTypeRequest;
  f.header.flags   = 0;
  f.header.id      = id;
  f.body.assign(command_line.begin(), command_line.end());
  return f;
}

IpcFrame make_event(const std::string& topic, std::string_view payload) {
  IpcFrame f;
  f.header.version = kProtocolVersion;
  f.header.type    = kTypeEvent;
  f.header.flags   = 0;
  f.header.id      = 0;
  f.body.assign(topic.begin(), topic.end());
  f.body.push_back('\0');
  f.body.insert(f.body.end(), payload.begin(), payload.end());
  return f;
}

IpcFrame make_response(uint64_t id, const std::string& body) {
  IpcFrame f;
  f.header.version = kProtocolVersion;
  f.header.type    = kTypeResponse;
  f.header.flags   = 0;
  f.header.id      = id;
  f.body.assign(body.begin(), body.end());
  return f;
}

IpcFrame make_ack(uint64_t id) {
  IpcFrame f;
  f.header.version = kProtocolVersion;
  f.header.type    = kTypeAck;
  f.header.flags   = 0;
  f.header.id      = id;
  return f;
}

} // namespace eh::ipc
