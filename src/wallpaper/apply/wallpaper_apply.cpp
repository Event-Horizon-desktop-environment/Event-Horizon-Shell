#define _GNU_SOURCE 1
#include "wallpaper/apply/wallpaper_apply.hpp"
#include "wallpaper/core/wallpaper.hpp"

#include "configuration/shell_config.hpp"
#include "dialog/file_chooser_dialog.hpp"
#include "services/process/parent_death_guard.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"
#include "wallpaper/wallpaper_log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <limits.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" char** environ;

namespace eh::wallpaper {

eh::wallpaper::WallpaperRenderer* g_native_renderer = nullptr;

void wallpaper_set_native_renderer(eh::wallpaper::WallpaperRenderer* r) {
  WP_SCOPE();
  g_native_renderer = r;
}

void wallpaper_redraw_native() {
  WP_SCOPE();
  if (g_native_renderer) wallpaper_renderer_redraw(*g_native_renderer);
}

namespace {
[[nodiscard]] std::string ascii_lower_copy(std::string_view in) {
  WP_SCOPE();
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}


}

bool pick_folder_via_sh_cmd(const std::string& cmd, std::string* outPath) {
  WP_SCOPE();
  WP_LOG("cmd=%s", cmd.c_str());
  outPath->clear();
  if (cmd.empty()) return false;
  int fd[2]{};
  if (pipe(fd) < 0) return false;

  std::vector<char> arg_c(cmd.begin(), cmd.end());
  arg_c.push_back('\0');

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    close(fd[0]);
    close(fd[1]);
    return false;
  }
  if (posix_spawn_file_actions_adddup2(&fa, fd[1], STDOUT_FILENO) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return false;
  }
  if (fd[0] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[0]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return false;
  }
  if (fd[1] != STDOUT_FILENO && posix_spawn_file_actions_addclose(&fa, fd[1]) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    close(fd[0]);
    close(fd[1]);
    return false;
  }

