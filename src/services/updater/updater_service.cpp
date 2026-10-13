#include "services/updater/updater_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <curl/curl.h>

#ifndef EH_SHELL_VERSION
#define EH_SHELL_VERSION "0.9.7.1-beta-3"
#endif

namespace eh::updater {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
  size_t b = s.size();
  while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

std::string to_lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

size_t curl_string_write(char* ptr, size_t size, size_t nmemb, void* ud) {
  auto* s = static_cast<std::string*>(ud);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t curl_file_write(char* ptr, size_t size, size_t nmemb, void* ud) {
  auto* f = static_cast<FILE*>(ud);
  return std::fwrite(ptr, 1, size * nmemb, f);
}

int curl_progress_cb(void* ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
  (void)ud;
  (void)dltotal;
  (void)dlnow;
  return 0;
}

void curl_easy_common(CURL* c, const std::string& url, long timeoutSec) {
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "EventHorizon-Updater/1.0");
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
  struct curl_slist* hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, "Accept: application/vnd.github+json");
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
  // NOTE: header list leaks per-request (tiny, one-shot). Freed on easy_cleanup? No —
  // curl does not free it. Acceptable for infrequent update checks; avoids shared state.
  (void)hdrs;
}

// Minimal JSON string-field extractor for the small releases/latest payload.
// Handles "key": "value" with basic escapes. Not a general JSON parser.
std::string json_string_field(const std::string& body, const std::string& key) {
  const std::string pat = "\"" + key + "\"";
  size_t pos = body.find(pat);
  while (pos != std::string::npos) {
    size_t c = pos + pat.size();
    while (c < body.size() && std::isspace((unsigned char)body[c])) ++c;
    if (c < body.size() && body[c] == ':') {
      ++c;
      while (c < body.size() && std::isspace((unsigned char)body[c])) ++c;
      if (c < body.size() && body[c] == '"') {
        ++c;
        std::string out;
        while (c < body.size()) {
          char ch = body[c];
          if (ch == '\\' && c + 1 < body.size()) {
            char e = body[c + 1];
            if (e == 'n') out += '\n';
            else if (e == 'r') out += '\r';
            else if (e == 't') out += '\t';
            else out += e;
            c += 2;
            continue;
          }
          if (ch == '"') break;
          out += ch;
          ++c;
        }
        return out;
      }
    }
    pos = body.find(pat, pos + 1);
  }
  return {};
}

// Collect every {"name":..., "browser_download_url":...} pair in the payload.
std::vector<ReleaseAsset> json_collect_assets(const std::string& body) {
  std::vector<ReleaseAsset> out;
  size_t pos = 0;
  while (true) {
    size_t npos = body.find("\"name\"", pos);
    if (npos == std::string::npos) break;
    size_t c = npos + 6;
    while (c < body.size() && std::isspace((unsigned char)body[c])) ++c;
    if (c >= body.size() || body[c] != ':') {
      pos = npos + 1;
      continue;
    }
    ++c;
    while (c < body.size() && std::isspace((unsigned char)body[c])) ++c;
    if (c >= body.size() || body[c] != '"') {
      pos = npos + 1;
      continue;
    }
    ++c;
    std::string name;
    while (c < body.size() && body[c] != '"') {
      if (body[c] == '\\' && c + 1 < body.size()) {
        name += body[c + 1];
        c += 2;
      } else {
        name += body[c++];
      }
    }
    size_t upos = body.find("\"browser_download_url\"", npos);
    if (upos == std::string::npos || upos > npos + 4096) {
      pos = npos + 1;
      continue;
    }
    size_t d = upos + 22;
    while (d < body.size() && body[d] != '"') ++d;
    if (d >= body.size()) break;
    ++d;
    std::string url;
    while (d < body.size() && body[d] != '"') {
      url += body[d++];
    }
    if (!name.empty() && !url.empty()) out.push_back({name, url});
    pos = d + 1;
    if (out.size() >= 64) break;
  }
  return out;
}

bool ends_with_lo(const std::string& s, const std::string& suf) {
  if (suf.size() > s.size()) return false;
  return to_lower(s.substr(s.size() - suf.size())) == to_lower(suf);
}

