#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <sdbus-c++/sdbus-c++.h>

#include "services/mpris/mpris_album_art.hpp"

namespace eh::mpris {

struct PlayerSnapshot {
  bool active = false;

  std::string bus_name{};
  std::string title{};
  std::string artist{};
  std::string album{};

  std::string art_url_raw{};

  std::string track_url_raw{};

  std::string art_url_resolved{};

  std::string art_loaded_from_url{};
  AlbumArtSurfacePtr art{};

  std::string track_id{};

  std::string playback_status{};
  bool can_go_next = false;
  bool can_go_previous = false;
  bool can_play = false;
  bool can_pause = false;

  int64_t position_us = 0;
  int64_t duration_us = 0;
};

struct RankedCandidate {
  PlayerSnapshot s;
  int rank = 0;
  std::chrono::steady_clock::time_point playing_since{};
};

class DockMpris {
public:
  DockMpris();
  ~DockMpris();

  DockMpris(const DockMpris&) = delete;
  DockMpris& operator=(const DockMpris&) = delete;
  DockMpris(DockMpris&&) = delete;
  DockMpris& operator=(DockMpris&&) = delete;

  bool poll_refresh();

  [[nodiscard]] PlayerSnapshot snapshot() const;

  bool poll_position();

  void play_pause();
  void next();
  void previous();
  void set_position(int64_t position_us);

  static int media_hit_zone(double local_x, double slot_width, double icon_ref_px);

  void process_pending_events();
  int poll_fd() const;
  int poll_event_fd() const;

private:
  mutable std::mutex mu_;
  PlayerSnapshot snap_;

  std::unique_ptr<sdbus::IConnection> bus_;

  int last_signature_ = 0;
  static int compute_signature(const PlayerSnapshot& s);

  std::future<std::pair<std::string, AlbumArtSurfacePtr>> art_future_{};
  std::string art_last_attempt_url_{};
  bool poll_art_completions();
  void kick_art_load(const std::string& resolved_url);

  std::string last_chosen_bus_;

  bool poll_refresh_impl(int sig_before);
  void diag_log_if_signature_changed(int sig_before);

  void maybe_emit_now_playing_notify_assume_locked();
  void update_now_playing_art_assume_locked();

  std::string last_track_notify_key_;

  std::unique_ptr<sdbus::IProxy> m_dbusProxy;
  std::unordered_map<std::string, std::unique_ptr<sdbus::IProxy>> m_playerProxies;

  struct PlayerState {
    PlayerSnapshot snap;
    int rank = 0;
    std::chrono::steady_clock::time_point playingSince;
    std::chrono::steady_clock::time_point lastPositionAt;
    bool authoritativePosition = false;
  };

  std::unordered_map<std::string, PlayerState> m_playerCache;

  std::chrono::steady_clock::time_point m_lastFullDiscovery{};
  bool m_needRediscovery = true;
  bool m_cacheChanged = false;

  std::shared_ptr<void> m_aliveGuard{std::make_shared<int>(0)};

  void setup_dbus_signals();
  void setup_name_owner_changed();

  void discover_all_players();
  void add_player(const std::string& busName);
  void remove_player(const std::string& busName);
  void refresh_player_properties(const std::string& busName);

  void process_async_properties(const std::string& busName,
                                const std::map<std::string, sdbus::Variant>& rootProps,
                                const std::map<std::string, sdbus::Variant>& playerProps,
                                bool hadPositionSignal);
  int64_t projected_position(const PlayerSnapshot& s, const PlayerState& ps) const;

  static bool is_mpris_bus_name(const std::string& name);
  static std::string variant_as_string(const sdbus::Variant& v);
  static int player_rank(const PlayerSnapshot& s);
};

}
