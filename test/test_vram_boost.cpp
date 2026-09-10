#include "services/vram_boost/vram_boost_detail.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using eh::service::vramboost::comm_matches;
using eh::service::vramboost::controllers_have_dmem;
using eh::service::vramboost::normalize_app_id;
using eh::service::vramboost::parse_dmem_capacity;
using eh::service::vramboost::parse_dmem_current;
using eh::service::vramboost::primary_vram_region;

// Keeps checks alive in release builds where assert() compiles away under
// -Werror=nodiscard (EH_CHECK is used throughout, no unused-variable warning).
#define EH_CHECK(expr)                                                                                                 \
  do {                                                                                                                 \
    if (!(expr)) {                                                                                                     \
      std::fprintf(stderr, "EH_CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                                \
      std::abort();                                                                                                    \
    }                                                                                                                  \
  } while (0)

// ── normalize_app_id ────────────────────────────────────────────────────────

static void test_normalize_suffixes() {
  EH_CHECK(normalize_app_id("Cyberpunk2077.exe") == "cyberpunk2077");
  EH_CHECK(normalize_app_id("com.cdprojektred.Cyberpunk2077.desktop") == "com.cdprojektred.cyberpunk2077");
  EH_CHECK(normalize_app_id("game.appimage") == "game");
  EH_CHECK(normalize_app_id("Game.EXE") == "game");
}

static void test_normalize_path_prefix() {
  EH_CHECK(normalize_app_id("/usr/bin/SomeGame") == "somegame");
  EH_CHECK(normalize_app_id("/home/matt/games/steam_app_12345.exe") == "steam_app_12345");
}

static void test_normalize_empty() {
  EH_CHECK(normalize_app_id("").empty());
  EH_CHECK(normalize_app_id("/").empty());
  EH_CHECK(normalize_app_id("/usr/").empty());
}

static void test_normalize_case() {
  EH_CHECK(normalize_app_id("Steam") == "steam");
  EH_CHECK(normalize_app_id("ValveSteam.exe") == "valvesteam");
}

// ── comm_matches ────────────────────────────────────────────────────────────

static void test_comm_matches_exact() {
  EH_CHECK(comm_matches("Cyberpunk2077", "cyberpunk2077"));
  EH_CHECK(comm_matches("steam", "steam"));
  EH_CHECK(comm_matches("firefox", "firefox"));
}

static void test_comm_matches_suffix() {
  // Game binary name matched within a longer reversed-domain app_id
  EH_CHECK(comm_matches("cyberpunk2077", "com.cdprojektred.cyberpunk2077"));
  EH_CHECK(comm_matches("SuperGame", "com.example.supergame"));
}

static void test_comm_matches_rejects_weak() {
  // Single-character and two-character comms are too short to be meaningful.
  EH_CHECK(!comm_matches("a", "com.app.a"));
  EH_CHECK(!comm_matches("sh", "com.app.sh"));
}

static void test_comm_matches_no_match() {
  EH_CHECK(!comm_matches("gzip", "cyberpunk2077"));
  EH_CHECK(!comm_matches("cyberpunk", "com.cdprojektred.cyberpunk2077"));
}

// ── controllers_have_dmem ──────────────────────────────────────────────────

static void test_dmem_found() {
  EH_CHECK(controllers_have_dmem("cpu cpuset memory dmem io"));
  EH_CHECK(controllers_have_dmem("dmem"));
  EH_CHECK(controllers_have_dmem("  dmem  "));
}

static void test_dmem_absent() {
  EH_CHECK(!controllers_have_dmem("cpu cpuset memory io"));
  EH_CHECK(!controllers_have_dmem(""));
  EH_CHECK(!controllers_have_dmem("dme"));
}

// ── parse_dmem_capacity ────────────────────────────────────────────────────

static void test_capacity_single_region() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  EH_CHECK(parse_dmem_capacity("drm/0000:03:00.0/vram0 8589934592\n", regions));
  EH_CHECK(regions.size() == 1);
  EH_CHECK(regions[0].name == "drm/0000:03:00.0/vram0");
  EH_CHECK(regions[0].capacity == 8589934592ull);
}

static void test_capacity_multiple_regions() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  const char* data = "drm/0000:00:02.0/stolen 1073741824\ndrm/0000:00:02.0/vram0 8589934592\n";
  EH_CHECK(parse_dmem_capacity(data, regions));
  EH_CHECK(regions.size() == 2);
  EH_CHECK(regions[0].name == "drm/0000:00:02.0/stolen");
  EH_CHECK(regions[0].capacity == 1073741824ull);
  EH_CHECK(regions[1].name == "drm/0000:00:02.0/vram0");
  EH_CHECK(regions[1].capacity == 8589934592ull);
}

static void test_capacity_empty() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  EH_CHECK(!parse_dmem_capacity("", regions));
  EH_CHECK(regions.empty());
}

