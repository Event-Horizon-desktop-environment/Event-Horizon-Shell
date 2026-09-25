// Multi-user backend for the Settings Accounts tab.
// See accounts_users.hpp. No business logic here touches the shell: every
// privileged call is fork+exec of pkexec with a literal argv.

#include "ux/settings/settings_tab_accounts/accounts_users.hpp"

// Declarations only: the implementation is linked from
// src/wallpaper/thumbnail/wallpaper_thumbnail.cpp (same binary).
#include "stb/stb_image.h"

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::string current_username() {
  struct passwd* pw = getpwuid(getuid());
  return (pw && pw->pw_name) ? pw->pw_name : "";
}

std::string full_from_gecos(const struct passwd* pw) {
  std::string raw = (pw && pw->pw_gecos) ? pw->pw_gecos : "";
  const auto pos = raw.find(',');
  std::string name = (pos != std::string::npos) ? raw.substr(0, pos) : raw;
  if (name.empty() && pw && pw->pw_name) name = pw->pw_name;
  return name;
}

std::string detect_admin_group() {
  if (getgrnam("wheel")) return "wheel";
  if (getgrnam("sudo")) return "sudo";
  return "";
}

bool user_is_admin(const char* username, gid_t primary, const std::string& adminGroup) {
  if (adminGroup.empty() || !username || !*username) return false;
  struct group* gr = getgrnam(adminGroup.c_str());
  if (!gr) return false;
  if (primary == gr->gr_gid) return true;
  int ngroups = 0;
  getgrouplist(username, primary, nullptr, &ngroups);
  if (ngroups <= 0 || ngroups > 256) return false;
  std::vector<gid_t> groups(static_cast<size_t>(ngroups));
  if (getgrouplist(username, primary, groups.data(), &ngroups) < 0) return false;
  return std::find(groups.begin(), groups.end(), gr->gr_gid) != groups.end();
}

std::vector<AccountsUserEntry> enumerate_users() {
  const std::string cur = current_username();
  const std::string adminGroup = detect_admin_group();
  std::vector<AccountsUserEntry> out;
  setpwent();
  for (struct passwd* pw = getpwent(); pw; pw = getpwent()) {
    if (!pw->pw_name || !pw->pw_name[0]) continue;
    const unsigned uid = static_cast<unsigned>(pw->pw_uid);
    const bool isCur = (pw->pw_name == cur);
    if (!isCur && (uid < 1000 || uid == 65534)) continue; // system users
    AccountsUserEntry e;
    e.username = pw->pw_name;
    e.fullName = full_from_gecos(pw);
    e.home = pw->pw_dir ? pw->pw_dir : "";
    e.uid = uid;
    e.isCurrent = isCur;
    e.isAdmin = user_is_admin(pw->pw_name, pw->pw_gid, adminGroup);
    out.push_back(std::move(e));
  }
  endpwent();
  std::sort(out.begin(), out.end(), [](const AccountsUserEntry& a, const AccountsUserEntry& b) {
    if (a.isCurrent != b.isCurrent) return a.isCurrent > b.isCurrent;
    return a.username < b.username;
  });
  return out;
}

// ---- privileged exec (no shell) ----

bool write_all(int fd, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    off += static_cast<size_t>(n);
  }
  return true;
}

} // namespace

PrivResult accounts_run_priv(const std::vector<std::string>& pkexec_argv,
                             const std::string& stdin_data) {
  PrivResult r;
  int inPipe[2] = {-1, -1}, outPipe[2] = {-1, -1};
  if (pipe(inPipe) != 0 || pipe(outPipe) != 0) {
    if (inPipe[0] >= 0) { close(inPipe[0]); close(inPipe[1]); }
    if (outPipe[0] >= 0) { close(outPipe[0]); close(outPipe[1]); }
    return r;
  }
  // FD_CLOEXEC so stray fds never leak into pkexec.
  for (int fd : {inPipe[0], inPipe[1], outPipe[0], outPipe[1]}) {
    int fl = fcntl(fd, F_GETFD);
    if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]);
    return r;
  }
  if (pid == 0) {
    dup2(inPipe[0], STDIN_FILENO);
    dup2(outPipe[1], STDOUT_FILENO);
    dup2(outPipe[1], STDERR_FILENO);
    close(inPipe[0]); close(inPipe[1]); close(outPipe[0]); close(outPipe[1]);
    std::vector<char*> argv;
    argv.reserve(pkexec_argv.size() + 1);
    for (const auto& a : pkexec_argv) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp("pkexec", argv.data());
    _exit(127);
  }
  close(inPipe[0]);
  close(outPipe[1]);
  bool ok = true;
  if (!stdin_data.empty()) ok = write_all(inPipe[0], stdin_data);
  close(inPipe[0]);
  std::string out;
  std::array<char, 4096> buf{};
  for (;;) {
    ssize_t n = read(outPipe[0], buf.data(), buf.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    out.append(buf.data(), static_cast<size_t>(n));
    if (out.size() > 65536) break;
  }
  close(outPipe[0]);
  int status = -1;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) break;
  }
  r.output = std::move(out);
  if (!ok) return r;
  if (WIFEXITED(status)) r.status = WEXITSTATUS(status);
  return r;
}

