#include "services/vram_boost/vram_boost_manager.hpp"

#include "configuration/shell_config.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace eh::service {

namespace {

constexpr const char* kCgroupFsRoot = "/sys/fs/cgroup";
constexpr const char* kControllersFile = "/sys/fs/cgroup/cgroup.controllers";
constexpr const char* kDmemCapacityFile = "/sys/fs/cgroup/dmem.capacity";

bool vram_trace_enabled() {
  const char* e = std::getenv("EH_VRAM_BOOST_TRACE");
  return e != nullptr && e[0] != '\0' && e[0] != '0';
}

void vram_trace(const std::string& msg) {
  if (!vram_trace_enabled()) return;
  std::cerr << "[vramboost] " << msg << '\n';
  std::ofstream log("/tmp/eh-vramboost.log", std::ios::app);
  if (log) log << msg << '\n';
}

void warn_once_no_permission() {
  static bool warned = false;
  if (warned) return;
  warned = true;
  std::cerr << "[vramboost] cannot write dmem cgroup limits — VRAM boost disabled. "
               "The user account needs write access to the target cgroup\'s dmem.* files.\n";
}

} // namespace

VramBoostManager::VramBoostManager(eh::wayland::ForeignToplevels& toplevels) : toplevels_(toplevels) {}

VramBoostManager::~VramBoostManager() { stop(); }

void VramBoostManager::start() {
  probe();
  if (!available_) {
    vram_trace("no dmem cgroup support — VRAM boost inactive");
    return;
  }
  snapshotToken_ = toplevels_.add_snapshot_cb([this]() { reevaluate(); });
  reevaluate();
  vram_trace("VRAM boost active for region '" + region_ + "'");
}

void VramBoostManager::stop() {
  clear_boost();
  if (snapshotToken_ != 0) {
    toplevels_.remove_snapshot_cb(snapshotToken_);
    snapshotToken_ = 0;
  }
}

void VramBoostManager::probe() {
  std::string controllers;
  if (!read_text_file(kControllersFile, controllers)) {
    available_ = false;
    return;
  }
  if (!vramboost::controllers_have_dmem(controllers)) {
    available_ = false;
    return;
  }
  regions_ = vramboost::probe_dmem_regions();
  region_ = vramboost::primary_vram_region(regions_);
  available_ = !region_.empty();
  if (available_) {
    vram_trace("dmem controller present; region='" + region_ + "' capacity=" +
               std::to_string(regions_.empty() ? 0 : regions_.front().capacity));
  }
}

bool VramBoostManager::enabled() const {
  return eh::config::shell_config_snapshot().vramBoost.enabled;
}

bool VramBoostManager::only_fullscreen() const {
  return eh::config::shell_config_snapshot().vramBoost.onlyFullscreen;
}

void VramBoostManager::reevaluate() {
  if (!available_ || !enabled()) {
    clear_boost();
    return;
  }

  const bool onlyFs = only_fullscreen();
  const void* handle = nullptr;
  bool fullscreen = false;
  std::string appId;

  for (const auto& tl : toplevels_.list()) {
    vram_trace("toplevel: app_id='" + tl.appId + "' title='" + tl.title + "' activated=" +
               (tl.activated ? "yes" : "no") + " fullscreen=" + (tl.fullscreen ? "yes" : "no") +
               " maximized=" + (tl.maximized ? "yes" : "no") + " minimized=" +
               (tl.minimized ? "yes" : "no"));
  }

  for (const auto& tl : toplevels_.list()) {
    if (tl.closed || tl.handle == nullptr) continue;
    if (!tl.activated) continue;
    if (onlyFs && !tl.fullscreen) continue;
    handle = tl.handle;
    fullscreen = tl.fullscreen;
    appId = tl.appId;
    break;
  }

  if (handle == nullptr) {
    vram_trace("reevaluate: no activated" + (onlyFs ? std::string(" + fullscreen") : std::string()) +
               " toplevel; candidates=" + std::to_string(toplevels_.list().size()));
    clear_boost();
    return;
  }
  if (handle == boostedHandle_ && appId == boostedAppId_) {
    refresh_boost();
    return;
  }
  clear_boost();
  apply_boost(handle, appId, fullscreen);
}

