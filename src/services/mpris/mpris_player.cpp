#include "services/mpris/mpris_player.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/notifications/types/notifications_notify.hpp"
#include "desktop_shell/common/log/shell_diag_log.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eh::mpris {
namespace {

constexpr const char* kDBusIface = "org.freedesktop.DBus";
constexpr const char* kDBusPath = "/org/freedesktop/DBus";
constexpr const char* kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr const char* kRootIface = "org.mpris.MediaPlayer2";
constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
constexpr const char* kMprisPath = "/org/mpris/MediaPlayer2";
constexpr std::string_view kMprisPrefix = "org.mpris.MediaPlayer2.";

constexpr auto kFullRediscoveryInterval = std::chrono::seconds{30};
constexpr auto kPositionProjectionMaxDelta = std::chrono::seconds{5};

static void trim_in_place(std::string& s) {
   
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

static std::optional<eh::shell::notifications::NotificationImageData> cairo_surface_to_notification_image(cairo_surface_t* surface) {
   
  if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    eh::shell_log::mpris_dbus("cairo_surface_to_notification_image: invalid surface");
    return std::nullopt;
  }
  if (cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
    eh::shell_log::mpris_dbus("cairo_surface_to_notification_image: non-image surface");
    return std::nullopt;
  }

  const int w = cairo_image_surface_get_width(surface);
  const int h = cairo_image_surface_get_height(surface);
  if (w <= 0 || h <= 0) {
    eh::shell_log::mpris_dbus("cairo_surface_to_notification_image: invalid dimensions ", w, "x", h);
    return std::nullopt;
  }

  const unsigned char* src = cairo_image_surface_get_data(surface);
  const int src_stride = cairo_image_surface_get_stride(surface);

  eh::shell::notifications::NotificationImageData img;
  img.width = w;
  img.height = h;
  img.rowStride = w * 4;
  img.hasAlpha = true;
  img.bitsPerSample = 8;
  img.channels = 4;
  img.data.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

  for (int y = 0; y < h; y++) {
    const uint8_t* s = src + static_cast<size_t>(y) * static_cast<size_t>(src_stride);
    uint8_t* d = img.data.data() + static_cast<size_t>(y) * static_cast<size_t>(img.rowStride);
    for (int x = 0; x < w; x++) {
      const size_t off = static_cast<size_t>(x) * 4;
      d[off + 0] = s[off + 2];
      d[off + 1] = s[off + 1];
      d[off + 2] = s[off + 0];
      d[off + 3] = s[off + 3];
    }
  }

  eh::shell_log::mpris_dbus("cairo_surface_to_notification_image: ", w, "x", h);
  return img;
}

static std::vector<std::string> split_comma_list(std::string_view raw) {
   
  std::vector<std::string> out;
  std::string cur;
  cur.reserve(raw.size());
  for (char c : raw) {
    if (c == ',' || c == ';') {
      trim_in_place(cur);
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else
      cur.push_back(c);
  }
  trim_in_place(cur);
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

static std::string lower_copy(std::string_view s) {
   
  std::string r;
  r.reserve(s.size());
  for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return r;
}

static bool bus_blacklisted(std::string_view bus_lower, const std::vector<std::string>& tokens_lower) {
   
  for (const auto& t : tokens_lower) {
    if (t.empty()) continue;
    if (bus_lower.find(t) != std::string::npos) return true;
  }
  return false;
}

static size_t preferred_order(std::string_view bus_lower, const std::vector<std::string>& pref_lower) {
   
  constexpr size_t kNoPref = 1000;
  for (size_t i = 0; i < pref_lower.size(); ++i) {
    if (pref_lower[i].empty()) continue;
    if (bus_lower.find(pref_lower[i]) != std::string::npos) return i;
  }
  return kNoPref;
}

static bool worse_than(const RankedCandidate& a, const RankedCandidate& b, int tier_rank,
                       const std::vector<std::string>& pref_lower, const std::string& last_chosen) {
   
  if (a.rank != b.rank) return a.rank < b.rank;
  if (tier_rank == 3) {
    if (a.playing_since != b.playing_since) return a.playing_since < b.playing_since;
  }
  const std::string al = lower_copy(a.s.bus_name);
  const std::string bl = lower_copy(b.s.bus_name);
  const size_t ia = preferred_order(al, pref_lower);
  const size_t ib = preferred_order(bl, pref_lower);
  if (ia != ib) return ia > ib;
  if (!last_chosen.empty()) {
    if (a.s.bus_name == last_chosen && b.s.bus_name != last_chosen) return false;
    if (b.s.bus_name == last_chosen && a.s.bus_name != last_chosen) return true;
  }
  return a.s.bus_name > b.s.bus_name;
}

int64_t get_int64_from_variant(const sdbus::Variant& v) {
   
  try {
    return v.get<int64_t>();
  } catch (const sdbus::Error&) {}
  try {
    return static_cast<int64_t>(v.get<uint64_t>());
  } catch (const sdbus::Error&) {}
  try {
    return static_cast<int64_t>(v.get<int32_t>());
  } catch (const sdbus::Error&) {}
  try {
    return static_cast<int64_t>(v.get<uint32_t>());
  } catch (const sdbus::Error&) {}
  return 0;
}

bool get_bool_from_variant(const sdbus::Variant& v) {
   
  try {
    return v.get<bool>();
  } catch (const sdbus::Error&) {}
  return false;
}

std::map<std::string, sdbus::Variant> get_variant_map_from_variant(const sdbus::Variant& v) {
   
  try {
    return v.get<std::map<std::string, sdbus::Variant>>();
  } catch (const sdbus::Error&) {}
  return {};
}

} // namespace

bool DockMpris::is_mpris_bus_name(const std::string& name) {
   
  return name.compare(0, kMprisPrefix.size(), kMprisPrefix) == 0 && name.size() > kMprisPrefix.size();
}

std::string DockMpris::variant_as_string(const sdbus::Variant& v) {
   
  try {
    return v.get<std::string>();
  } catch (const sdbus::Error&) {
  }
  try {
    const auto a = v.get<std::vector<std::string>>();
    if (!a.empty()) return a[0];
  } catch (const sdbus::Error&) {
  }
  return {};
}

int DockMpris::player_rank(const PlayerSnapshot& s) {
   
  if (s.playback_status == "Playing") return 3;
  if (s.playback_status == "Paused") return 2;
  if (s.playback_status == "Stopped") return 1;
  return 0;
}

void DockMpris::process_async_properties(const std::string& busName,
                                        const std::map<std::string, sdbus::Variant>& /*rootProps*/,
                                        const std::map<std::string, sdbus::Variant>& playerProps,
                                        bool hadPositionSignal) {
   
  const auto pbIt = playerProps.find("PlaybackStatus");
  if (pbIt == playerProps.end()) return;

  std::string playbackStatus = variant_as_string(pbIt->second);
  if (playbackStatus.empty()) return;

  std::string title, artist, album, artUrl, trackUrl, trackId;
  int64_t durationUs = 0;
  int64_t positionUs = 0;
  bool canGoNext = false, canGoPrevious = false, canPlay = false, canPause = false;

  const auto metaIt = playerProps.find("Metadata");
  if (metaIt != playerProps.end()) {
    const auto meta = get_variant_map_from_variant(metaIt->second);
    auto it = meta.find("xesam:title");
    if (it != meta.end()) title = variant_as_string(it->second);
    it = meta.find("xesam:artist");
    if (it != meta.end()) artist = variant_as_string(it->second);
    it = meta.find("xesam:album");
    if (it != meta.end()) album = variant_as_string(it->second);
    it = meta.find("mpris:artUrl");
    if (it != meta.end()) artUrl = variant_as_string(it->second);
    it = meta.find("xesam:url");
    if (it != meta.end()) trackUrl = variant_as_string(it->second);
    it = meta.find("mpris:trackid");
    if (it != meta.end()) trackId = variant_as_string(it->second);
    it = meta.find("mpris:length");
    if (it != meta.end()) durationUs = get_int64_from_variant(it->second);
  }

  const auto posIt = playerProps.find("Position");
  if (posIt != playerProps.end()) {
    positionUs = get_int64_from_variant(posIt->second);
  }

  const auto readFlag = [&](const std::string& key) -> bool {
    auto it = playerProps.find(key);
    if (it == playerProps.end()) return false;
    return get_bool_from_variant(it->second);
  };
  canGoNext = readFlag("CanGoNext");
  canGoPrevious = readFlag("CanGoPrevious");
  canPlay = readFlag("CanPlay");
  canPause = readFlag("CanPause");

  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mu_);

  auto cacheIt = m_playerCache.find(busName);
  if (cacheIt == m_playerCache.end()) {
    PlayerSnapshot snap;
    snap.active = true;
    snap.bus_name = busName;
    snap.title = std::move(title);
    snap.artist = std::move(artist);
    snap.album = std::move(album);
    snap.art_url_raw = std::move(artUrl);
    snap.track_url_raw = std::move(trackUrl);
    snap.track_id = std::move(trackId);
    snap.playback_status = std::move(playbackStatus);
    snap.can_go_next = canGoNext;
    snap.can_go_previous = canGoPrevious;
    snap.can_play = canPlay;
    snap.can_pause = canPause;
    snap.position_us = hadPositionSignal ? positionUs : 0;
    snap.duration_us = durationUs;

    PlayerState ps;
    ps.snap = std::move(snap);
    ps.rank = player_rank(ps.snap);
    ps.playingSince = ps.snap.playback_status == "Playing" ? now : std::chrono::steady_clock::time_point{};
    ps.lastPositionAt = ps.snap.playback_status != "Stopped" ? now : std::chrono::steady_clock::time_point{};
    ps.authoritativePosition = hadPositionSignal || ps.snap.playback_status != "Stopped";
    m_playerCache[busName] = std::move(ps);
    m_cacheChanged = true;
  } else {
    PlayerState& ps = cacheIt->second;

    if (playbackStatus != ps.snap.playback_status ||
        title != ps.snap.title || artist != ps.snap.artist ||
        album != ps.snap.album || artUrl != ps.snap.art_url_raw ||
        trackUrl != ps.snap.track_url_raw || durationUs != ps.snap.duration_us ||
        canGoNext != ps.snap.can_go_next || canGoPrevious != ps.snap.can_go_previous ||
        canPlay != ps.snap.can_play || canPause != ps.snap.can_pause) {
      m_cacheChanged = true;
    }

    const bool wasPlaying = ps.snap.playback_status == "Playing";
    ps.snap.title = std::move(title);
    ps.snap.artist = std::move(artist);
    ps.snap.album = std::move(album);
    ps.snap.art_url_raw = std::move(artUrl);
    ps.snap.track_url_raw = std::move(trackUrl);
    ps.snap.track_id = std::move(trackId);
    ps.snap.playback_status = std::move(playbackStatus);
    ps.snap.can_go_next = canGoNext;
    ps.snap.can_go_previous = canGoPrevious;
    ps.snap.can_play = canPlay;
    ps.snap.can_pause = canPause;
    ps.snap.duration_us = durationUs;
    ps.snap.position_us = positionUs;
    ps.rank = player_rank(ps.snap);
    if (ps.snap.playback_status == "Playing" && !wasPlaying) {
      ps.playingSince = now;
    }
    if (ps.snap.playback_status != "Stopped") {
      ps.lastPositionAt = now;
      ps.authoritativePosition = true;
    } else {
      ps.authoritativePosition = false;
    }
  }
}

int64_t DockMpris::projected_position(const PlayerSnapshot& s, const PlayerState& ps) const {
   
  if (s.playback_status == "Stopped" || s.playback_status.empty()) return 0;
  if (!ps.authoritativePosition || ps.lastPositionAt.time_since_epoch().count() == 0) {
    return std::max<int64_t>(0, s.position_us);
  }

  int64_t pos = std::max<int64_t>(0, s.position_us);

  if (s.playback_status == "Playing") {
    const auto elapsed = std::chrono::steady_clock::now() - ps.lastPositionAt;
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    if (elapsedUs > 0 && elapsed < kPositionProjectionMaxDelta) {
      pos += elapsedUs;
    }
  }

  if (s.duration_us > 0) {
    pos = std::clamp<int64_t>(pos, 0, s.duration_us);
  }

  return pos;
}

void DockMpris::maybe_emit_now_playing_notify_assume_locked() {
   
  const char* e = std::getenv("EH_MPRIS_NOTIFY");
  if (e && (e[0] == '0' || std::strcmp(e, "off") == 0)) {
    return;
  }
  if (!eh::config::shell_config_snapshot().mprisNowPlayingNotify) {
    return;
  }
  const PlayerSnapshot& s = snap_;
  if (s.playback_status != "Playing") {
    if (s.playback_status == "Stopped" || s.playback_status.empty()) {
      last_track_notify_key_.clear();
    }
    return;
  }

  std::string title = s.title;
  std::string artist = s.artist;
  trim_in_place(title);
  trim_in_place(artist);
  if (title.empty() && artist.empty()) {
    return;
  }

  // Key must round-trip as a single `notify.push` field, so its composite parts
  // are joined with \x1f (unit separator), never kFieldSep ('\x1e').
  std::string key = s.bus_name;
  key.push_back('\x1f');
  key += title;
  key.push_back('\x1f');
  key += artist;
  if (key == last_track_notify_key_) {
    return;
  }
  last_track_notify_key_ = key;

  const std::string summary = title.empty() ? artist : title;
  const std::string body = (title.empty() || artist.empty()) ? std::string(" ") : artist;

  eh::notify::push_mpris(key, summary, body);
  if (s.art) {
    if (auto art = cairo_surface_to_notification_image(s.art.get())) {
      eh::notify::push_mpris_art(key, *art);
    }
  }
}

DockMpris::DockMpris() {
   
  try {
    bus_ = sdbus::createSessionBusConnection();
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("session bus: ", e.what());
    return;
  }
  setup_dbus_signals();
}

DockMpris::~DockMpris() {
   
  MANGOWM_INFO("{}", __func__);
  m_playerProxies.clear();
  m_dbusProxy.reset();
  m_playerCache.clear();

  if (!art_future_.valid()) return;
  art_future_.wait();
  try {
    (void)art_future_.get();
  } catch (const std::exception& e) {
    std::cerr << "[mpris] album art load (shutdown): " << e.what() << "\n";
  } catch (...) {
    std::cerr << "[mpris] album art load (shutdown): unknown error\n";
  }
}

int DockMpris::poll_fd() const {
   
  if (!bus_) return -1;
  auto pd = bus_->getEventLoopPollData();
  return pd.fd;
}

int DockMpris::poll_event_fd() const {
   
  if (!bus_) return -1;
  auto pd = bus_->getEventLoopPollData();
  return pd.eventFd;
}

void DockMpris::process_pending_events() {
   
  if (!bus_) return;
  int budget = 64;
  while (budget-- > 0) {
    try {
      if (!bus_->processPendingEvent()) break;
    } catch (const std::exception& e) {
      eh::shell_log::mpris_dbus("process_pending_events: ", e.what());
      break;
    }
  }
}

void DockMpris::setup_dbus_signals() {
   
  try {
    m_dbusProxy = sdbus::createProxy(*bus_, sdbus::ServiceName{kDBusIface}, sdbus::ObjectPath{kDBusPath});
    setup_name_owner_changed();
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("DBus proxy setup: ", e.what());
  }
}

void DockMpris::setup_name_owner_changed() {
   
  if (!m_dbusProxy) return;
  try {
    m_dbusProxy->uponSignal("NameOwnerChanged")
        .onInterface(kDBusIface)
        .call([this](const std::string& name, const std::string& /*oldOwner*/, const std::string& newOwner) {
          if (!is_mpris_bus_name(name)) return;
          if (newOwner.empty()) {
            std::lock_guard<std::mutex> lock(mu_);
            remove_player(name);
            m_cacheChanged = true;
          } else {
            add_player(name);
          }
        });
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("NameOwnerChanged setup: ", e.what());
  }
}

void DockMpris::discover_all_players() {
   
  if (!bus_ || !m_dbusProxy) return;
  const std::weak_ptr<void> guard = m_aliveGuard;
  try {
    m_dbusProxy->callMethodAsync("ListNames")
        .onInterface(kDBusIface)
        .uponReplyInvoke([this, guard](std::optional<sdbus::Error> err, std::vector<std::string> names) {
          if (guard.expired()) return;
          if (err.has_value()) {
            eh::shell_log::mpris_dbus("ListNames async: ", err->what());
            return;
          }

          std::unordered_set<std::string> current_players;
          for (const auto& n : names) {
            if (is_mpris_bus_name(n)) current_players.insert(n);
          }
          std::unique_lock<std::mutex> lock(mu_);

          for (auto it = m_playerCache.begin(); it != m_playerCache.end();) {
            if (!current_players.count(it->first)) {
              m_playerProxies.erase(it->first);
              it = m_playerCache.erase(it);
              m_cacheChanged = true;
            } else {
              ++it;
            }
          }

          for (const auto& name : current_players) {
            if (!m_playerCache.count(name)) {
              lock.unlock();
              add_player(name);
              lock.lock();
            }
          }

          m_lastFullDiscovery = std::chrono::steady_clock::now();
        });
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("discover_all_players async: ", e.what());
  }
}

void DockMpris::add_player(const std::string& busName) {
   
  if (!bus_) return;
  try {
    auto proxy = sdbus::createProxy(*bus_, sdbus::ServiceName{busName}, sdbus::ObjectPath{kMprisPath});

    proxy->uponSignal("PropertiesChanged")
        .onInterface(kPropsIface)
        .call([this, busName](const std::string& interfaceName,
                              const std::map<std::string, sdbus::Variant>& changedProps,
                              const std::vector<std::string>& /*invalidated*/) {
           
          if (interfaceName != kRootIface && interfaceName != kPlayerIface) return;
          const bool metadataChanged = changedProps.contains("Metadata");
          std::unique_lock<std::mutex> lock(mu_);
          auto cacheIt = m_playerCache.find(busName);
          if (cacheIt == m_playerCache.end()) {
            lock.unlock();
            refresh_player_properties(busName);
            return;
          }

          const bool positionChanged = changedProps.contains("Position");
          if (positionChanged) {
            auto posIt = changedProps.find("Position");
            if (posIt != changedProps.end()) {
              const int64_t rawPos = get_int64_from_variant(posIt->second);
              if (rawPos >= 0) {
                cacheIt->second.snap.position_us = rawPos;
                cacheIt->second.lastPositionAt = std::chrono::steady_clock::now();
                cacheIt->second.authoritativePosition = true;
              }
            }
          }

          if (metadataChanged) {
            auto metaIt = changedProps.find("Metadata");
            if (metaIt != changedProps.end()) {
              const auto meta = get_variant_map_from_variant(metaIt->second);
              auto it = meta.find("xesam:title");
              if (it != meta.end()) cacheIt->second.snap.title = variant_as_string(it->second);
              it = meta.find("xesam:artist");
              if (it != meta.end()) cacheIt->second.snap.artist = variant_as_string(it->second);
              it = meta.find("xesam:album");
              if (it != meta.end()) cacheIt->second.snap.album = variant_as_string(it->second);
              it = meta.find("mpris:artUrl");
              if (it != meta.end()) cacheIt->second.snap.art_url_raw = variant_as_string(it->second);
              it = meta.find("xesam:url");
              if (it != meta.end()) cacheIt->second.snap.track_url_raw = variant_as_string(it->second);
              it = meta.find("mpris:trackid");
              if (it != meta.end()) cacheIt->second.snap.track_id = variant_as_string(it->second);
              it = meta.find("mpris:length");
              if (it != meta.end()) cacheIt->second.snap.duration_us = get_int64_from_variant(it->second);
            }
          }

          if (const auto pbIt = changedProps.find("PlaybackStatus"); pbIt != changedProps.end()) {
            cacheIt->second.snap.playback_status = variant_as_string(pbIt->second);
            cacheIt->second.rank = player_rank(cacheIt->second.snap);
            if (cacheIt->second.snap.playback_status == "Playing") {
              cacheIt->second.playingSince = std::chrono::steady_clock::now();
            }
          }

          auto readFlag = [&](const std::string& key, bool& field) {
            auto it = changedProps.find(key);
            if (it != changedProps.end()) field = get_bool_from_variant(it->second);
          };
          readFlag("CanGoNext", cacheIt->second.snap.can_go_next);
          readFlag("CanGoPrevious", cacheIt->second.snap.can_go_previous);
          readFlag("CanPlay", cacheIt->second.snap.can_play);
          readFlag("CanPause", cacheIt->second.snap.can_pause);

          const bool sigChanged = metadataChanged || positionChanged || changedProps.contains("PlaybackStatus");
          if (sigChanged) {
            const auto& s = cacheIt->second.snap;
            eh::shell_log::dock_mpris("sig bus=", busName, " meta=", metadataChanged, " pos=", positionChanged,
                                      " pb=", changedProps.contains("PlaybackStatus"),
                                      " title=\"", s.title, "\" artist=\"", s.artist, "\" pb_stat=", s.playback_status);
            m_cacheChanged = true;
          }
        });

    proxy->uponSignal("Seeked")
        .onInterface(kPlayerIface)
        .call([this, busName](int64_t positionUs) {
          std::lock_guard<std::mutex> lock(mu_);
          auto cacheIt = m_playerCache.find(busName);
          if (cacheIt == m_playerCache.end()) return;
          if (positionUs >= 0) {
            cacheIt->second.snap.position_us = positionUs;
            cacheIt->second.lastPositionAt = std::chrono::steady_clock::now();
            cacheIt->second.authoritativePosition = true;
          }
          m_cacheChanged = true;
        });

    {
      std::lock_guard<std::mutex> lock(mu_);
      m_playerProxies[busName] = std::move(proxy);
    }

    refresh_player_properties(busName);
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("add_player failed bus=", busName, " — ", e.what());
  }
}

void DockMpris::remove_player(const std::string& busName) {
   
  m_playerCache.erase(busName);
  m_playerProxies.erase(busName);
}

void DockMpris::refresh_player_properties(const std::string& busName) {
   
  if (!bus_) return;
  const std::weak_ptr<void> guard = m_aliveGuard;

  {
    std::lock_guard<std::mutex> lock(mu_);
    auto proxyIt = m_playerProxies.find(busName);
    if (proxyIt == m_playerProxies.end()) {
      return;
    }
    auto& proxy = *proxyIt->second;

    try {
      proxy.callMethodAsync("GetAll")
          .onInterface(kPropsIface)
          .withArguments(std::string{kRootIface})
          .uponReplyInvoke([this, guard, busName](std::optional<sdbus::Error> rootErr,
                                                  std::map<std::string, sdbus::Variant> rootProps) {
             
            if (guard.expired()) return;

            // Look up proxy again under lock, call within lock scope (callMethodAsync is synchronous)
            std::lock_guard<std::mutex> lock(mu_);
            auto pit = m_playerProxies.find(busName);
            if (pit == m_playerProxies.end()) return;
            auto& proxy2 = *pit->second;

            proxy2.callMethodAsync("GetAll")
                .onInterface(kPropsIface)
                .withArguments(std::string{kPlayerIface})
                .uponReplyInvoke([this, guard, busName, rootErr, rootProps = std::move(rootProps)](
                                     std::optional<sdbus::Error> playerErr,
                                     std::map<std::string, sdbus::Variant> playerProps) {
                   
                  if (guard.expired()) return;

                  if (rootErr.has_value() && playerErr.has_value()) {
                    eh::shell_log::mpris_dbus("GetAll failed bus=", busName,
                                              " root=", rootErr->what(), " player=", playerErr->what());
                    return;
                  }

                  std::map<std::string, sdbus::Variant> er, ep;
                  if (!rootErr.has_value()) er = std::move(rootProps);
                  if (!playerErr.has_value()) ep = std::move(playerProps);

                  process_async_properties(busName, er, ep, false);
                });
          });
    } catch (const std::exception& e) {
      eh::shell_log::mpris_dbus("refresh_player_properties async failed bus=", busName, " — ", e.what());
    }
  }
}

void DockMpris::diag_log_if_signature_changed(int sig_before) {
   
  int sig_after;
  PlayerSnapshot s;
  {
    std::lock_guard<std::mutex> lock(mu_);
    sig_after = last_signature_;
    s = snap_;
  }
  if (sig_after == sig_before) return;
  auto trunc = [](std::string_view t) -> std::string {
    constexpr size_t kMax = 80;
    if (t.size() <= kMax) return std::string(t);
    return std::string(t.substr(0, kMax)) + "...";
  };
  const std::string bus = s.bus_name.empty() ? std::string("(none)") : s.bus_name;
  const std::string st = s.playback_status.empty() ? std::string("(empty)") : s.playback_status;
  eh::shell_log::mpris_dbus("now-playing bus=", bus, " status=", st, " title=\"", trunc(s.title), "\" artist=\"", trunc(s.artist),
                            "\" sig ", sig_before, "\u2192", sig_after);
}

int DockMpris::compute_signature(const PlayerSnapshot& s) {
   
  int h = static_cast<int>(s.active);
  h = h * 31 + static_cast<int>(s.title.size());
  h = h * 31 + static_cast<int>(s.artist.size());
  for (unsigned char c : s.playback_status) h = h * 31 + c;
  h = h * 31 + (s.can_go_next ? 1 : 0);
  h = h * 31 + (s.can_go_previous ? 1 : 0);
  h = h * 31 + (s.can_play ? 1 : 0);
  h = h * 31 + (s.can_pause ? 1 : 0);
  h = h * 31 + static_cast<int>(s.duration_us);

  if (!s.title.empty()) h = h * 31 + static_cast<unsigned char>(s.title[0]);
  if (s.title.size() > 1) h = h * 31 + static_cast<unsigned char>(s.title.back());
  if (!s.artist.empty()) h = h * 31 + static_cast<unsigned char>(s.artist[0]);
  h = h * 31 + static_cast<int>(s.art_url_resolved.size());
  if (!s.art_url_resolved.empty()) {
    h = h * 31 + static_cast<unsigned char>(s.art_url_resolved[0]);
    if (s.art_url_resolved.size() > 1) h = h * 31 + static_cast<unsigned char>(s.art_url_resolved.back());
  }
  h = h * 31 + static_cast<int>(s.art_loaded_from_url.size());
  h = h * 31 + (s.art ? 17 : 0);
  return h;
}

void DockMpris::update_now_playing_art_assume_locked() {
   
  if (last_track_notify_key_.empty()) return;
  const PlayerSnapshot& s = snap_;
  std::string title = s.title;
  std::string artist = s.artist;
  trim_in_place(title);
  trim_in_place(artist);
  if (title.empty() && artist.empty()) return;
  if (!s.art) return;
  std::optional<eh::shell::notifications::NotificationImageData> album_art =
      cairo_surface_to_notification_image(s.art.get());
  eh::shell_log::mpris_dbus("update_now_playing_art: key=\"", last_track_notify_key_, "\" art=", album_art.has_value());
  if (album_art) {
    eh::notify::push_mpris_art(last_track_notify_key_, *album_art);
  }
}

bool DockMpris::poll_art_completions() {
   
  if (!art_future_.valid()) return false;
  if (art_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
  std::pair<std::string, AlbumArtSurfacePtr> pr;
  try {
    pr = art_future_.get();
  } catch (const std::exception& e) {
    std::cerr << "[mpris] album art load: " << e.what() << "\n";
    art_future_ = {};
    return false;
  } catch (...) {
    std::cerr << "[mpris] album art load: unknown error\n";
    art_future_ = {};
    return false;
  }
  art_future_ = {};
  std::lock_guard<std::mutex> lock(mu_);
  if (pr.first == snap_.art_url_resolved) {
    if (pr.second) {
      snap_.art = std::move(pr.second);
      snap_.art_loaded_from_url = pr.first;
      art_last_attempt_url_.clear();
      update_now_playing_art_assume_locked();
      eh::shell_log::mpris_dbus("poll_art_completions: loaded art ", pr.first, " — updated notification key=", last_track_notify_key_);
      return true;
    }
    art_last_attempt_url_ = pr.first;
  }
  return false;
}

void DockMpris::kick_art_load(const std::string& resolved) {
   
  if (resolved.empty()) return;
  (void)poll_art_completions();
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (snap_.art && snap_.art_loaded_from_url == resolved) return;
    if (art_future_.valid()) return;
    if (art_last_attempt_url_ == resolved && !snap_.art) return;
    art_last_attempt_url_ = resolved;
  }
  art_future_ = std::async(std::launch::async, [resolved]() {
    cairo_surface_t* raw = load_album_art_surface(resolved, 512);
    return std::make_pair(resolved, adopt_album_art_surface(raw));
  });
}

bool DockMpris::poll_refresh() {
   
  const int sig_before = [&]() {
    std::lock_guard<std::mutex> lock(mu_);
    return last_signature_;
  }();
  try {
    return poll_refresh_impl(sig_before);
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("poll_refresh: ", e.what());
    diag_log_if_signature_changed(sig_before);
    return false;
  } catch (...) {
    eh::shell_log::mpris_dbus("poll_refresh: unknown exception");
    diag_log_if_signature_changed(sig_before);
    return false;
  }
}

bool DockMpris::poll_refresh_impl(int sig_before) {
   
  process_pending_events();

  bool changed = poll_art_completions();
  if (!bus_) {
    diag_log_if_signature_changed(sig_before);
    return changed;
  }

  const auto now = std::chrono::steady_clock::now();
  if (m_needRediscovery || now - m_lastFullDiscovery > kFullRediscoveryInterval) {
    discover_all_players();
    m_needRediscovery = false;
  }

  std::string bl_raw;
  std::string pf_raw;
  if (const char* e = std::getenv("EH_MPRIS_BLACKLIST"); e && *e)
    bl_raw = e;
  else
    bl_raw = eh::config::shell_config_snapshot().mprisBlacklist;
  if (const char* e = std::getenv("EH_MPRIS_PREFERRED"); e && *e)
    pf_raw = e;
  else
    pf_raw = eh::config::shell_config_snapshot().mprisPreferred;

  std::vector<std::string> bl_tokens = split_comma_list(bl_raw);
  std::vector<std::string> pf_tokens = split_comma_list(pf_raw);
  for (auto& t : bl_tokens) t = lower_copy(t);
  for (auto& t : pf_tokens) t = lower_copy(t);

  std::vector<RankedCandidate> ranked;

  std::string last_chosen_copy;
  {
    std::unique_lock<std::mutex> lock(mu_);
    last_chosen_copy = last_chosen_bus_;

    if (!m_cacheChanged) {
      lock.unlock();
      diag_log_if_signature_changed(sig_before);
      return changed;
    }
    m_cacheChanged = false;

    ranked.reserve(m_playerCache.size());
    for (auto& [busName, ps] : m_playerCache) {
      const std::string bus_l = lower_copy(busName);
      if (bus_blacklisted(bus_l, bl_tokens)) continue;

      RankedCandidate rc;
      rc.s = ps.snap;
      rc.rank = ps.rank;
      rc.playing_since = ps.playingSince;
      ranked.push_back(std::move(rc));
    }
  }

  if (ranked.empty()) {
    PlayerSnapshot empty{};
    const int sig = compute_signature(empty);
    const bool ch = (sig != last_signature_);
    {
      std::lock_guard<std::mutex> lock(mu_);
      last_chosen_bus_.clear();
      snap_ = std::move(empty);
      last_signature_ = sig;
      art_last_attempt_url_.clear();
      last_track_notify_key_.clear();
    }
    diag_log_if_signature_changed(sig_before);
    return changed || ch;
  }

  int bestRank = 0;
  for (const auto& r : ranked) bestRank = std::max(bestRank, r.rank);

  std::vector<RankedCandidate> tier;
  for (auto& r : ranked) {
    if (r.rank == bestRank) tier.push_back(std::move(r));
  }
  auto bestIt = std::max_element(tier.begin(), tier.end(), [&](const RankedCandidate& a, const RankedCandidate& b) {
    return worse_than(a, b, bestRank, pf_tokens, last_chosen_copy);
  });

  std::string kick_resolved;
  {
    std::lock_guard<std::mutex> lock(mu_);

    PlayerSnapshot best = std::move(bestIt->s);
    if (bestRank >= 0 && !best.bus_name.empty())
      last_chosen_bus_ = best.bus_name;
    else
      last_chosen_bus_.clear();

    const std::string old_bus = snap_.bus_name;
    const AlbumArtSurfacePtr old_art = snap_.art;
    const std::string old_resolved = snap_.art_url_resolved;
    const std::string old_loaded_from = snap_.art_loaded_from_url;
    const std::string old_art_raw = snap_.art_url_raw;
    const std::string old_track_raw = snap_.track_url_raw;

    if (best.bus_name == old_bus) {
      if (best.art_url_raw.empty() && !old_art_raw.empty()) best.art_url_raw = old_art_raw;
      if (best.track_url_raw.empty() && !old_track_raw.empty()) best.track_url_raw = old_track_raw;
      if (best.title.empty() && best.artist.empty() &&
          !snap_.title.empty() && !snap_.artist.empty()) {
         
        best.title = snap_.title;
        best.artist = snap_.artist;
        best.album = snap_.album;
      }
    }

    {
      std::string art_src = best.art_url_raw;
      if (art_src.empty()) {
        const std::string yt = derive_youtube_thumbnail_url(best.track_url_raw);
        if (!yt.empty()) art_src = yt;
      }
      best.art_url_resolved = resolve_mpris_art_url(art_src);
    }

    if (best.bus_name != old_bus) {
      best.art.reset();
      best.art_loaded_from_url.clear();
      art_last_attempt_url_.clear();
    } else {
      if (best.art_url_resolved == old_resolved) {
        best.art = old_art;
        best.art_loaded_from_url = old_loaded_from;
      } else {
        best.art.reset();
        best.art_loaded_from_url.clear();
      }
    }

    const PlayerState* psPtr = nullptr;
    {
      auto cacheIt = m_playerCache.find(best.bus_name);
      if (cacheIt != m_playerCache.end()) psPtr = &cacheIt->second;
    }

    kick_resolved = best.art_url_resolved;

    if (psPtr) {
      best.position_us = projected_position(best, *psPtr);
    }

    const int sig = compute_signature(best);
    const bool sigDiff = (sig != last_signature_);
    changed = changed || sigDiff;
    eh::shell_log::dock_mpris("poll best=\"", best.title, "\" artist=\"", best.artist,
                              "\" bus=", best.bus_name, " pb=", best.playback_status,
                              " old_sig=", last_signature_, " new_sig=", sig, " diff=", sigDiff, " changed=", changed);
    snap_ = std::move(best);
    last_signature_ = sig;
  }

  kick_art_load(kick_resolved);
  const bool art_loaded = poll_art_completions();
  changed = changed || art_loaded;

  {
    std::lock_guard<std::mutex> lock(mu_);
    maybe_emit_now_playing_notify_assume_locked();
  }

  diag_log_if_signature_changed(sig_before);
  return changed;
}

PlayerSnapshot DockMpris::snapshot() const {
   
  std::lock_guard<std::mutex> lock(mu_);

  PlayerSnapshot s = snap_;

  auto cacheIt = m_playerCache.find(s.bus_name);
  if (cacheIt != m_playerCache.end()) {
    s.position_us = projected_position(s, cacheIt->second);
  }

  return s;
}

bool DockMpris::poll_position() {
   
  std::string active_bus;
  {
    std::lock_guard<std::mutex> lock(mu_);
    active_bus = snap_.bus_name;
  }
  if (active_bus.empty()) return false;

  std::lock_guard<std::mutex> lock(mu_);
  auto cacheIt = m_playerCache.find(active_bus);
  if (cacheIt != m_playerCache.end()) {
    snap_.position_us = projected_position(cacheIt->second.snap, cacheIt->second);
    return true;
  }
  return false;
}

static void async_player_method_call(sdbus::IConnection& bus,
                                     const std::string& svc,
                                     const std::string& method,
                                     std::shared_ptr<void> guard) {
   
  std::weak_ptr<void> wg = guard;
  try {
    auto proxy = sdbus::createProxy(bus, sdbus::ServiceName{svc}, sdbus::ObjectPath{kMprisPath});
    auto sharedProxy = std::shared_ptr<sdbus::IProxy>(std::move(proxy));
    sharedProxy->callMethodAsync(method)
        .onInterface(kPlayerIface)
        .uponReplyInvoke([wg, method](std::optional<sdbus::Error> err) {
          if (!err) return;
          if (wg.expired()) return;
          eh::shell_log::mpris_dbus(method, " async: ", err->what());
        });
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus(method, " async: ", e.what());
  }
}

void DockMpris::play_pause() {
   
  if (!bus_) return;
  std::string svc;
  {
    std::lock_guard<std::mutex> lock(mu_);
    svc = snap_.bus_name;
  }
  if (svc.empty()) return;
  async_player_method_call(*bus_, svc, "PlayPause", m_aliveGuard);
}

void DockMpris::next() {
   
  if (!bus_) return;
  std::string svc;
  {
    std::lock_guard<std::mutex> lock(mu_);
    svc = snap_.bus_name;
  }
  if (svc.empty()) return;
  async_player_method_call(*bus_, svc, "Next", m_aliveGuard);
}

void DockMpris::previous() {
   
  if (!bus_) return;
  std::string svc;
  {
    std::lock_guard<std::mutex> lock(mu_);
    svc = snap_.bus_name;
  }
  if (svc.empty()) return;
  async_player_method_call(*bus_, svc, "Previous", m_aliveGuard);
}

void DockMpris::set_position(const int64_t position_us) {
   
  if (!bus_) return;
  std::string svc;
  std::string track_id;
  {
    std::lock_guard<std::mutex> lock(mu_);
    svc = snap_.bus_name;
    track_id = snap_.track_id;
  }
  if (svc.empty() || track_id.empty()) return;
  std::weak_ptr<void> wg = m_aliveGuard;
  try {
    auto proxy = sdbus::createProxy(*bus_, sdbus::ServiceName{svc}, sdbus::ObjectPath{kMprisPath});
    auto sharedProxy = std::shared_ptr<sdbus::IProxy>(std::move(proxy));
    sharedProxy->callMethodAsync("SetPosition")
        .onInterface(kPlayerIface)
        .withArguments(sdbus::ObjectPath{track_id}, position_us)
        .uponReplyInvoke([wg](std::optional<sdbus::Error> err) {
          if (!err) return;
          if (wg.expired()) return;
          eh::shell_log::mpris_dbus("SetPosition async: ", err->what());
        });
  } catch (const std::exception& e) {
    eh::shell_log::mpris_dbus("SetPosition async: ", e.what());
  }
}

int DockMpris::media_hit_zone(const double local_x, const double slot_width, const double icon_ref_px) {
   
  if (slot_width <= 1.0 || icon_ref_px <= 0.0) return -1;

  const double s = icon_ref_px / 30.0;
  const double bsr = 16.0 * s * 0.5;
  const double bpr = 19.0 * s * 0.5;
  const double btnGap = std::max(4.0, 6.0 * s);
  const double rad = std::max(2.0, std::min(icon_ref_px, slot_width) * 0.22);
  const double innerH = rad + 4.0;
  const double artDia = icon_ref_px * 0.78;

  const double ctrl_right = slot_width - innerH;
  const double next_cx = ctrl_right - bsr;
  const double play_cx = next_cx - bsr - btnGap - bpr;
  const double prev_cx = play_cx - bpr - btnGap - bsr;

  if (local_x >= prev_cx - bsr && local_x <= prev_cx + bsr) return 0;
  if (local_x >= play_cx - bpr && local_x <= play_cx + bpr) return 1;
  if (local_x >= next_cx - bsr && local_x <= next_cx + bsr) return 2;
  if (local_x >= innerH && local_x < innerH + artDia) return 3;
  return -1;
}

}