  char argv0[] = "/bin/sh";
  char argv1[] = "sh";
  char argv2[] = "-c";
  char* argv[] = {argv0, argv1, argv2, arg_c.data(), nullptr};

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, "/bin/sh", &fa, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  close(fd[1]);
  if (spawn_err != 0 || pid < 0) {
    close(fd[0]);
    return false;
  }
  std::string line;
  std::array<char, 16384> buf{};
  for (;;) {
    pollfd pfd{fd[0], POLLIN, 0};
    const int pr = poll(&pfd, 1, 30000);
    if (pr <= 0) break;
    const ssize_t n = read(fd[0], buf.data(), buf.size());
    if (n <= 0) break;
    line.append(buf.data(), static_cast<std::size_t>(n));
  }
  close(fd[0]);
  int st = 0;
  (void)waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return false;
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  if (line.empty()) return false;
  struct stat sb {};
  if (stat(line.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) return false;
  *outPath = std::move(line);
  WP_LOG("result=%s ok=%d", outPath->c_str(), !outPath->empty());
  return true;
}

static std::string state_base() {
  WP_SCOPE();
  if (const char* d = std::getenv("XDG_STATE_HOME")) return std::string(d);
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/state";
  return "/tmp";
}

static std::string pid_file_path() {
  WP_SCOPE();
  std::string d = state_base() + "/event-horizon";
  (void)mkdir(state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  return d + "/wallpaper.pid";
}

static bool read_pid_file(pid_t* outPid) {
  WP_SCOPE();
  *outPid = -1;
  FILE* f = fopen(pid_file_path().c_str(), "rb");
  if (!f) return false;
  char buf[64]{};
  if (!fgets(buf, sizeof(buf), f)) {
    fclose(f);
    return false;
  }
  fclose(f);
  *outPid = static_cast<pid_t>(std::strtol(buf, nullptr, 10));
  return *outPid > 1;
}

void write_pid_file(pid_t p) {
  WP_SCOPE();
  FILE* f = fopen(pid_file_path().c_str(), "wb");
  if (!f) return;
  fprintf(f, "%d\n", static_cast<int>(p));
  fclose(f);
}

void unlink_pid_file() {
  WP_SCOPE();
  (void)unlink(pid_file_path().c_str());
}

static void wallpaper_log(const std::string& msg) {
  WP_SCOPE();
  WP_LOG("msg=%s", msg.c_str());
  std::string d = state_base() + "/event-horizon";
  (void)mkdir(state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  FILE* f = fopen((d + "/wallpaper.log").c_str(), "ab");
  if (!f) return;
  std::time_t t = std::time(nullptr);
  struct tm tm_buf{};
  char ts[32]{};
  if (strftime(ts, sizeof(ts), "%F %T", localtime_r(&t, &tm_buf))) {
    fprintf(f, "%s %s\n", ts, msg.c_str());
  } else {
    fprintf(f, "%s\n", msg.c_str());
  }
  fclose(f);
}

static void kill_previous_spawned_wallpaper() {
  WP_SCOPE();
  pid_t p = -1;
  if (!read_pid_file(&p) || p <= 1) {
    unlink_pid_file();
    return;
  }
  if (kill(p, SIGTERM) == 0) {
    for (int i = 0; i < 50; ++i) {
      if (kill(p, 0) != 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (kill(p, 0) == 0) (void)kill(p, SIGKILL);
  }
  unlink_pid_file();
}

bool wallpaper_native_child_active() {
  WP_SCOPE();
  pid_t p = -1;
  if (!read_pid_file(&p) || p <= 1) return false;
  if (kill(p, 0) != 0) {
    unlink_pid_file();
    return false;
  }
  char proc[64];
  std::snprintf(proc, sizeof(proc), "/proc/%d/exe", static_cast<int>(p));
  char exe[PATH_MAX];
  const ssize_t n = ::readlink(proc, exe, sizeof(exe) - 1);
  if (n <= 0) return false;
  exe[n] = '\0';
  const std::string_view exePath(exe, static_cast<std::size_t>(n));
  const bool native = exePath.find("horizon-wallpaper") != std::string_view::npos;
  if (!native) unlink_pid_file();
  return native;
}

pid_t wallpaper_spawn_native_child() {
  WP_SCOPE();
  // Reap a stale horizon-wallpaper child from a previous (possibly killed)
  // session before starting a fresh one — both owning BACKGROUND layer
  // surfaces at once would be a problem.
  if (wallpaper_native_child_active()) {
    pid_t stale = -1;
    if (read_pid_file(&stale) && stale > 1) wallpaper_kill_native_child(stale);
  }

  // Resolve the child binary: prefer a `horizon-wallpaper` sitting next to our
  // own executable (works from a build dir / uninstalled tree), else PATH.
  std::string childBin = "horizon-wallpaper";
  {
    char self[4096];
    const ssize_t n = ::readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
      self[n] = '\0';
      const std::string exe = self;
      const auto slash = exe.find_last_of('/');
      const std::string dir = slash == std::string::npos ? "." : exe.substr(0, slash);
      if (::access((dir + "/horizon-wallpaper").c_str(), X_OK) == 0)
        childBin = dir + "/horizon-wallpaper";
    }
  }

  std::string d = state_base() + "/event-horizon";
  (void)mkdir(state_base().c_str(), 0755);
  (void)mkdir(d.c_str(), 0755);
  const std::string logPath = d + "/horizon-wallpaper.log";
  const int logFd = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  if (logFd >= 0) {
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
    (void)posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);
    (void)posix_spawn_file_actions_addclose(&actions, logFd);
  }

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

  char* argv[] = { const_cast<char*>(childBin.c_str()), nullptr };
  eh::proc::mark_child_parent_pid();
  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, childBin.c_str(), &actions, &attr, argv, environ);
  eh::proc::clear_child_parent_pid();

  posix_spawnattr_destroy(&attr);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0 || pid < 0) {
    std::cerr << "[wallpaper] spawn horizon-wallpaper failed: " << strerror(rc) << "\n";
    return -1;
  }
  std::cout << "[wallpaper] spawned horizon-wallpaper pid=" << static_cast<int>(pid)
            << " (log: " << logPath << ")\n";
  return pid;
}

void wallpaper_kill_native_child(int pid) {
  WP_SCOPE();
  if (pid <= 1) {
    unlink_pid_file();
    return;
  }
  if (kill(pid, SIGTERM) == 0) {
    for (int i = 0; i < 50; ++i) {
      if (kill(pid, 0) != 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (kill(pid, 0) == 0) (void)kill(pid, SIGKILL);
  }
  unlink_pid_file();
}

static void substitute_placeholders(std::string& cmd, const std::string& path, const std::string& mode) {
  WP_SCOPE();
  WP_LOG("cmd=%s path=%s mode=%s", cmd.c_str(), path.c_str(), mode.c_str());
  constexpr const char kPath[] = "%p";
  constexpr const char kMode[] = "%m";
  for (;;) {
    const auto ip = cmd.find(kPath);
    if (ip == std::string::npos) break;
    cmd.replace(ip, sizeof(kPath) - 1, path);
  }
  for (;;) {
    const auto im = cmd.find(kMode);
    if (im == std::string::npos) break;
    cmd.replace(im, sizeof(kMode) - 1, mode);
  }
}

const char* swaybg_mode_keyword(int modeIndex) noexcept {
  WP_SCOPE();
  static const std::array<const char*, 5> modes = {"fill", "fit", "stretch", "center", "tile"};
  if (modeIndex < 0 || modeIndex >= static_cast<int>(modes.size())) return modes[0];
  return modes[static_cast<size_t>(modeIndex)];
}

static std::string canonical_image_path(const std::string& imagePath) {
  WP_SCOPE();
  WP_LOG("path=%s", imagePath.c_str());
  char resolved[PATH_MAX];
  if (realpath(imagePath.c_str(), resolved)) {
    WP_LOG("resolved=%s", resolved);
    return std::string(resolved);
  }
  WP_LOG("resolved=%s", imagePath.c_str());
  return imagePath;
}

static std::string file_uri_from_absolute_path(std::string_view abs) {
  WP_SCOPE();
  std::string out = "file://";
  for (unsigned char c : abs) {
    if (c == '/') {
      out += '/';
      continue;
    }
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
      out += static_cast<char>(c);
      continue;
    }
    char buf[8]{};
    std::snprintf(buf, sizeof(buf), "%%%02X", c);
    out += buf;
  }
  return out;
}

static bool ascii_substr_ci(std::string_view hay, std::string_view ned_lower_ascii) noexcept {
  WP_SCOPE();
  if (ned_lower_ascii.empty() || hay.size() < ned_lower_ascii.size()) return false;
  for (size_t i = 0; i <= hay.size() - ned_lower_ascii.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < ned_lower_ascii.size(); ++j) {
      const unsigned char c = static_cast<unsigned char>(hay[i + j]);
      if (std::tolower(c) != static_cast<unsigned char>(ned_lower_ascii[j])) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

static bool env_indicates_gnome_session() noexcept {
  WP_SCOPE();
  constexpr std::string_view kGnome = "gnome";
  if (const char* cur = std::getenv("XDG_CURRENT_DESKTOP")) {
    if (ascii_substr_ci(std::string_view{cur}, kGnome)) return true;
  }
  if (const char* ses = std::getenv("DESKTOP_SESSION")) {
    if (ascii_substr_ci(std::string_view{ses}, kGnome)) return true;
  }
  return false;
}

static int run_execl_sync(const char* prog, std::initializer_list<const char*> arg_tail) {
  WP_SCOPE();
  std::vector<const char*> av;
  av.push_back(prog);
  for (const char* a : arg_tail) av.push_back(a);
  std::vector<char*> argv;
  argv.reserve(av.size() + 1);
  for (const char* p : av) argv.push_back(const_cast<char*>(p));
  argv.push_back(nullptr);
  pid_t pid = -1;
  if (posix_spawnp(&pid, prog, nullptr, nullptr, argv.data(), environ) != 0 || pid < 0) return -1;
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) return -1;
  if (!WIFEXITED(st)) return -1;
  return WEXITSTATUS(st);
}

static bool try_gnome_background_apply(const std::string& absPath, int modeIndex) {
  WP_SCOPE();
  WP_LOG("path=%s mode=%d", absPath.c_str(), modeIndex);
  if (!env_indicates_gnome_session()) return false;

  static const std::array<const char*, 5> gnome_modes = {"zoom", "scaled", "stretched", "centered", "wallpaper"};
  const char* picOpt = gnome_modes[static_cast<size_t>(
      (modeIndex < 0 || modeIndex >= 5) ? 0 : modeIndex)];
  const std::string uri = file_uri_from_absolute_path(absPath);

  if (run_execl_sync("gsettings", {"set", "org.gnome.desktop.background", "picture-uri", uri.c_str()}) != 0) {
    return false;
  }
  (void)run_execl_sync("gsettings",
                        {"set", "org.gnome.desktop.background", "picture-uri-dark", uri.c_str()});
  (void)run_execl_sync("gsettings",
                        {"set", "org.gnome.desktop.background", "picture-options", picOpt});
  return true;
}

static bool wallpaper_daemon_child_still_alive(pid_t wallpaper_pid, const char* attempted) {
  WP_SCOPE();
  WP_LOG("pid=%d attempted=%s", wallpaper_pid, attempted);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int st = 0;
  if (waitpid(wallpaper_pid, &st, WNOHANG) <= 0) return true;
  unlink_pid_file();
  std::string reason = "exit status raw=" + std::to_string(st);
  if (WIFEXITED(st)) {
    const int code = WEXITSTATUS(st);
    reason += " WEXITED code=" + std::to_string(code);
    if (code == 127) {
      std::cerr << "[wallpaper] exec failed (exit 127) for " << attempted
                << " — install the helper or use EH_WALLPAPER_CMD\n";
    } else {
      std::cerr << "[wallpaper] wallpaper process exited immediately code=" << code << " (" << attempted << ")\n";
    }
  }
  if (WIFSIGNALED(st)) {
    reason += " WSIGNALED sig=" + std::to_string(WTERMSIG(st));
  }
  wallpaper_log(std::string("DEATH: ") + reason);
  return false;
}

static std::string detect_folder_picker_cmd() {
  WP_SCOPE();
  const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
  const std::string dlow = desktop ? ascii_lower_copy(desktop) : "";

  if (dlow.find("kde") != std::string::npos || dlow.find("plasma") != std::string::npos) {
    if (access("/usr/bin/kdialog", X_OK) == 0 || access("/bin/kdialog", X_OK) == 0)
      return "kdialog --getexistingdirectory . --title \"Wallpaper folder\"";
  }

  if (access("/usr/bin/kdialog", X_OK) == 0 || access("/bin/kdialog", X_OK) == 0)
    return "kdialog --getexistingdirectory . --title \"Wallpaper folder\"";

  if (access("/usr/bin/qarma", X_OK) == 0 || access("/bin/qarma", X_OK) == 0)
    return "qarma --file-selection --directory --title=\"Wallpaper folder\"";

  return {};
}

bool pick_folder(std::string* outPath, int picker_mode) {
  WP_SCOPE();
  WP_LOG("picker_mode=%d", picker_mode);
  outPath->clear();
  if (const char* custom = std::getenv("EH_WALLPAPER_FOLDER_CMD"); custom && custom[0] != '\0') {
    return pick_folder_via_sh_cmd(std::string(custom), outPath);
  }
  const int m = std::clamp(picker_mode, 0, 3);
  if (m == 0) {
    if (eh::dialog::show_native_folder_picker(outPath)) return true;
    return pick_folder_via_sh_cmd(detect_folder_picker_cmd(), outPath);
  }
  if (m == 1) {
    return pick_folder_via_sh_cmd("kdialog --getexistingdirectory . --title \"Wallpaper folder\"", outPath);
  }
  if (m == 2) {
    return pick_folder_via_sh_cmd("qarma --file-selection --directory --title=\"Wallpaper folder\"", outPath);
  }
  return pick_folder_via_sh_cmd("zenity --file-selection --directory --title=\"Wallpaper folder\"", outPath);
}

void apply_saved(bool enabled, const std::string& imagePath, int modeIndex) {
  WP_SCOPE();
  WP_LOG("enabled=%d path=%s mode=%d", enabled, imagePath.c_str(), modeIndex);
  WP_LOG("native_renderer=%p", (void*)eh::wallpaper::g_native_renderer);
  const bool bench = eh::settings::trace::bench();
  const std::chrono::steady_clock::time_point t_wp0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  auto bench_log = [&](const char* tag, bool ok) {
    if (!bench) return;
    const int64_t us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_wp0).count();
    std::cerr << "[settings-bench] wallpaper_apply tag=" << tag << " ok=" << (ok ? 1 : 0) << " enabled=" << (enabled ? 1 : 0)
              << " mode_idx=" << modeIndex << " path_len=" << imagePath.size() << " total_us=" << us << "\n";
  };

  kill_previous_spawned_wallpaper();

  if (!enabled || imagePath.empty()) {
    std::cout << "[wallpaper] cleared (disabled or empty path)\n";
    eh::config::shell_config_invalidate();
    bench_log("cleared_or_disabled", true);
    return;
  }
  if (access(imagePath.c_str(), R_OK) != 0) {
    std::cerr << "[wallpaper] not readable errno=" << errno << " path=\"" << imagePath << "\"\n";
    bench_log("not_readable", false);
    return;
  }

  const std::string absUsed = canonical_image_path(imagePath);
  const char* kw = swaybg_mode_keyword(modeIndex);

  const char* custom = std::getenv("EH_WALLPAPER_CMD");

  if (g_native_renderer && g_native_renderer->compositor) {
    wallpaper_renderer_apply(*g_native_renderer, enabled, absUsed, modeIndex);
    std::cout << "[wallpaper] native renderer path=\"" << absUsed << "\" mode=" << kw << "\n";
    eh::config::shell_config_invalidate();
    bench_log("native", true);
    return;
  }

  if (custom && custom[0]) {
    std::string cmd(custom);
    substitute_placeholders(cmd, absUsed, kw);
    std::vector<char> arg_lc(cmd.begin(), cmd.end());
    arg_lc.push_back('\0');

    posix_spawnattr_t attr{};
    if (posix_spawnattr_init(&attr) != 0) {
      std::cerr << "[wallpaper] posix_spawnattr_init failed errno=" << errno << "\n";
      bench_log("eh_wallpaper_cmd_fork_fail", false);
      return;
    }
    (void)posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    char argv0[] = "/bin/sh";
    char argv1[] = "sh";
    char argv2[] = "-c";
    char* argv[] = {argv0, argv1, argv2, arg_lc.data(), nullptr};

    pid_t pid = -1;
    const int spawn_err = posix_spawnp(&pid, "/bin/sh", nullptr, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    if (spawn_err != 0 || pid < 0) {
      std::cerr << "[wallpaper] posix_spawn failed errno=" << spawn_err << "\n";
      bench_log("eh_wallpaper_cmd_fork_fail", false);
      return;
    }
    write_pid_file(pid);
    if (!wallpaper_daemon_child_still_alive(pid, "EH_WALLPAPER_CMD")) {
      bench_log("eh_wallpaper_cmd_child_dead", false);
      return;
    }
    std::cout << "[wallpaper] started pid=" << static_cast<int>(pid) << " mode=" << kw << " path=\"" << absUsed
              << "\" (EH_WALLPAPER_CMD)\n";
    eh::config::shell_config_invalidate();
    bench_log("eh_wallpaper_cmd", true);
    return;
  }

  if (try_gnome_background_apply(absUsed, modeIndex)) {
    std::cout << "[wallpaper] applied via gsettings (GNOME background) path=\"" << absUsed << "\"\n";
    eh::config::shell_config_invalidate();
    bench_log("gnome_background", true);
    return;
  }

  std::vector<std::string> sway_argv_store;
  sway_argv_store.emplace_back("swaybg");
  sway_argv_store.emplace_back("-m");
  sway_argv_store.emplace_back(kw ? kw : "");
  sway_argv_store.emplace_back("-i");
  sway_argv_store.emplace_back(absUsed);
  std::vector<char*> sway_argv;
  for (auto& s : sway_argv_store) sway_argv.push_back(s.data());
  sway_argv.push_back(nullptr);

  posix_spawnattr_t sattr{};
  if (posix_spawnattr_init(&sattr) != 0) {
    std::cerr << "[wallpaper] posix_spawnattr_init failed errno=" << errno << "\n";
    bench_log("swaybg_fork_fail", false);
    return;
  }
  (void)posix_spawnattr_setflags(&sattr, POSIX_SPAWN_SETSID);

  pid_t pid = -1;
  const int sway_err = posix_spawnp(&pid, "swaybg", nullptr, &sattr, sway_argv.data(), environ);
  posix_spawnattr_destroy(&sattr);
  if (sway_err != 0 || pid < 0) {
    std::cerr << "[wallpaper] posix_spawn failed errno=" << sway_err << "\n";
    bench_log("swaybg_fork_fail", false);
    return;
  }

  write_pid_file(pid);
  if (!wallpaper_daemon_child_still_alive(pid, "swaybg")) {
    bench_log("swaybg_child_dead", false);
    return;
  }
  std::cout << "[wallpaper] started pid=" << static_cast<int>(pid) << " mode=" << kw << " path=\"" << absUsed
            << "\" (swaybg)\n";
  eh::config::shell_config_invalidate();
  bench_log("swaybg", true);
}

}