void VramBoostManager::apply_boost(const void* handle, const std::string& appId, bool fullscreen) {
  boostedHandle_ = handle;
  boostedAppId_ = appId;

  const std::string normalized = vramboost::normalize_app_id(appId);

  // dmem charges VRAM to a whole cgroup, and Proton/Wine games spread their
  // work over many processes (wineserver, winedevice.exe, the game's own
  // renamed comm). Collect the unique cgroups for this app; if process-name
  // matching comes up empty, fall back to the scope under the user's app.slice
  // holding the most VRAM right now (the foreground/fullscreen game).
  std::unordered_set<std::string> seen;
  std::vector<std::string> targetDirs;
  if (!normalized.empty()) {
    for (const pid_t pid : resolve_pids(normalized)) {
      std::string dir = cgroup_dir_for_pid(pid);
      if (!dir.empty() && seen.insert(dir).second) targetDirs.push_back(std::move(dir));
    }
  }
  if (targetDirs.empty()) {
    const std::string fallbackDir = detect_heaviest_app_scope();
    if (!fallbackDir.empty()) {
      vram_trace("resolve: no comm match for app_id '" + appId + "'; boosting heaviest app scope '" +
                 fallbackDir + "'");
      if (seen.insert(fallbackDir).second) targetDirs.push_back(fallbackDir);
    }
  }
  if (targetDirs.empty()) {
    clear_boost();
    if (fullscreen) {
      vram_trace("boost: no cgroup found for app_id '" + appId + "' — boost skipped");
    }
    return;
  }

  std::vector<BoostedCgroup> applied;
  for (std::string& dir : targetDirs) {
    // Make sure dmem interface files exist for this cgroup. The dmem controller
    // may not be enabled in the delegated subtree yet (systemd starts with
    // cpu/memory/pids only) — enable it top-down along the ancestor chain where
    // the files are writable by us. No-op when we lack permission.
    (void)enable_dmem_subtree(dir);

    uint64_t bytes = 0;
    if (!read_usage_bytes(dir, &bytes) || bytes == 0) continue;

    BoostedCgroup cg;
    cg.dir = std::move(dir);
    cg.bytes = bytes;
    (void)read_text_file(cg.dir + "/dmem.min", cg.prevMin);
    (void)read_text_file(cg.dir + "/dmem.low", cg.prevLow);

    if (!write_dmem_limit(cg.dir, "dmem.low", bytes)) {
      warn_once_no_permission();
      continue;
    }
    if (!write_dmem_limit(cg.dir, "dmem.min", bytes)) {
      warn_once_no_permission();
      if (!cg.prevMin.empty()) (void)write_raw(cg.dir, "dmem.min", cg.prevMin);
      continue;
    }
    applied.push_back(std::move(cg));
  }

  if (applied.empty()) {
    clear_boost();
    if (fullscreen) {
      vram_trace("boost: no processes in matched cgroups — boost skipped");
    }
    return;
  }
  boosted_ = std::move(applied);
  vram_trace("boost: app_id '" + appId + "' protected (" + std::to_string(boosted_.size()) + " cgroups)");
}

void VramBoostManager::refresh_boost() {
  for (auto& cg : boosted_) {
    uint64_t bytes = 0;
    if (!read_usage_bytes(cg.dir, &bytes)) continue;
    if (bytes <= cg.bytes) continue;
    if (write_dmem_limit(cg.dir, "dmem.low", bytes) && write_dmem_limit(cg.dir, "dmem.min", bytes)) {
      cg.bytes = bytes;
      vram_trace("boost: raised '" + cg.dir + "' to " + std::to_string(bytes));
    }
  }
}

void VramBoostManager::clear_boost() {
  if (boosted_.empty() && boostedHandle_ == nullptr && boostedAppId_.empty()) return;
  for (const auto& cg : boosted_) {
    if (!cg.prevMin.empty()) {
      (void)write_raw(cg.dir, "dmem.min", cg.prevMin);
    } else {
      (void)write_dmem_limit(cg.dir, "dmem.min", 0);
    }
    if (!cg.prevLow.empty()) {
      (void)write_raw(cg.dir, "dmem.low", cg.prevLow);
    } else {
      (void)write_dmem_limit(cg.dir, "dmem.low", 0);
    }
  }
  boosted_.clear();
  boostedHandle_ = nullptr;
  boostedAppId_.clear();
  vram_trace("boost: reverted");
}

std::string VramBoostManager::detect_heaviest_app_scope() const {
  const std::string appSlice =
      std::string(kCgroupFsRoot) + "/user.slice/user-" + std::to_string(geteuid()) +
      ".slice/user@" + std::to_string(geteuid()) + ".service/app.slice";
  DIR* dir = ::opendir(appSlice.c_str());
  if (dir == nullptr) return {};

  std::string best;
  uint64_t bestBytes = 0;
  while (struct dirent* ent = ::readdir(dir)) {
    if (ent->d_name[0] == '.') continue;
    const std::string child = appSlice + "/" + ent->d_name;
    // Only leaf scopes hold app processes; app.slice children holding service
    // trees would resolve through their own processes instead.
    const std::string name(ent->d_name);
    if (name.find(".scope") == std::string::npos) continue;
    uint64_t bytes = 0;
    if (!read_usage_bytes(child, &bytes) || bytes == 0) continue;
    if (bytes > bestBytes) {
      bestBytes = bytes;
      best = child;
    }
  }
  ::closedir(dir);
  if (bestBytes > 0) {
    vram_trace("fallback: app.slice scopes scanned; heaviest='" + best + "' @" +
               std::to_string(bestBytes) + " bytes");
  }
  return best;
}