std::string state_dir() {
  if (const char* d = std::getenv("XDG_STATE_HOME")) return std::string(d) + "/event-horizon";
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state/event-horizon";
  return "/tmp/event-horizon";
}

std::string cache_dir() {
  if (const char* d = std::getenv("XDG_CACHE_HOME")) return std::string(d) + "/event-horizon";
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.cache/event-horizon";
  return "/tmp/event-horizon-cache";
}

}  // namespace

Version parse_version(const std::string& in) {
  Version v;
  v.raw = in;
  std::string s = trim(in);
  if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
  std::string core = s;
  std::string pre;
  if (auto d = s.find('-'); d != std::string::npos) {
    core = s.substr(0, d);
    pre = s.substr(d + 1);
  }
  std::stringstream ss(core);
  std::string tok;
  while (std::getline(ss, tok, '.')) {
    tok = trim(tok);
    if (tok.empty()) {
      v.parts.push_back(0);
      continue;
    }
    size_t i = 0;
    while (i < tok.size() && std::isdigit((unsigned char)tok[i])) ++i;
    v.parts.push_back(i ? std::atoi(tok.substr(0, i).c_str()) : 0);
  }
  while (v.parts.size() < 3) v.parts.push_back(0);
  if (!pre.empty()) {
    std::string pl = to_lower(pre);
    // Accept "beta-3", "beta3", "beta.3".
    auto b = pl.find("beta");
    if (b != std::string::npos) {
      std::string num;
      for (size_t i = b + 4; i < pl.size(); ++i)
        if (std::isdigit((unsigned char)pl[i])) num += pl[i];
      v.beta = num.empty() ? 0 : std::atoi(num.c_str());
    } else {
      v.beta = 0;  // any other prerelease sorts below stable
    }
  } else {
    v.beta = -1;
  }
  return v;
}

int compare_versions(const Version& a, const Version& b) {
  const size_t n = std::max(a.parts.size(), b.parts.size());
  for (size_t i = 0; i < n; ++i) {
    const int av = i < a.parts.size() ? a.parts[i] : 0;
    const int bv = i < b.parts.size() ? b.parts[i] : 0;
    if (av != bv) return av < bv ? -1 : 1;
  }
  if (a.beta != b.beta) {
    if (a.beta < 0 && b.beta >= 0) return 1;   // stable > beta
    if (a.beta >= 0 && b.beta < 0) return -1;
    return a.beta < b.beta ? -1 : 1;
  }
  return 0;
}

std::string current_version_string() { return std::string(EH_SHELL_VERSION); }

PkgKind detect_pkg_kind() {
  std::ifstream f("/etc/os-release");
  std::string id, like;
  if (f) {
    std::string line;
    while (std::getline(f, line)) {
      if (line.rfind("ID=", 0) == 0) id = to_lower(trim(line.substr(3)));
      if (line.rfind("ID_LIKE=", 0) == 0) like = to_lower(trim(line.substr(8)));
    }
    // Strip quotes: ID="fedora".
    for (auto* s : {&id, &like}) {
      if (s->size() >= 2 && (*s)[0] == '"' && s->back() == '"') *s = s->substr(1, s->size() - 2);
    }
  }
  const std::string hay = id + " " + like;
  auto has = [&](const char* w) { return hay.find(w) != std::string::npos; };
  if (has("arch") || has("cachy") || has("manjaro") || has("endeavour")) return PkgKind::Pacman;
  if (has("fedora") || has("rhel") || has("centos") || has("rocky") || has("alma") || has("opensuse") ||
      has("suse"))
    return PkgKind::Rpm;
  if (has("debian") || has("ubuntu") || has("mint") || has("pop")) return PkgKind::Deb;
  // Fallbacks: probe for the package managers themselves.
  if (::access("/usr/bin/pacman", X_OK) == 0) return PkgKind::Pacman;
  if (::access("/usr/bin/dnf", X_OK) == 0 || ::access("/usr/bin/rpm", X_OK) == 0) return PkgKind::Rpm;
  if (::access("/usr/bin/apt-get", X_OK) == 0 || ::access("/usr/bin/apt", X_OK) == 0) return PkgKind::Deb;
  return PkgKind::Unknown;
}