// ---- cached enumeration ----

namespace {
std::vector<AccountsUserEntry> g_users;
int g_users_tick = 0;
constexpr int kUsersRefreshEvery = 240;
AutologinInfo g_autologin;
bool g_autologin_init = false;

void merge_lock_states(std::vector<AccountsUserEntry>& fresh) {
  for (auto& e : fresh) {
    for (const auto& old : g_users) {
      if (old.username == e.username && old.lockKnown) {
        e.locked = old.locked;
        e.lockKnown = true;
        break;
      }
    }
  }
}
} // namespace

void accounts_refresh_users() {
  auto fresh = enumerate_users();
  merge_lock_states(fresh);
  g_users = std::move(fresh);
  accounts_invalidate_avatars();
  g_users_tick = 0;
}

void accounts_poll_refresh() {
  if (g_users.empty() || ++g_users_tick >= kUsersRefreshEvery) {
    accounts_refresh_users();
  }
  if (!g_autologin_init || g_users_tick == 0) {
    g_autologin = accounts_autologin_info();
    g_autologin_init = true;
  }
}

const std::vector<AccountsUserEntry>& accounts_users() { return g_users; }

const AccountsUserEntry* accounts_find(const std::string& username) {
  for (const auto& e : g_users) {
    if (e.username == username) return &e;
  }
  return nullptr;
}

std::string accounts_current_username() { return current_username(); }

int accounts_admin_count() {
  int n = 0;
  for (const auto& e : g_users) {
    if (e.isAdmin) ++n;
  }
  return n;
}

std::string accounts_admin_group() { return detect_admin_group(); }

// ---- validation ----

bool accounts_valid_username(const std::string& u) {
  if (u.empty() || u.size() > 32) return false;
  if (!(u[0] == '_' || (u[0] >= 'a' && u[0] <= 'z'))) return false;
  for (char c : u) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  if (u == "." || u == "..") return false;
  return true;
}

bool accounts_valid_hostname(const std::string& h) {
  if (h.empty() || h.size() > 64) return false;
  if (!(std::isalnum(static_cast<unsigned char>(h.front())) &&
        std::isalnum(static_cast<unsigned char>(h.back()))))
    return false;
  for (char c : h) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
  }
  return true;
}

bool accounts_valid_fullname(const std::string& f) {
  if (f.empty() || f.size() > 128) return false;
  for (char c : f) {
    if (c == ':' || c == '\n' || c == '\r') return false;
  }
  return true;
}

// ---- lock state ----

bool accounts_query_locked(const std::string& username, bool& lockedOut) {
  if (username.empty()) return false;
  std::string out;
  if (username == current_username()) {
    // Unprivileged for self.
    FILE* fp = popen("passwd -S 2>/dev/null", "r");
    if (!fp) return false;
    std::array<char, 256> b{};
    if (fgets(b.data(), static_cast<int>(b.size()), fp)) out = b.data();
    pclose(fp);
  } else {
    if (!accounts_valid_username(username)) return false;
    PrivResult r = accounts_run_priv({"pkexec", "passwd", "-S", username});
    if (r.status != 0) return false;
    out = r.output;
  }
  // Format: "name STATUS ..." where STATUS is L (locked), P (usable), NP (none).
  std::string::size_type sp = out.find(' ');
  if (sp == std::string::npos || out.compare(0, sp, username) != 0) return false;
  const std::string rest = out.substr(sp + 1);
  const std::string::size_type sp2 = rest.find_first_of(" \t\r\n");
  const std::string status = rest.substr(0, sp2);
  if (status != "L" && status != "P" && status != "NP") return false;
  lockedOut = (status == "L");
  for (auto& e : g_users) {
    if (e.username == username) {
      e.locked = lockedOut;
      e.lockKnown = true;
      break;
    }
  }
  return true;
}

