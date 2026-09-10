// Live integration test for the dmem (VRAM) cgroup controller.
//
// Validates the actual kernel interface that VramBoostManager rides on:
//   - dmem controller present and dmem.capacity parseable
//   - primary region selected with the same rules the manager uses
//   - a fresh child cgroup exposes dmem.current/dmem.min
//   - dmem.min can be written (region + bytes) and reads back
//
// Requires CAP_SYS_ADMIN on the root cgroup tree. When dmem is unavailable or
// the caller can't create child cgroups, the test is SKIPPED (exit 77) so it
// never blocks normal `meson test` runs on dmem-less hosts.

#include "services/vram_boost/vram_boost_detail.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr const char* kCgroupRoot = "/sys/fs/cgroup";
constexpr const char* kCapacityFile = "/sys/fs/cgroup/dmem.capacity";

bool read_text_file(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f.is_open()) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool write_file(const std::string& path, const std::string& content) {
  const int fd = ::open(path.c_str(), O_WRONLY);
  if (fd < 0) return false;
  const ssize_t n = ::write(fd, content.data(), content.size());
  const int saved = errno;
  ::close(fd);
  if (n < 0) {
    errno = saved;
    return false;
  }
  return true;
}

bool contains_line_with(const std::string& text, const std::string& needle, const std::string& region) {
  std::istringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.find(region) != std::string::npos && line.find(needle) != std::string::npos) return true;
  }
  return false;
}

int fail(const char* what, bool skip) {
  std::fprintf(stderr, "[vram_boost_live] %s\n", what);
  return skip ? 77 : 1;
}

} // namespace

int main() {
  // dmem available at all?
  {
    std::string controllers;
    if (!read_text_file(std::string(kCgroupRoot) + "/cgroup.controllers", controllers) ||
        !eh::service::vramboost::controllers_have_dmem(controllers)) {
      return fail("dmem controller not available — skipped", true);
    }
  }

  std::string capacity;
  if (!read_text_file(kCapacityFile, capacity)) {
    return fail("cannot read dmem.capacity — skipped", true);
  }
  std::vector<eh::service::vramboost::DmemRegion> regions;
  if (!eh::service::vramboost::parse_dmem_capacity(capacity, regions) || regions.empty()) {
    return fail("dmem.capacity empty/unparseable — skipped", true);
  }
  const std::string region = eh::service::vramboost::primary_vram_region(regions);
  if (region.empty()) return fail("no usable dmem region — skipped", true);

  const std::string cgDir = std::string(kCgroupRoot) + "/eh_vram_live_test_" + std::to_string(static_cast<long>(::getpid()));
  if (::mkdir(cgDir.c_str(), 0755) != 0) {
    if (errno == EACCES || errno == EPERM || errno == EROFS) {
      return fail("cannot create child cgroup (unprivileged or read-only fs) — skipped", true);
    }
    return fail(("mkdir " + cgDir + " failed: " + std::strerror(errno)).c_str(), false);
  }

  bool ok = true;
  // Park a child process inside the new cgroup so it has a member.
  const pid_t child = ::fork();
  if (child == 0) {
    // Child stays alive until killed by the parent's cleanup.
    for (;;) ::pause();
    _exit(1);
  }
  if (child < 0) {
    ::rmdir(cgDir.c_str());
    return fail("fork failed", false);
  }
  if (!write_file(cgDir + "/cgroup.procs", std::to_string(static_cast<long>(child)) + "\n")) {
    std::fprintf(stderr, "[vram_boost_live] cgroup.procs write failed: %s\n", std::strerror(errno));
    ::kill(child, SIGKILL);
    ::rmdir(cgDir.c_str());
    return fail("cannot attach process to test cgroup — skipped", true);
  }

  std::string currentText;
  const uint64_t baseline = 4096;
  if (read_text_file(cgDir + "/dmem.current", currentText)) {
    uint64_t parsed = 0;
    if (eh::service::vramboost::parse_dmem_current(currentText, region, &parsed) && parsed > 0) {
      std::printf("[vram_boost_live] region '%s' current usage in test cgroup: %llu bytes\n",
                  region.c_str(), static_cast<unsigned long long>(parsed));
    }
  }

  // Write a floor above the current usage (or a small nonzero floor if empty).
  uint64_t committed = baseline;
  {
    std::string t;
    if (read_text_file(cgDir + "/dmem.current", t)) {
      uint64_t u = 0;
      if (eh::service::vramboost::parse_dmem_current(t, region, &u)) committed = u + baseline;
    }
  }

  const std::string limit = region + " " + std::to_string(committed) + "\n";
  if (!write_file(cgDir + "/dmem.min", limit)) {
    std::fprintf(stderr, "[vram_boost_live] dmem.min write failed: %s\n", std::strerror(errno));
    ok = false;
  }

  std::string readback;
  if (ok && !read_text_file(cgDir + "/dmem.min", readback)) {
    ok = false;
  }
  if (ok && !contains_line_with(readback, std::to_string(committed), region)) {
    std::fprintf(stderr, "[vram_boost_live] dmem.min readback mismatch: '%s'\n", readback.c_str());
    ok = false;
  }

  // Restore and tear down.
  (void)write_file(cgDir + "/dmem.min", region + " 0\n");
  ::kill(child, SIGKILL);
  ::waitpid(child, nullptr, 0);
  if (::rmdir(cgDir.c_str()) != 0) {
    std::fprintf(stderr, "[vram_boost_live] rmdir failed: %s\n", std::strerror(errno));
  }

  if (!ok) return fail("dmem.min round-trip failed", false);
  std::printf("[vram_boost_live] OK: region '%s', dmem.min round-trip successful\n", region.c_str());
  return 0;
}