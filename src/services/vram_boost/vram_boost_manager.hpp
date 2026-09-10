#pragma once

#include "services/vram_boost/vram_boost_detail.hpp"
#include "wl/toplevel/foreign_toplevels.hpp"

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eh::service {

// Follows the focused fullscreen toplevel and protects its device-memory from
// eviction by raising the owning cgroup's dmem.min/dmem.low to its current VRAM
// residency. When a background window needs VRAM, the DRM/ TTM eviction path
// must pick the background windows first (their cgroups carry no protection).
// Falls back silently to a no-op when the kernel lacks the dmem cgroup
// controller or no GPU memory region is registered. When the target cgroup
// lives in a delegated (user-writable) subtree that does not yet expose the
// dmem controller, it is enabled automatically along the ancestor chain.
class VramBoostManager {
public:
  explicit VramBoostManager(eh::wayland::ForeignToplevels& toplevels);
  ~VramBoostManager();

  VramBoostManager(const VramBoostManager&) = delete;
  VramBoostManager& operator=(const VramBoostManager&) = delete;
  VramBoostManager(VramBoostManager&&) = delete;
  VramBoostManager& operator=(VramBoostManager&&) = delete;

  void start();
  void stop();

  [[nodiscard]] bool available() const { return available_; }
  [[nodiscard]] bool active() const { return !boosted_.empty(); }
  [[nodiscard]] const std::string& boosted_app_id() const { return boostedAppId_; }

private:
  struct BoostedCgroup {
    std::string dir{}; // full path under /sys/fs/cgroup
    uint64_t bytes = 0;
    std::string prevMin{};
    std::string prevLow{};
  };

  void probe();
  void reevaluate();
  void apply_boost(const void* handle, const std::string& appId, bool fullscreen);
  void refresh_boost();
  void clear_boost();

  [[nodiscard]] bool enabled() const;
  [[nodiscard]] bool only_fullscreen() const;

  std::vector<pid_t> resolve_pids(const std::string& normalizedAppId);
  [[nodiscard]] std::string detect_heaviest_app_scope() const;
  [[nodiscard]] std::string cgroup_dir_for_pid(pid_t pid) const;
  [[nodiscard]] bool read_text_file(const std::string& path, std::string& out) const;
  [[nodiscard]] bool read_usage_bytes(const std::string& cgroupDir, uint64_t* bytes) const;
  [[nodiscard]] bool enable_dmem_subtree(const std::string& cgroupDir) const;
  [[nodiscard]] bool write_dmem_limit(const std::string& cgroupDir, const char* file, uint64_t bytes) const;
  [[nodiscard]] bool write_raw(const std::string& cgroupDir, const char* file, std::string_view content) const;

  eh::wayland::ForeignToplevels& toplevels_;
  uint64_t snapshotToken_ = 0;
  bool available_ = false;
  std::string region_{};
  std::vector<vramboost::DmemRegion> regions_{};

  const void* boostedHandle_ = nullptr;
  std::string boostedAppId_{};
  std::vector<BoostedCgroup> boosted_{};
};

} // namespace eh::service