// ---- actions ----

AccountsActionResult accounts_set_password(const std::string& username, const std::string& pass) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  if (pass.size() < 4) return {false, "Password too short (min 4 chars)."};
  if (pass.find('\n') != std::string::npos || pass.find('\r') != std::string::npos)
    return {false, "Password cannot contain newlines."};
  // chpasswd splits user/password at the first ':'; the validated username
  // cannot contain one.
  PrivResult r = accounts_run_priv({"pkexec", "chpasswd"}, username + ":" + pass + "\n");
  if (r.status == 0) return {true, "Password changed for '" + username + "'."};
  return {false, "Failed to change password."};
}

AccountsActionResult accounts_create_user(const std::string& username, const std::string& fullName,
                                          const std::string& pass, bool admin) {
  if (!accounts_valid_username(username)) {
    return {false, "Invalid username (a-z, 0-9, _, -, max 32)."};
  }
  if (!accounts_valid_fullname(fullName)) return {false, "Invalid full name."};
  if (pass.size() < 4) return {false, "Password too short (min 4 chars)."};
  if (getpwnam(username.c_str())) return {false, "User '" + username + "' already exists."};
  std::vector<std::string> argv = {"pkexec", "useradd", "-m", "-s", "/bin/bash",
                                   "-c", fullName};
  if (admin) {
    const std::string grp = detect_admin_group();
    if (grp.empty()) return {false, "No administrator group (wheel/sudo) found."};
    argv.push_back("-G");
    argv.push_back(grp);
  }
  argv.push_back(username);
  PrivResult r = accounts_run_priv(argv);
  if (r.status != 0) return {false, "Failed to create user (already exists?)."};
  AccountsActionResult pw = accounts_set_password(username, pass);
  accounts_refresh_users();
  if (!pw.ok) return {false, "User created but password failed."};
  std::string msg = "User '" + username + "' created.";
  if (admin) msg += " (administrator)";
  return {true, msg};
}

AccountsActionResult accounts_delete_user(const std::string& username) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  if (username == current_username()) return {false, "You cannot delete your own account."};
  const AccountsUserEntry* e = accounts_find(username);
  if (e && e->isAdmin && accounts_admin_count() <= 1)
    return {false, "Cannot remove the last administrator."};
  PrivResult r = accounts_run_priv({"pkexec", "userdel", "-r", username});
  accounts_refresh_users();
  if (r.status == 0) return {true, "User '" + username + "' deleted."};
  return {false, "Failed to delete user."};
}

AccountsActionResult accounts_set_admin(const std::string& username, bool admin) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  const std::string grp = detect_admin_group();
  if (grp.empty()) return {false, "No administrator group (wheel/sudo) found."};
  if (!admin) {
    const AccountsUserEntry* e = accounts_find(username);
    if (e && e->isAdmin && accounts_admin_count() <= 1)
      return {false, "Cannot remove the last administrator."};
  }
  PrivResult r = admin ? accounts_run_priv({"pkexec", "usermod", "-aG", grp, username})
                       : accounts_run_priv({"pkexec", "gpasswd", "-d", username, grp});
  accounts_refresh_users();
  if (r.status == 0) {
    return {true, admin ? "'" + username + "' is now an administrator."
                        : "'" + username + "' is now a standard user."};
  }
  return {false, "Failed to change administrator rights."};
}

AccountsActionResult accounts_set_locked(const std::string& username, bool locked) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  if (locked && username == current_username())
    return {false, "You cannot lock your own account."};
  PrivResult r = locked ? accounts_run_priv({"pkexec", "passwd", "-l", username})
                        : accounts_run_priv({"pkexec", "passwd", "-u", username});
  bool st = false;
  if (r.status == 0 && accounts_query_locked(username, st)) {
    return {true, st ? "'" + username + "' is locked." : "'" + username + "' is unlocked."};
  }
  if (r.status == 0) {
    accounts_refresh_users();
    return {true, locked ? "Lock requested." : "Unlock requested."};
  }
  return {false, "Failed to change account lock."};
}

AccountsActionResult accounts_set_fullname(const std::string& username, const std::string& fullName) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  if (!accounts_valid_fullname(fullName)) return {false, "Invalid full name."};
  PrivResult r = accounts_run_priv({"pkexec", "usermod", "-c", fullName, username});
  accounts_refresh_users();
  if (r.status == 0) return {true, "Full name updated."};
  return {false, "Failed to update full name."};
}

