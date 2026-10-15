#include "services/polkit/standalone/polkit_agent_standalone.hpp"

#ifdef EH_USE_JEMALLOC
#include <jemalloc/jemalloc.h>

// Mirror entry_point.cpp / notifications_standalone_main.cpp: reference jemalloc
// symbols so the dynamic linker keeps libjemalloc in our DT_NEEDED (forcing
// allocator interposition, same as the main EventHorizon binary), and apply the
// same malloc_conf so the child's allocator behaves identically to the
// in-process polkit path it replaces. MANDATORY per the desktop_list.cpp
// static-init heap bug (every child links core).
const char* malloc_conf = "background_thread:true,narenas:1,dirty_decay_ms:0,muzzy_decay_ms:0,tcache_max:2048,prof:false,retain:false";

static bool eh_polkit_agent_je_pin() {
  size_t n = 0;
  mallctl("narenas", &n, nullptr, nullptr, 0);
  return n > 0;
}

static const bool kEhPolkitAgentJePinned = eh_polkit_agent_je_pin();
#endif

int main() {
  return eh::polkit::run_polkit_agent_standalone();
}
