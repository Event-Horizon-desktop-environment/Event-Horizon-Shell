#include "desktop_shell/stage/standalone/stage_standalone.hpp"

#ifdef EH_USE_JEMALLOC
#include <jemalloc/jemalloc.h>

// Mirror the other children: keep libjemalloc in DT_NEEDED and apply the
// same malloc_conf so the child's allocator behaves identically.
const char* malloc_conf =
    "background_thread:true,narenas:1,dirty_decay_ms:0,muzzy_decay_ms:0,tcache_max:2048,prof:false,retain:false";

static bool eh_stage_je_pin() {
  size_t n = 0;
  mallctl("narenas", &n, nullptr, nullptr, 0);
  return n > 0;
}

static const bool kEhStageJePinned = eh_stage_je_pin();
#endif

int main() { return eh::shell::stage::run_stage_standalone(); }