std::vector<pid_t> VramBoostManager::resolve_pids(const std::string& normalizedAppId) {
  std::vector<pid_t> pids;
  DIR* procDir = ::opendir("/proc");
  if (procDir == nullptr) return pids;

  while (struct dirent* ent = ::readdir(procDir)) {
    if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
    char commPath[64];
    const int plen = std::snprintf(commPath, sizeof(commPath), "/proc/%s/comm", ent->d_name);
    if (plen <= 0 || static_cast<size_t>(plen) >= sizeof(commPath)) continue;
    std::string comm;
    if (!read_text_file(commPath, comm)) continue;
    const size_t nl = comm.find('\n');
    if (nl != std::string::npos) comm.resize(nl);
    if (!vramboost::comm_matches(comm, normalizedAppId)) continue;
    char* end = nullptr;
    const long pid = std::strtol(ent->d_name, &end, 10);
    if (end != nullptr && *end == '\0' && pid > 0) pids.push_back(static_cast<pid_t>(pid));
  }
  ::closedir(procDir);
  return pids;
}

std::string VramBoostManager::cgroup_dir_for_pid(pid_t pid) const {
  char path[64];
  const int plen = std::snprintf(path, sizeof(path), "/proc/%d/cgroup", static_cast<int>(pid));
  if (plen <= 0 || static_cast<size_t>(plen) >= sizeof(path)) return {};
  std::string text;
  if (!read_text_file(path, text)) return {};

  const size_t pos = text.find("0::");
  if (pos == std::string::npos) return {};
  std::string cg = text.substr(pos + 3);
  const size_t nl = cg.find('\n');
  if (nl != std::string::npos) cg.resize(nl);
  if (cg.empty() || cg == "/") return {}; // root cgroup: dmem.min is not settable there
  return std::string(kCgroupFsRoot) + cg;
}

bool VramBoostManager::read_text_file(const std::string& path, std::string& out) const {
  std::ifstream f(path);
  if (!f.is_open()) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

bool VramBoostManager::read_usage_bytes(const std::string& cgroupDir, uint64_t* bytes) const {
  std::string text;
  if (!read_text_file(cgroupDir + "/dmem.current", text)) return false;
  return vramboost::parse_dmem_current(text, region_, bytes);
}

bool VramBoostManager::enable_dmem_subtree(const std::string& cgroupDir) const {
  // cgroupDir is like /sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/app.slice/app-x.scope
  const std::string_view root(kCgroupFsRoot);
  if (!cgroupDir.starts_with(root)) return false;
  std::string_view rel(cgroupDir);
  rel.remove_prefix(root.size());
  if (rel.empty()) return false;

  bool any = false;
  std::string cur(kCgroupFsRoot);
  std::string_view rest = rel;
  for (;;) {
    const size_t slash = rest.find('/');
    const std::string_view comp = slash == std::string_view::npos ? rest : rest.substr(0, slash);
    if (comp.empty()) break;
    if (slash == std::string_view::npos) break; // leaf cgroup: nothing to enable beneath it
    if (!cur.empty()) cur += '/';
    cur += comp;
    const std::string scFile = cur + "/cgroup.subtree_control";
    std::string content;
    if (read_text_file(scFile, content) && content.find("dmem") == std::string::npos) {
      if (write_raw(cur, "cgroup.subtree_control", "+dmem\n")) {
        any = true;
        vram_trace("enabled dmem controller in '" + cur + "'");
      }
    }
    rest.remove_prefix(slash + 1);
  }
  return any;
}

bool VramBoostManager::write_dmem_limit(const std::string& cgroupDir, const char* file, uint64_t bytes) const {
  return write_raw(cgroupDir, file, region_ + " " + std::to_string(bytes) + "\n");
}

bool VramBoostManager::write_raw(const std::string& cgroupDir, const char* file, std::string_view content) const {
  const std::string path = cgroupDir + "/" + file;
  const int fd = ::open(path.c_str(), O_WRONLY);
  if (fd < 0) {
    vram_trace("write " + path + " open failed: " + std::strerror(errno));
    return false;
  }
  const ssize_t written = ::write(fd, content.data(), content.size());
  const int savedErrno = errno;
  ::close(fd);
  if (written < 0) {
    vram_trace("write " + path + " failed: " + std::strerror(savedErrno));
    return false;
  }
  return true;
}

} // namespace eh::service