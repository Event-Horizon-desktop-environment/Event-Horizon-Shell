#include "bootstrap/entry/app_bootstrap.hpp"
#include "desktop_shell/common/log/mangowm_logger.hpp"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#ifdef EH_USE_JEMALLOC
#include <jemalloc/jemalloc.h>

// jemalloc tuning: a background thread keeps purging even when idle, one arena,
// and aggressive decay so unused pages go back to the OS quickly.
const char* malloc_conf = "background_thread:true,narenas:1,dirty_decay_ms:0,muzzy_decay_ms:0,tcache_max:2048,prof:false,retain:false";

static int je_stats_fd = -1;

static void je_write_cb(void*, const char* s) {
   
  (void)write(je_stats_fd, s, strlen(s));
}

static void on_sigusr1(int) {
   
  const char hdr[] = "\n=== jemalloc stats ===\n";
  (void)write(je_stats_fd, hdr, sizeof(hdr) - 1);
  malloc_stats_print(je_write_cb, nullptr, "abd");
  // Also dump a heap profile alongside the stats.
  mallctl("prof.dump", nullptr, nullptr, nullptr, 0);
}

#elif defined(EH_USE_GLIBC)
#include <malloc.h>
#endif

static void silence_stdout() {
  int fd = open("/dev/null", O_RDWR);
  if (fd >= 0) {
    dup2(fd, STDOUT_FILENO);
    if (fd > STDOUT_FILENO) close(fd);
  }
}

int main(int argc, char** argv) {
  silence_stdout();
  std::signal(SIGPIPE, SIG_IGN);
    
#if defined(EH_USE_GLIBC) && !defined(EH_USE_JEMALLOC)
  mallopt(M_ARENA_MAX, 2);
#endif

#ifdef EH_USE_JEMALLOC
  je_stats_fd = open("/tmp/eh_jemalloc_stats.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (je_stats_fd == -1)
    je_stats_fd = STDERR_FILENO;
  std::signal(SIGUSR1, on_sigusr1);
#endif

  Application app;
  return app.run(argc, argv);
}