const char* pkg_kind_label(PkgKind k) {
  switch (k) {
    case PkgKind::Rpm: return "RPM (.rpm)";
    case PkgKind::Deb: return "DEB (.deb)";
    case PkgKind::Pacman: return "Pacman (.pkg.tar.zst)";
    case PkgKind::Tarball: return "Tarball";
    default: return "Unknown";
  }
}

std::optional<ReleaseAsset> pick_asset_for_distro(const std::vector<ReleaseAsset>& assets,
                                                  PkgKind kind) {
  if (assets.empty()) return std::nullopt;
  auto find_by = [&](std::initializer_list<const char*> sufs) -> std::optional<ReleaseAsset> {
    for (const auto& a : assets)
      for (const char* s : sufs)
        if (ends_with_lo(a.name, s)) return a;
    return std::nullopt;
  };
  switch (kind) {
    case PkgKind::Rpm:
      if (auto a = find_by({".rpm"})) return a;
      break;
    case PkgKind::Deb:
      if (auto a = find_by({".deb"})) return a;
      break;
    case PkgKind::Pacman:
      if (auto a = find_by({".pkg.tar.zst", ".pkg.tar.xz"})) return a;
      break;
    default: break;
  }
  // Fallback: any recognizable package, preferring native formats.
  if (auto a = find_by({".rpm", ".deb", ".pkg.tar.zst", ".pkg.tar.xz", ".tar.gz", ".tgz"})) return a;
  return assets.front();
}

std::vector<std::string> install_argv_for(PkgKind kind, const std::string& pkgPath) {
  switch (kind) {
    case PkgKind::Rpm: return {"pkexec", "dnf", "install", "-y", pkgPath};
    case PkgKind::Deb: return {"pkexec", "apt-get", "install", "-y", pkgPath};
    case PkgKind::Pacman: return {"pkexec", "pacman", "-U", "--noconfirm", pkgPath};
    default: return {};
  }
}

// ── UpdaterService ────────────────────────────────────────────────────────────

UpdaterService& UpdaterService::instance() {
  static UpdaterService s;
  return s;
}

UpdaterService::UpdaterService() {
  int fds[2] = {-1, -1};
  if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0) {
    wakeFd_ = fds[0];
    wakeWriteFd_ = fds[1];
  }
  load_prefs();
}

UpdaterService::~UpdaterService() {
  if (wakeFd_ >= 0) ::close(wakeFd_);
  if (wakeWriteFd_ >= 0) ::close(wakeWriteFd_);
}

void UpdaterService::ping_wake() {
  {
    std::lock_guard<std::mutex> lk(wakeMu_);
    for (auto& fn : redraws_) {
      try {
        fn();
      } catch (...) {
      }
    }
  }
  if (wakeWriteFd_ >= 0) {
    char b = 1;
    (void)::write(wakeWriteFd_, &b, 1);
  }
}

void UpdaterService::drain_wake() {
  if (wakeFd_ < 0) return;
  char buf[64];
  while (::read(wakeFd_, buf, sizeof(buf)) > 0) {
  }
}

void UpdaterService::register_redraw(std::function<void()> fn) {
  std::lock_guard<std::mutex> lk(wakeMu_);
  redraws_.push_back(std::move(fn));
}

bool UpdaterService::is_checking() const noexcept {
  std::lock_guard<std::mutex> lk(stateMu_);
  return checking_;
}
bool UpdaterService::is_downloading() const noexcept {
  std::lock_guard<std::mutex> lk(stateMu_);
  return downloading_;
}
bool UpdaterService::is_installing() const noexcept {
  std::lock_guard<std::mutex> lk(stateMu_);
  return installing_;
}

bool UpdaterService::check_async() {
  {
    std::lock_guard<std::mutex> lk(stateMu_);
    if (checking_) return false;
    checking_ = true;
  }
  std::thread([] {
    CheckResult r;
    try {
      r = UpdaterService::instance().do_check();
    } catch (const std::exception& e) {
      r.ok = false;
      r.error = e.what();
    } catch (...) {
      r.ok = false;
      r.error = "unknown error";
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().resultMu_);
      UpdaterService::instance().checkRes_ = r;
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().stateMu_);
      UpdaterService::instance().checking_ = false;
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().prefsMu_);
      UpdaterService::instance().lastCheckSec_ =
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
    }
    UpdaterService::instance().save_prefs();
    UpdaterService::instance().ping_wake();
  }).detach();
  return true;
}

