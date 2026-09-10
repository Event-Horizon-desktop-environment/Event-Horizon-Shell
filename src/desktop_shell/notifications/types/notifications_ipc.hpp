#pragma once

#include "desktop_shell/notifications/types/notification_types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Cross-process notification bus contract.
//
// The NotificationManager + D-Bus service + toast host live in the
// `horizon-notifications` child. Producers (supervisor `notify` command,
// mpris now-playing, the settings test button) publish framed `notify.push`
// events; the child subscribes and owns the authoritative state. The child
// publishes `notify.changed` when state changes so the shell / control-center
// can render a notifications strip.
//
// Payload fields are joined with kFieldSep ('\x1e'); notification text is
// printable and never contains it. Keys must also be single fields, so composite
// keys (e.g. mpris bus_name+title+artist) are joined with '\x1f', never '\x1e'.
// Images are downscaled and inlined in the payload (client->server SCM_RIGHTS is
// not implemented, and the 1 MiB frame cap is respected by capping the art at
// kArtMaxDim).

namespace eh::notify {

inline constexpr char kPushTopic[] = "notify.push";
inline constexpr char kChangedTopic[] = "notify.changed";

inline constexpr char kFieldSep = '\x1e';

// Payload kinds on `notify.push`:
//   internal  \x1e app \x1e summary \x1e body \x1e urgency
//   mpris     \x1e key \x1e summary \x1e body
//   mpris-art \x1e key \x1e <image blob>
inline constexpr char kKindInternal[] = "internal";
inline constexpr char kKindMpris[] = "mpris";
inline constexpr char kKindMprisArt[] = "mpris-art";

inline constexpr std::int32_t kArtMaxDim = 160;

// Image serialization (downscale + inline blob).

inline std::uint64_t art_payload_size(const eh::shell::notifications::NotificationImageData& img) {
  return static_cast<std::uint64_t>(img.width) * img.height * 4;
}

inline eh::shell::notifications::NotificationImageData downscale_art(
    const eh::shell::notifications::NotificationImageData& in) {
  if (in.width <= kArtMaxDim && in.height <= kArtMaxDim) {
    return in;
  }
  const double scale = std::min(static_cast<double>(kArtMaxDim) / in.width,
                                static_cast<double>(kArtMaxDim) / in.height);
  const int dw = std::max(1, static_cast<int>(std::lround(in.width * scale)));
  const int dh = std::max(1, static_cast<int>(std::lround(in.height * scale)));
  const int srcPb = (in.channels >= 4) ? 4 : 3;
  const int srcStride = in.rowStride > 0 ? in.rowStride : in.width * srcPb;

  eh::shell::notifications::NotificationImageData out;
  out.width = dw;
  out.height = dh;
  out.rowStride = dw * 4;
  out.hasAlpha = true;
  out.bitsPerSample = 8;
  out.channels = 4;
  out.data.resize(static_cast<std::size_t>(dw) * dh * 4);

  for (int y = 0; y < dh; ++y) {
    const int y0 = static_cast<int>((static_cast<long long>(y) * in.height) / dh);
    const int y1 = std::max(y0 + 1, static_cast<int>((static_cast<long long>(y + 1) * in.height) / dh));
    for (int x = 0; x < dw; ++x) {
      const int x0 = static_cast<int>((static_cast<long long>(x) * in.width) / dw);
      const int x1 = std::max(x0 + 1, static_cast<int>((static_cast<long long>(x + 1) * in.width) / dw));
      std::uint64_t r = 0, g = 0, b = 0, a = 0;
      const std::uint64_t n = static_cast<std::uint64_t>(y1 - y0) * (x1 - x0);
      for (int sy = y0; sy < y1; ++sy) {
        const std::uint8_t* p =
            in.data.data() + static_cast<std::size_t>(sy) * srcStride + static_cast<std::size_t>(x0) * srcPb;
        for (int sxx = x0; sxx < x1; ++sxx) {
          r += p[0];
          g += p[1];
          b += p[2];
          a += (srcPb >= 4) ? p[3] : 255;
          p += srcPb;
        }
      }
      std::uint8_t* d = out.data.data() + (static_cast<std::size_t>(y) * dw + x) * 4;
      d[0] = static_cast<std::uint8_t>(r / n);
      d[1] = static_cast<std::uint8_t>(g / n);
      d[2] = static_cast<std::uint8_t>(b / n);
      d[3] = static_cast<std::uint8_t>(a / n);
    }
  }
  return out;
}

struct ImageBlobHeader {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t rowStride = 0;
  std::int32_t hasAlpha = 1;
  std::int32_t bitsPerSample = 8;
  std::int32_t channels = 4;
  std::uint64_t dataLen = 0;
};

inline std::vector<std::uint8_t> serialize_image_blob(
    const eh::shell::notifications::NotificationImageData& img) {
  ImageBlobHeader h;
  h.width = img.width;
  h.height = img.height;
  h.rowStride = img.rowStride;
  h.hasAlpha = img.hasAlpha ? 1 : 0;
  h.bitsPerSample = img.bitsPerSample;
  h.channels = img.channels;
  h.dataLen = img.data.size();

  std::vector<std::uint8_t> blob(sizeof(h) + img.data.size());
  std::memcpy(blob.data(), &h, sizeof(h));
  if (!img.data.empty()) {
    std::memcpy(blob.data() + sizeof(h), img.data.data(), img.data.size());
  }
  return blob;
}

inline std::optional<eh::shell::notifications::NotificationImageData> deserialize_image_blob(
    std::string_view blob) {
  if (blob.size() < sizeof(ImageBlobHeader)) return std::nullopt;
  ImageBlobHeader h;
  std::memcpy(&h, blob.data(), sizeof(h));
  if (h.width <= 0 || h.height <= 0 || h.width > kArtMaxDim * 4 || h.height > kArtMaxDim * 4 ||
      h.dataLen > blob.size() - sizeof(h)) {
    return std::nullopt;
  }
  eh::shell::notifications::NotificationImageData out;
  out.width = h.width;
  out.height = h.height;
  out.rowStride = h.rowStride;
  out.hasAlpha = h.hasAlpha != 0;
  out.bitsPerSample = h.bitsPerSample;
  out.channels = h.channels;
  out.data.assign(blob.data() + sizeof(h), blob.data() + sizeof(h) + h.dataLen);
  return out;
}

// Process-wide sender.
// The process that owns an IPC client installs a sender so `eh::notify::` push
// helpers can reach the bus. Only one process actually owns a manager (the
// notifications child); the rest just forward.

using PushSender = std::function<bool(const std::string& payload, const std::vector<int>& fds)>;

inline PushSender& notifySenderSlot() {
  static PushSender s;
  return s;
}

inline void setNotifySender(PushSender sender) { notifySenderSlot() = std::move(sender); }

inline bool canPush() { return static_cast<bool>(notifySenderSlot()); }

inline bool push_raw(const std::string& payload, const std::vector<int>& fds = {}) {
  auto& sender = notifySenderSlot();
  if (!sender) return false;
  return sender(payload, fds);
}

// Encoding helpers.

inline std::string encode_internal(const std::string& app, const std::string& summary,
                                   const std::string& body, int urgency) {
  std::string out;
  out.reserve(app.size() + summary.size() + body.size() + 16);
  out = kKindInternal;
  out += kFieldSep;
  out += app;
  out += kFieldSep;
  out += summary;
  out += kFieldSep;
  out += body;
  out += kFieldSep;
  out += std::to_string(urgency);
  return out;
}

inline std::string encode_mpris(const std::string& key, const std::string& summary,
                                const std::string& body) {
  std::string out;
  out.reserve(key.size() + summary.size() + body.size() + 16);
  out = kKindMpris;
  out += kFieldSep;
  out += key;
  out += kFieldSep;
  out += summary;
  out += kFieldSep;
  out += body;
  return out;
}

inline void push_internal(const std::string& app, const std::string& summary,
                          const std::string& body, int urgency) {
  (void)push_raw(encode_internal(app, summary, body, urgency));
}

inline void push_mpris(const std::string& key, const std::string& summary, const std::string& body) {
  (void)push_raw(encode_mpris(key, summary, body));
}

inline void push_mpris_art(const std::string& key,
                           const eh::shell::notifications::NotificationImageData& img) {
  const auto blob = serialize_image_blob(downscale_art(img));
  std::string payload;
  payload.reserve(key.size() + blob.size() + 32);
  payload = kKindMprisArt;
  payload += kFieldSep;
  payload += key;
  payload += kFieldSep;
  payload.append(reinterpret_cast<const char*>(blob.data()), blob.size());
  (void)push_raw(payload);
}

}  // namespace eh::notify