AccountsActionResult accounts_set_hostname(const std::string& hostname) {
  if (!accounts_valid_hostname(hostname)) return {false, "Invalid hostname."};
  PrivResult r = accounts_run_priv({"pkexec", "hostnamectl", "set-hostname", hostname});
  if (r.status == 0) return {true, "Hostname changed."};
  return {false, "Failed to set hostname."};
}

// ---- avatars ----

namespace {
std::map<std::string, cairo_surface_t*> g_avatar_cache;

cairo_surface_t* surface_from_rgba(const unsigned char* px, int w, int h) {
  cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return surf;
  unsigned char* dst = cairo_image_surface_get_data(surf);
  if (!dst) return surf;
  const int stride = cairo_image_surface_get_stride(surf);
  for (int y = 0; y < h; ++y) {
    auto* row = reinterpret_cast<uint32_t*>(dst + static_cast<ptrdiff_t>(y) * stride);
    const unsigned char* src = px + static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
    for (int x = 0; x < w; ++x) {
      const unsigned rv = src[0], gv = src[1], bv = src[2], av = src[3];
      row[x] = (av << 24) | (((rv * av + 127) / 255) << 16) |
               (((gv * av + 127) / 255) << 8) | ((bv * av + 127) / 255);
      src += 4;
    }
  }
  cairo_surface_mark_dirty(surf);
  return surf;
}

cairo_surface_t* decode_avatar_file(const std::string& path) {
  if (path.empty()) return nullptr;
  cairo_surface_t* img = cairo_image_surface_create_from_png(path.c_str());
  if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
    if (cairo_image_surface_get_width(img) > 0 && cairo_image_surface_get_height(img) > 0) return img;
  }
  cairo_surface_destroy(img);
  int w = 0, h = 0;
  unsigned char* px = stbi_load(path.c_str(), &w, &h, nullptr, 4);
  if (!px || w <= 0 || h <= 0 ||
      static_cast<int64_t>(w) * static_cast<int64_t>(h) > 32LL * 1024 * 1024) {
    if (px) stbi_image_free(px);
    return nullptr;
  }
  cairo_surface_t* surf = surface_from_rgba(px, w, h);
  stbi_image_free(px);
  if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surf);
    return nullptr;
  }
  return surf;
}

bool readable_file(const std::string& path) {
  if (path.empty()) return false;
  struct stat st{};
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
  return access(path.c_str(), R_OK) == 0;
}
} // namespace

cairo_surface_t* accounts_avatar_for(const std::string& username, const std::string& home,
                                     const std::string& explicitPath) {
  if (username.empty()) return nullptr;
  auto it = g_avatar_cache.find(username);
  if (it != g_avatar_cache.end()) return it->second; // may be null = known miss
  cairo_surface_t* surf = nullptr;
  if (!explicitPath.empty() && readable_file(explicitPath)) surf = decode_avatar_file(explicitPath);
  if (!surf && !home.empty()) {
    const std::string face = home + "/.face";
    if (readable_file(face)) surf = decode_avatar_file(face);
  }
  if (!surf) {
    const std::string asIcon = "/var/lib/AccountsService/icons/" + username;
    if (readable_file(asIcon)) surf = decode_avatar_file(asIcon);
  }
  g_avatar_cache[username] = surf;
  return surf;
}

void accounts_invalidate_avatars() {
  for (auto& kv : g_avatar_cache) {
    if (kv.second) cairo_surface_destroy(kv.second);
  }
  g_avatar_cache.clear();
}