bool UpdaterService::download_async(std::string url, std::string assetName) {
  {
    std::lock_guard<std::mutex> lk(stateMu_);
    if (downloading_) return false;
    downloading_ = true;
  }
  std::thread([url = std::move(url), asset = std::move(assetName)] {
    DownloadResult r;
    try {
      r = UpdaterService::instance().do_download(url, asset);
    } catch (const std::exception& e) {
      r.ok = false;
      r.error = e.what();
    } catch (...) {
      r.ok = false;
      r.error = "unknown error";
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().resultMu_);
      UpdaterService::instance().downloadRes_ = r;
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().stateMu_);
      UpdaterService::instance().downloading_ = false;
    }
    UpdaterService::instance().ping_wake();
  }).detach();
  return true;
}

bool UpdaterService::install_async(std::string pkgPath) {
  {
    std::lock_guard<std::mutex> lk(stateMu_);
    if (installing_) return false;
    installing_ = true;
  }
  std::thread([p = std::move(pkgPath)] {
    InstallResult r;
    try {
      r = UpdaterService::instance().do_install(p);
    } catch (const std::exception& e) {
      r.ok = false;
      r.error = e.what();
    } catch (...) {
      r.ok = false;
      r.error = "unknown error";
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().resultMu_);
      UpdaterService::instance().installRes_ = r;
    }
    {
      std::lock_guard<std::mutex> lk(UpdaterService::instance().stateMu_);
      UpdaterService::instance().installing_ = false;
    }
    UpdaterService::instance().ping_wake();
  }).detach();
  return true;
}

std::optional<CheckResult> UpdaterService::take_check_result() {
  std::lock_guard<std::mutex> lk(resultMu_);
  auto r = checkRes_;
  checkRes_.reset();
  return r;
}
std::optional<DownloadResult> UpdaterService::take_download_result() {
  std::lock_guard<std::mutex> lk(resultMu_);
  auto r = downloadRes_;
  downloadRes_.reset();
  return r;
}
std::optional<InstallResult> UpdaterService::take_install_result() {
  std::lock_guard<std::mutex> lk(resultMu_);
  auto r = installRes_;
  installRes_.reset();
  return r;
}

CheckResult UpdaterService::do_check() {
  CheckResult out;
  CURL* c = curl_easy_init();
  if (!c) {
    out.error = "curl init failed";
    return out;
  }
  std::string body;
  curl_easy_common(c, kReleasesLatestUrl, 15);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_string_write);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);
  CURLcode rc = curl_easy_perform(c);
  long http = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(c);
  if (rc != CURLE_OK) {
    out.error = std::string("network error: ") + curl_easy_strerror(rc);
    return out;
  }
  if (http < 200 || http >= 300) {
    out.error = "GitHub API HTTP " + std::to_string(http);
    if (http == 403 || http == 429) out.error += " (rate-limited, try again later)";
    return out;
  }
  const std::string tag = trim(json_string_field(body, "tag_name"));
  if (tag.empty()) {
    out.error = "no tag_name in release payload";
    return out;
  }
  out.remoteTag = tag;
  out.remoteName = json_string_field(body, "name");
  std::string notes = json_string_field(body, "body");
  if (notes.size() > 600) notes = notes.substr(0, 600) + "…";
  out.notes = notes;
  auto assets = json_collect_assets(body);
  PkgKind kind = detect_pkg_kind();
  if (auto pick = pick_asset_for_distro(assets, kind)) {
    out.assetName = pick->name;
    out.assetUrl = pick->url;
  } else if (!assets.empty()) {
    out.assetName = assets.front().name;
    out.assetUrl = assets.front().url;
  }
  const Version local = parse_version(current_version_string());
  const Version remote = parse_version(tag);
  out.newer = is_newer_than(remote, local);
  out.ok = true;
  if (out.assetUrl.empty()) {
    // Version info is still useful even without a matching package.
    out.error.clear();
  }
  return out;
}

