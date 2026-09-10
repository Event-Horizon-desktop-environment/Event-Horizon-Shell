#pragma once

#include <cstdint>
#include <string>

namespace eh::shell::overview {

class Host;

class RevealPass {
public:
  explicit RevealPass(Host& host);
  ~RevealPass() = default;

  RevealPass(const RevealPass&) = delete;
  RevealPass& operator=(const RevealPass&) = delete;

  void tick();
  void reset();

private:
  enum class Stage : uint8_t { Idle, Revealed, Syncing, Restoring };

  bool pick_and_focus();
  bool target_captured() const;
  void restore_focus();
  bool query_cursor_pos(int& outX, int& outY) const;

  Host& host_;

  Stage stage = Stage::Idle;
  uint64_t deadline_ms = 0;
  std::string targetAddr;
  int wsId = -1;
  std::string origAddr;
  int attempts = 0;
  uint64_t next_ms_ = 0;
};

} // namespace eh::shell::overview