AccountsActionResult accounts_install_avatar(const std::string& username, const std::string& home,
                                             const std::string& srcPath, bool isCurrent) {
  if (!accounts_valid_username(username)) return {false, "Invalid username."};
  if (home.empty() || home[0] != '/') return {false, "Unknown home directory."};
  if (!readable_file(srcPath)) return {false, "Cannot read the chosen image."};
  // Validate it decodes before installing.
  cairo_surface_t* probe = decode_avatar_file(srcPath);
  if (!probe) return {false, "Unsupported image file."};
  cairo_surface_destroy(probe);
  const std::string dst = home + "/.face";
  if (isCurrent) {
    // Direct copy for our own home (no privilege needed).
    FILE* in = fopen(srcPath.c_str(), "rb");
    FILE* out = fopen(dst.c_str(), "wb");
    if (!in || !out) {
      if (in) fclose(in);
      if (out) fclose(out);
      return {false, "Failed to save avatar."};
    }
    std::array<char, 65536> chunk{};
    size_t n = 0;
    bool ok = true;
    while ((n = fread(chunk.data(), 1, chunk.size(), in)) > 0) {
      if (fwrite(chunk.data(), 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) return {false, "Failed to save avatar."};
  } else {
    // install(1) sets owner/mode atomically via pkexec; argv is literal.
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) return {false, "Unknown user."};
    PrivResult r = accounts_run_priv(
        {"pkexec", "install", "-o", username, "-g", std::to_string(static_cast<unsigned>(pw->pw_gid)),
         "-m", "0644", srcPath, dst});
    if (r.status != 0) return {false, "Failed to install avatar."};
  }
  accounts_invalidate_avatars();
  return {true, "Avatar updated for '" + username + "'."};
}

// ---- automatic login ----

namespace {
bool file_exists(const std::string& p) {
  struct stat st{};
  return stat(p.c_str(), &st) == 0;
}
bool any_exists(std::initializer_list<const char*> ps) {
  for (const char* p : ps) {
    if (file_exists(p)) return true;
  }
  return false;
}
std::string read_file(const std::string& p) {
  FILE* f = fopen(p.c_str(), "r");
  if (!f) return {};
  std::string out;
  std::array<char, 8192> b{};
  size_t n = 0;
  while ((n = fread(b.data(), 1, b.size(), f)) > 0) {
    out.append(b.data(), n);
    if (out.size() > (1u << 20)) break;
  }
  fclose(f);
  return out;
}
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> lines;
  std::string::size_type start = 0;
  for (;;) {
    const auto pos = s.find('\n', start);
    lines.push_back(s.substr(start, pos == std::string::npos ? pos : pos - start));
    if (pos == std::string::npos) break;
    start = pos + 1;
  }
  return lines;
}
std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}
// Get key from [section], searching lines in order (later wins).
std::string ini_get(const std::vector<std::string>& lines, const std::string& section,
                    const std::string& key) {
  std::string cur;
  std::string val;
  for (const auto& raw : lines) {
    const std::string t = trim(raw);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
      cur = t.substr(1, t.size() - 2);
      continue;
    }
    if (cur != section || t.empty() || t[0] == '#' || t[0] == ';') continue;
    const auto eq = t.find('=');
    if (eq == std::string::npos) continue;
    if (trim(t.substr(0, eq)) == key) val = trim(t.substr(eq + 1));
  }
  return val;
}
// Set (or remove when value==nullopt... use erase flag) key in [section],
// preserving everything else. Appends the section when missing.
std::string ini_set(const std::string& content, const std::string& section, const std::string& key,
                    const std::string& value, bool erase) {
  auto lines = split_lines(content);
  bool inSection = false, sectionSeen = false, done = false;
  std::vector<std::string> out;
  out.reserve(lines.size() + 2);
  for (const auto& raw : lines) {
    const std::string t = trim(raw);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
      if (inSection && !done && !erase) {
        out.push_back(key + "=" + value);
        done = true;
      }
      inSection = (t.substr(1, t.size() - 2) == section);
      if (inSection) sectionSeen = true;
      out.push_back(raw);
      continue;
    }
    if (inSection && !t.empty() && t[0] != '#' && t[0] != ';') {
      const auto eq = t.find('=');
      if (eq != std::string::npos && trim(t.substr(0, eq)) == key) {
        if (!erase) {
          out.push_back(key + "=" + value);
          done = true;
        }
        continue; // drop old (or erased) line
      }
    }
    out.push_back(raw);
  }
  if (!done && !erase) {
    if (!sectionSeen) {
      if (!out.empty() && !trim(out.back()).empty()) out.emplace_back("");
      out.push_back("[" + section + "]");
    }
    out.push_back(key + "=" + value);
  }
  std::string res;
  for (size_t i = 0; i < out.size(); ++i) {
    res += out[i];
    if (i + 1 < out.size()) res += "\n";
  }
  if (!res.empty() && res.back() != '\n') res += "\n";
  return res;
}
bool str_on(const std::string& v) {
  std::string l = v;
  std::transform(l.begin(), l.end(), l.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return l == "true" || l == "yes" || l == "1";
}

constexpr const char* kGdmConf = "/etc/gdm/custom.conf";
constexpr const char* kSddmConf = "/etc/sddm.conf";
constexpr const char* kSddmConfD = "/etc/sddm.conf.d";
constexpr const char* kSddmDropIn = "/etc/sddm.conf.d/10-event-horizon-autologin.conf";
constexpr const char* kLightdmConf = "/etc/lightdm/lightdm.conf";

bool gdm_present() { return any_exists({"/usr/bin/gdm", "/usr/sbin/gdm", kGdmConf}); }
bool sddm_present() { return any_exists({"/usr/bin/sddm", kSddmConf, kSddmConfD}); }
bool lightdm_present() {
  return any_exists({"/usr/bin/lightdm", "/usr/sbin/lightdm", kLightdmConf});
}

std::string sddm_merged(const char* key) {
  std::string val = ini_get(split_lines(read_file(kSddmConf)), "Autologin", key);
  DIR* d = opendir(kSddmConfD);
  if (d) {
    std::vector<std::string> names;
    for (struct dirent* e = readdir(d); e; e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 5 && n.compare(n.size() - 5, 5, ".conf") == 0) names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
      const std::string v = ini_get(split_lines(read_file(std::string(kSddmConfD) + "/" + n)),
                                    "Autologin", key);
      if (!v.empty()) val = v;
    }
  }
  return val;
}
} // namespace

