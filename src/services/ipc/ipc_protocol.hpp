#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Wire protocol for the Event Horizon IPC bus.
//
// Two protocol dialects share one socket:
//   - Legacy: one-shot plain-text lines ("command args...\n", <= 4096 bytes), one
//     request -> one response, connection closed after each request. Kept so the
//     existing `eh-ipc` binary and `--ipc` inline client keep working unchanged.
//   - Framed: persistent connection, magic-prefixed length-framed messages with a
//     header (version/type/flags/id), optional SCM_RIGHTS fd passing, and
//     pub/sub topics. This is what split-out components use.
//
// Framed wire layout:
//   [1 byte  magic = 0xE7]
//   [4 bytes little-endian u32 length]   // bytes of header + body (not including
//                                         // magic or this field)
//   [14 bytes packed IpcHeader]
//   [length - 14 bytes body]
//   [optional SCM_RIGHTS fds when flags & kFlagFd]
//
// Detection on connect: peek first byte; == kFrameMagic => framed, else legacy.

namespace eh::ipc {

constexpr uint8_t  kFrameMagic   = 0xE7;
constexpr uint8_t  kProtocolVersion = 1;
constexpr uint32_t kMaxFrameBody = 1u << 20;   // 1 MiB payload cap
constexpr uint16_t kMaxLegacyCmd = 4096;       // legacy line cap (unchanged)

enum FrameType : uint8_t {
  kTypeRequest     = 1,
  kTypeResponse    = 2,
  kTypeEvent       = 3,
  kTypeAck         = 4,
  kTypeSubscribe   = 5,
  kTypeUnsubscribe = 6,
};

enum FrameFlags : uint32_t {
  kFlagFd = 1u << 0,  // SCM_RIGHTS fds are attached after the frame
};

#pragma pack(push, 1)
struct IpcHeader {
  uint8_t  version;   // == kProtocolVersion
  uint8_t  type;      // FrameType
  uint32_t flags;     // FrameFlags
  uint64_t id;        // correlation id (0 for events/acks)
};
#pragma pack(pop)

static_assert(sizeof(IpcHeader) == 14, "IpcHeader must be 14 packed bytes");

struct IpcFrame {
  IpcHeader            header{};
  std::vector<uint8_t> body;
};

inline constexpr size_t kHeaderSize = sizeof(IpcHeader);

// Encode magic + length + header + body into `out`.
void encode_frame(const IpcFrame& frame, std::vector<uint8_t>& out);

// Try to parse one frame from a receive buffer. Returns true and sets `consumed`
// when a complete frame was parsed. Returns false with consumed = 0 when more
// data is needed (or the buffer is corrupt/oversized).
bool decode_frame(const uint8_t* data, size_t len, size_t& consumed, IpcFrame& out);

// Build a request frame (body = command line, same syntax as legacy).
IpcFrame make_request(uint64_t id, const std::string& command_line);

// Build an event frame (body = topic \0 payload).
IpcFrame make_event(const std::string& topic, std::string_view payload);

// Build an ack / response frame.
IpcFrame make_response(uint64_t id, const std::string& body);
IpcFrame make_ack(uint64_t id);

} // namespace eh::ipc
