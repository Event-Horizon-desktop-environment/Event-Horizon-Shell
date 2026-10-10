// Regression test for the horizon-settings singleton pidfile guard.
//
// Bug 1 (original report): split-out children (horizon-dock / taskbar / ...)
// spawned horizon-settings directly, so PDEATHSIG parented Settings to the
// child — turning the dock off killed Settings too. Fixed by delegating
// launches to the supervisor over IPC (settings.open).
//
// Bug 2 (found while diagnosing): settings_singleton_running() only checked
// kill(pid,0) + zombie state. A stale pidfile (left by a SIGKILLed settings)
// whose number was recycled by an unrelated process blocked EVERY launch
// silently — widget clicks appeared dead while Super+S "worked". Fixed with a
// /proc comm/exe identity check (mirrors dock_pid_is_ours).
//
//   singleton_no_pidfile          no pidfile -> not running
//   singleton_foreign_pid_reuse   pidfile with our own (foreign comm) pid ->
//                                 not running + pidfile reclaimed
//   singleton_dead_pid            pidfile with dead pid -> not running +
//                                 pidfile reclaimed
//   singleton_live_settings_comm  pidfile with a live pid whose comm is
//                                 horizon-settings -> running, pidfile kept
//   singleton_garbage             unparsable pidfile -> not running

#include "ux/settings/common/embed/settings_embed_lifecycle.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

static std::string g_dir;

static std::string pid_path() { return g_dir + "/event-horizon-settings.pid"; }

static void write_pidfile(const std::string& content) {
  std::ofstream f(pid_path(), std::ios::trunc);
  f << content;
  f.close();
}

static bool pidfile_exists() { return fs::exists(pid_path()); }

int main() {
  char tmpl[] = "/tmp/eh-settings-singleton-test-XXXXXX";
  if (!::mkdtemp(tmpl)) {
    fprintf(stderr, "FAIL: mkdtemp\n");
    return 1;
  }
  g_dir = tmpl;
  ::setenv("XDG_RUNTIME_DIR", g_dir.c_str(), 1);

  // 1. No pidfile -> not running.
  CHECK(!eh::settings::settings_singleton_running());

  // 2. Garbage content -> not running (reclaimed).
  write_pidfile("not-a-pid\n");
  CHECK(!eh::settings::settings_singleton_running());

  // 3. Dead pid -> not running + pidfile reclaimed.
  // Use a pid that is (almost surely) dead: fork a child that exits at once.
  {
    const pid_t c = ::fork();
    if (c == 0) ::_exit(42);
    int st = 0;
    ::waitpid(c, &st, 0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d\n", (int)c);
    write_pidfile(buf);
    CHECK(!eh::settings::settings_singleton_running());
    CHECK(!pidfile_exists());
  }

  // 4. PID reuse: pidfile holds OUR OWN pid (live, but comm !=
  // horizon-settings / EventHorizon --eh-settings). Must NOT count as
  // running, and the pidfile must be reclaimed so launches aren't blocked.
  {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d\n", (int)::getpid());
    write_pidfile(buf);
    CHECK(!eh::settings::settings_singleton_running());
    CHECK(!pidfile_exists());
  }

  // 5. Live process with comm == horizon-settings counts as running and the
  // pidfile is kept.
  {
    pid_t c = ::fork();
    if (c == 0) {
      // Mask our comm to look like the settings binary, then sleep until
      // killed. prctl(PR_SET_NAME) changes /proc/pid/stat comm.
      (void)::prctl(PR_SET_NAME, "horizon-settings", 0, 0, 0);
      for (;;) ::pause();
      ::_exit(0);
    }
    // Give the child a moment to set its comm.
    for (int i = 0; i < 50; ++i) {
      char p[64];
      std::snprintf(p, sizeof(p), "/proc/%d/stat", (int)c);
      if (FILE* f = std::fopen(p, "r")) {
        char b[256];
        bool named = false;
        if (std::fgets(b, sizeof(b), f) && std::strstr(b, "(horizon-setting")) named = true;
        std::fclose(f);
        if (named) break;
      }
      ::usleep(20 * 1000);
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d\n", (int)c);
    write_pidfile(buf);
    CHECK(eh::settings::settings_singleton_running());
    CHECK(pidfile_exists());
    ::kill(c, SIGKILL);
    int st = 0;
    ::waitpid(c, &st, 0);
    // Reaped SIGKILLed child is a zombie-turned-gone pid: the guard must now
    // report not-running (reuses the zombie + identity handling).
    // Note: the pid is dead but the number may still resolve via /proc
    // briefly; either way running must be false since comm no longer matches
    // a live settings process.
    CHECK(!eh::settings::settings_singleton_running());
  }

  // Cleanup.
  ::unlink(pid_path().c_str());
  ::rmdir(g_dir.c_str());

  if (g_failures == 0) printf("settings_singleton: all tests passed\n");
  return g_failures == 0 ? 0 : 1;
}