AutologinInfo accounts_autologin_info() {
  if (gdm_present()) {
    const auto lines = split_lines(read_file(kGdmConf));
    const bool on = str_on(ini_get(lines, "daemon", "AutomaticLoginEnable"));
    const std::string user = ini_get(lines, "daemon", "AutomaticLogin");
    return {true, "GDM", on && !user.empty(), user};
  }
  if (sddm_present()) {
    const std::string user = sddm_merged("User");
    return {true, "SDDM", !user.empty(), user};
  }
  if (lightdm_present()) {
    const auto lines = split_lines(read_file(kLightdmConf));
    const std::string user = ini_get(lines, "Seat:*", "autologin-user");
    return {true, "LightDM", !user.empty(), user};
  }
  return {};
}

AccountsActionResult accounts_set_autologin(const std::string& username, bool enable) {
  if (enable && !accounts_valid_username(username)) return {false, "Invalid username."};
  if (gdm_present()) {
    std::string content = read_file(kGdmConf);
    if (content.empty()) content = "# GDM configuration\n";
    content = ini_set(content, "daemon", "AutomaticLoginEnable", enable ? "True" : "False", false);
    content = ini_set(content, "daemon", "AutomaticLogin", username, !enable);
    PrivResult r = accounts_run_priv({"pkexec", "tee", kGdmConf}, content);
    if (r.status != 0) return {false, "Failed to update GDM autologin."};
    g_autologin = accounts_autologin_info();
    return {true, enable ? "Automatic login enabled for '" + username + "' (GDM)."
                         : "Automatic login disabled (GDM)."};
  }
  if (sddm_present()) {
    if (enable) {
      std::string content = "[Autologin]\nUser=" + username + "\n";
      const std::string session = sddm_merged("Session");
      if (!session.empty()) content += "Session=" + session + "\n";
      PrivResult r = accounts_run_priv({"pkexec", "tee", kSddmDropIn}, content);
      if (r.status != 0) return {false, "Failed to update SDDM autologin."};
    } else {
      PrivResult r = accounts_run_priv({"pkexec", "rm", "-f", kSddmDropIn});
      if (r.status != 0) return {false, "Failed to disable SDDM autologin."};
    }
    g_autologin = accounts_autologin_info();
    return {true, enable ? "Automatic login enabled for '" + username + "' (SDDM)."
                         : "Automatic login disabled (SDDM)."};
  }
  if (lightdm_present()) {
    std::string content = read_file(kLightdmConf);
    if (content.empty()) content = "# LightDM configuration\n";
    content = ini_set(content, "Seat:*", "autologin-user", username, !enable);
    PrivResult r = accounts_run_priv({"pkexec", "tee", kLightdmConf}, content);
    if (r.status != 0) return {false, "Failed to update LightDM autologin."};
    g_autologin = accounts_autologin_info();
    return {true, enable ? "Automatic login enabled for '" + username + "' (LightDM)."
                         : "Automatic login disabled (LightDM)."};
  }
  return {false, "No supported login manager (GDM/SDDM/LightDM) detected."};
}