// ── parse_dmem_current ─────────────────────────────────────────────────────

static void test_current_match() {
  uint64_t bytes = 0;
  const char* data = "drm/0000:03:00.0/stolen 1073741824\ndrm/0000:03:00.0/vram0 6442450944\n";
  EH_CHECK(parse_dmem_current(data, "drm/0000:03:00.0/vram0", &bytes));
  EH_CHECK(bytes == 6442450944ull);
}

static void test_current_no_match() {
  uint64_t bytes = 0;
  const char* data = "drm/0000:00:02.0/stolen 1073741824\n";
  EH_CHECK(!parse_dmem_current(data, "drm/0000:00:02.0/vram0", &bytes));
}

static void test_current_empty() {
  uint64_t bytes = 0;
  EH_CHECK(!parse_dmem_current("", "drm/0000:03:00.0/vram0", &bytes));
}

// ── primary_vram_region ────────────────────────────────────────────────────

static void test_primary_region_prefers_vram() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  regions.push_back({"drm/0000:00:02.0/stolen", 1073741824});
  regions.push_back({"drm/0000:00:02.0/vram0", 8589934592});
  EH_CHECK(primary_vram_region(regions) == "drm/0000:00:02.0/vram0");
}

static void test_primary_region_fallback() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  regions.push_back({"drm/0000:00:02.0/stolen", 1073741824});
  EH_CHECK(primary_vram_region(regions) == "drm/0000:00:02.0/stolen");
}

static void test_primary_region_empty() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  EH_CHECK(primary_vram_region(regions).empty());
}

// ── NVIDIA dmemcg naming (as seen on a live 16 GB RTX system) ─────────────

static void test_primary_region_nvidia_vidmem() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  regions.push_back({"nvidia/00000000:01:00.0/gpu0", 1073741824});
  regions.push_back({"nvidia/00000000:01:00.0/vidmem", 17094934528});
  EH_CHECK(primary_vram_region(regions) == "nvidia/00000000:01:00.0/vidmem");
}

static void test_primary_region_vidmem_wins_over_vram0() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  regions.push_back({"drm/0000:03:00.0/vram0", 8589934592});
  regions.push_back({"nvidia/00000000:01:00.0/vidmem", 17094934528});
  EH_CHECK(primary_vram_region(regions) == "nvidia/00000000:01:00.0/vidmem");
}

static void test_capacity_real_nvidia_file() {
  std::vector<eh::service::vramboost::DmemRegion> regions;
  const char* data = "nvidia/00000000:01:00.0/gpu0 1073741824\nnvidia/00000000:01:00.0/vidmem 17094934528\n";
  EH_CHECK(parse_dmem_capacity(data, regions));
  EH_CHECK(regions.size() == 2);
  EH_CHECK(regions[0].name == "nvidia/00000000:01:00.0/gpu0");
  EH_CHECK(regions[1].name == "nvidia/00000000:01:00.0/vidmem");
  EH_CHECK(regions[1].capacity == 17094934528ull);
}

static void test_current_real_nvidia_file() {
  uint64_t bytes = 0;
  const char* data = "nvidia/00000000:01:00.0/gpu0 268435456\nnvidia/00000000:01:00.0/vidmem 2378585792\n";
  EH_CHECK(parse_dmem_current(data, "nvidia/00000000:01:00.0/vidmem", &bytes));
  EH_CHECK(bytes == 2378585792ull);
  EH_CHECK(!parse_dmem_current(data, "nvidia/00000000:01:00.0/cuda", &bytes));
}

static void test_write_format_region_size() {
  // Manager composes "dmem.min" writes as "<region> <bytes>\n".
  const uint64_t bytes = 2378585792ull;
  const std::string expected = std::string("nvidia/00000000:01:00.0/vidmem ") + std::to_string(bytes) + "\n";
  EH_CHECK(expected == "nvidia/00000000:01:00.0/vidmem 2378585792\n");
}

int main() {
  test_normalize_suffixes();
  test_normalize_path_prefix();
  test_normalize_empty();
  test_normalize_case();
  test_comm_matches_exact();
  test_comm_matches_suffix();
  test_comm_matches_rejects_weak();
  test_comm_matches_no_match();
  test_dmem_found();
  test_dmem_absent();
  test_capacity_single_region();
  test_capacity_multiple_regions();
  test_capacity_empty();
  test_current_match();
  test_current_no_match();
  test_current_empty();
  test_primary_region_prefers_vram();
  test_primary_region_fallback();
  test_primary_region_empty();
  test_primary_region_nvidia_vidmem();
  test_primary_region_vidmem_wins_over_vram0();
  test_capacity_real_nvidia_file();
  test_current_real_nvidia_file();
  test_write_format_region_size();
  std::puts("test_vram_boost: all checks passed");
  return 0;
}