DownloadResult UpdaterService::do_download(const std::string& url, const std::string& assetName) {
  DownloadResult out;
  if (url.empty()) {
    out.error = "empty download URL";
    return out;
  }
  std::error_code ec;
  std::filesystem::create_directories(cache_dir(), ec);
  std::string safe = assetName.empty() ? "event-horizon-update.pkg" : assetName;
  for (char& ch : safe)
    if (ch == '/' || ch == '\\') ch = '_';
  const std::string dest = cache_dir() + "/" + safe + ".download";
  const std::string finalPath = cache_dir() + "/" + safe;
  FILE* f = std::fopen(dest.c_str(), "wb");
  if (!f) {
    out.error = "cannot write " + dest;
    return out;
  }
  CURL* c = curl_easy_init();
  if (!c) {
    std::fclose(f);
    out.error = "curl init failed";
    return out;
  }
  curl_easy_common(c, url, 0);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);  // large packages: no total timeout
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_file_write);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
  CURLcode rc = curl_easy_perform(c);
  long http = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
  curl_easy_cleanup(c);
  std::fclose(f);
  out.httpCode = http;
  if (rc != CURLE_OK) {
    ::unlink(dest.c_str());
    out.error = std::string("download failed: ") + curl_easy_strerror(rc);
    return out;
  }
  if (http < 200 || http >= 300) {
    ::unlink(dest.c_str());
    out.error = "download HTTP " + std::to_string(http);
    return out;
  }
  if (::rename(dest.c_str(), finalPath.c_str()) != 0) {
    out.error = "cannot finalize download";
    return out;
  }
  out.ok = true;
  out.path = finalPath;
  return out;
}

InstallResult UpdaterService::do_install(const std::string& pkgPath) {
  InstallResult out;
  if (pkgPath.empty() || ::access(pkgPath.c_str(), R_OK) != 0) {
    out.error = "package file not readable";
    return out;
  }
  PkgKind kind = detect_pkg_kind();
  auto argv = install_argv_for(kind, pkgPath);
  if (argv.empty()) {
    out.error = "unsupported distro (no rpm/dnf, apt, or pacman found)";
    return out;
  }
  // Log to state dir for the Settings tab to show.
  const std::string logPath = state_dir() + "/updater-install.log";
  std::error_code ec;
  std::filesystem::create_directories(state_dir(), ec);

  pid_t pid = ::fork();
  if (pid < 0) {
    out.error = "fork failed";
    return out;
  }
  if (pid == 0) {
    // Child: redirect output to log, then exec pkexec.
    int fd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
      ::dup2(fd, STDOUT_FILENO);
      ::dup2(fd, STDERR_FILENO);
      if (fd > 2) ::close(fd);
    }
    std::vector<char*> args;
    for (auto& s : argv) args.push_back(const_cast<char*>(s.c_str()));
    args.push_back(nullptr);
    ::execvp(args[0], args.data());
    _exit(127);
  }
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) break;
  }
  std::ifstream lf(logPath);
  std::string log((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
  if (log.size() > 4000) log = log.substr(log.size() - 4000);
  out.log = log;
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    out.ok = true;
  } else {
    out.ok = false;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 126)
      out.error = "authorization dismissed (pkexec)";
    else if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
      out.error = "pkexec not found";
    else
      out.error = "installer exited with status " +
                  std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    if (!log.empty()) out.error += " — see updater-install.log";
  }
  return out;
}

std::string UpdaterService::prefs_path() const { return state_dir() + "/updater.toml"; }

void UpdaterService::load_prefs() {
  std::lock_guard<std::mutex> lk(prefsMu_);
  std::ifstream f(prefs_path());
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    if (k == "auto_check") autoCheck_ = (v == "true" || v == "1");
    if (k == "last_check") {
      try {
        lastCheckSec_ = std::stoll(v);
      } catch (...) {
      }
    }
    if (k == "last_notified") lastNotifiedTag_ = v;
  }
}

