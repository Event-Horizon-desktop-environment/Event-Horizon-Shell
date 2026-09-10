#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eh::service::vramboost {

// Pure helpers for the Linux device-memory (dmem) cgroup controller. Kept free
// of Wayland/compositor types so they can be unit-tested standalone.

struct DmemRegion {
  std::string name{};
  uint64_t capacity = 0;
};

// Lowercase, strip path prefixes and .exe/.desktop/.AppImage suffixes so a
// toplevel app_id can be compared to a /proc/<pid>/comm value.
[[nodiscard]] std::string normalize_app_id(std::string_view appId);

// True when a process comm matches a normalized app_id: exact match, or a
// suffix match with a reasonably specific comm (e.g. app_id
// "com.cdprojektred.cyberpunk2077" vs comm "cyberpunk2077").
[[nodiscard]] bool comm_matches(std::string_view comm, std::string_view normalizedAppId);

// True when the cgroup v2 controllers string advertises the "dmem" controller.
[[nodiscard]] bool controllers_have_dmem(std::string_view controllers);

// Parse <region> <bytes> lines from dmem.capacity output into regions.
[[nodiscard]] bool parse_dmem_capacity(std::string_view text, std::vector<DmemRegion>& out);

// Parse <region> <bytes> lines from dmem.current and return usage of `region`.
[[nodiscard]] bool parse_dmem_current(std::string_view text, std::string_view region, uint64_t* bytes);

// Read /sys/fs/cgroup/dmem.capacity. Returns {} when unavailable.
[[nodiscard]] std::vector<DmemRegion> probe_dmem_regions();

// Prefer the region whose name contains "vram", else the first region.
[[nodiscard]] std::string primary_vram_region(const std::vector<DmemRegion>& regions);

} // namespace eh::service::vramboost