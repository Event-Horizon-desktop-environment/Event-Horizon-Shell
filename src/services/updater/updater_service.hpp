#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace eh::updater {

inline constexpr const char* kGitHubOwner = "Event-Horizon-desktop-environment";
inline constexpr const char* kGitHubRepo = "Event-Horizon-Shell";
inline constexpr const char* kReleasesLatestUrl =
    "https://api.github.com/repos/Event-Horizon-desktop-environment/Event-Horizon-Shell/releases/latest";

// Parsed version: numeric parts + optional beta number.
// Stable (no beta) > any beta with same numbers.
struct Version {
  std::vector<int> parts;
  int beta = -1;  // -1 = stable release, >=0 = beta-N
  std::string raw;
};

Version parse_version(const std::string& s);
int compare_versions(const Version& a, const Version& b);
inline bool is_newer_than(const Version& remote, const Version& local) {
  return compare_versions(remote, local) > 0;
}
std::string current_version_string();

enum class PkgKind { Unknown, Rpm, Deb, Pacman, Tarball };

PkgKind detect_pkg_kind();
const char* pkg_kind_label(PkgKind k);

struct ReleaseAsset {
  std::string name;
  std::string url;
};

struct CheckResult {
  bool ok = false;
  bool newer = false;
  std::string remoteTag;
  std::string remoteName;
  std::string notes;  // truncated body
  std::string assetName;
  std::string assetUrl;
  std::string error;
};

struct DownloadResult {
  bool ok = false;
  std::string path;
  std::string error;
  long httpCode = 0;
};

struct InstallResult {
  bool ok = false;
  std::string log;
  std::string error;
};

// Pick the best asset for this distro from a release asset list.
std::optional<ReleaseAsset> pick_asset_for_distro(const std::vector<ReleaseAsset>& assets,
                                                  PkgKind kind);

// Build the pkexec install argv for a downloaded package file.
// Returns empty on Unknown kind.
std::vector<std::string> install_argv_for(PkgKind kind, const std::string& pkgPath);

class UpdaterService {
 public:
  static UpdaterService& instance();

  int wake_fd() const noexcept { return wakeFd_; }
  void drain_wake();
  void register_redraw(std::function<void()> fn);

  bool check_async();
  bool download_async(std::string url, std::string assetName);
  bool install_async(std::string pkgPath);

  std::optional<CheckResult> take_check_result();
  std::optional<DownloadResult> take_download_result();
  std::optional<InstallResult> take_install_result();

  bool is_checking() const noexcept;
  bool is_downloading() const noexcept;
  bool is_installing() const noexcept;

  // Prefs (own file, no ShellConfig dependency).
  bool auto_check() const;
  void set_auto_check(bool on);
  std::int64_t last_check_sec() const;
  std::string last_notified_tag() const;
  void set_last_notified_tag(const std::string& tag);
  // True when a periodic check is due (default 6h).
  bool periodic_due(int intervalHours = 6) const;

 private:
  UpdaterService();
  ~UpdaterService();
  UpdaterService(const UpdaterService&) = delete;
  UpdaterService& operator=(const UpdaterService&) = delete;

  void ping_wake();
  std::string prefs_path() const;
  void load_prefs();
  void save_prefs();

  CheckResult do_check();
  DownloadResult do_download(const std::string& url, const std::string& assetName);
  InstallResult do_install(const std::string& pkgPath);

  int wakeFd_ = -1;
  int wakeWriteFd_ = -1;
  mutable std::mutex wakeMu_;
  std::vector<std::function<void()>> redraws_;

  mutable std::mutex stateMu_;
  bool checking_ = false;
  bool downloading_ = false;
  bool installing_ = false;

  std::mutex resultMu_;
  std::optional<CheckResult> checkRes_;
  std::optional<DownloadResult> downloadRes_;
  std::optional<InstallResult> installRes_;

  mutable std::mutex prefsMu_;
  bool autoCheck_ = true;
  std::int64_t lastCheckSec_ = 0;
  std::string lastNotifiedTag_;
};

// Detached shell restart: respawn EventHorizon, then terminate the supervisor.
// Safe to call from the settings child; the respawner is setsid-detached so it
// survives the supervisor's death (children have PDEATHSIG guards).
void request_shell_restart();

// Best-effort locate of the EventHorizon supervisor binary.
std::string find_shell_binary();

}  // namespace eh::updater