void UpdaterService::save_prefs() {
  std::lock_guard<std::mutex> lk(prefsMu_);
  std::error_code ec;
  std::filesystem::create_directories(state_dir(), ec);
  std::ofstream f(prefs_path());
  if (!f) return;
  f << "# Event Horizon updater prefs\n";
  f << "auto_check = " << (autoCheck_ ? "true" : "false") << "\n";
  f << "last_check = " << lastCheckSec_ << "\n";
  f << "last_notified = \"" << lastNotifiedTag_ << "\"\n";
}

bool UpdaterService::auto_check() const {
  std::lock_guard<std::mutex> lk(prefsMu_);
  return autoCheck_;
}

void UpdaterService::set_auto_check(bool on) {
  {
    std::lock_guard<std::mutex> lk(prefsMu_);
    autoCheck_ = on;
  }
  save_prefs();
}

std::int64_t UpdaterService::last_check_sec() const {
  std::lock_guard<std::mutex> lk(prefsMu_);
  return lastCheckSec_;
}

std::string UpdaterService::last_notified_tag() const {
  std::lock_guard<std::mutex> lk(prefsMu_);
  return lastNotifiedTag_;
}

void UpdaterService::set_last_notified_tag(const std::string& tag) {
  {
    std::lock_guard<std::mutex> lk(prefsMu_);
    lastNotifiedTag_ = tag;
  }
  save_prefs();
}

bool UpdaterService::periodic_due(int intervalHours) const {
  std::lock_guard<std::mutex> lk(prefsMu_);
  if (!autoCheck_) return false;
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  if (lastCheckSec_ <= 0) return true;
  return (now - lastCheckSec_) >= (std::int64_t)intervalHours * 3600;
}

std::string find_shell_binary() {
  // Sibling of the current exe (horizon-settings lives next to EventHorizon).
  char self[4096] = {};
  ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n > 0) {
    std::string dir = std::string(self, (size_t)n);
    auto slash = dir.rfind('/');
    if (slash != std::string::npos) {
      std::string sib = dir.substr(0, slash) + "/EventHorizon";
      if (::access(sib.c_str(), X_OK) == 0) return sib;
    }
  }
  for (const char* cand : {"/usr/bin/EventHorizon", "/usr/local/bin/EventHorizon"}) {
    if (::access(cand, X_OK) == 0) return cand;
  }
  return "EventHorizon";  // PATH fallback
}

void request_shell_restart() {
  const std::string bin = find_shell_binary();
  // Double-fork a setsid restarter that outlives the supervisor kill below.
  pid_t p1 = ::fork();
  if (p1 < 0) return;
  if (p1 == 0) {
    if (::setsid() < 0) _exit(1);
    pid_t p2 = ::fork();
    if (p2 < 0) _exit(1);
    if (p2 != 0) _exit(0);
    // Grandchild: detached. Wait for the old shell to exit, then start a new one.
    ::sleep(2);
    // If a user service manages the shell, prefer it (clean cgroup).
    int rc = ::system("systemctl --user restart event-horizon 2>/dev/null");
    (void)rc;
    // Fallback: launch directly if no service took over (best-effort probe).
    ::sleep(1);
    // Only start manually when no EventHorizon is running anymore.
    if (::system("pgrep -x EventHorizon >/dev/null 2>&1") != 0) {
      std::string cmd = std::string("nohup '") + bin + "' >/dev/null 2>&1 &";
      (void)::system(cmd.c_str());
    }
    _exit(0);
  }
  // Parent (settings child): reap first fork, then terminate the supervisor so
  // the PDEATHSIG contract reaps the remaining children (dock/panel/...).
  int st = 0;
  while (::waitpid(p1, &st, 0) < 0 && errno == EINTR) {
  }
  const char* sup = std::getenv("EH_SUPERVISOR_PID");
  long supPid = sup ? std::atol(sup) : 0;
  if (supPid > 1) {
    ::kill((pid_t)supPid, SIGTERM);
    // Escalate if it lingers.
    for (int i = 0; i < 10; ++i) {
      ::usleep(200000);
      if (::kill((pid_t)supPid, 0) != 0) break;
      if (i == 6) ::kill((pid_t)supPid, SIGKILL);
    }
  } else {
    // Standalone settings (no supervisor): just kill any running shell.
    (void)::system("pkill -x EventHorizon 2>/dev/null");
  }
}

}  // namespace eh::updater
