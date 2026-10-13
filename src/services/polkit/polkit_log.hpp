#pragma once

#if defined(EH_HAVE_POLKIT_AGENT)

#include <cstdarg>
#include <cstdio>

#include "desktop_shell/common/log/debug_log.hpp"

// Dedicated polkit trail: ~/EH-logs/polkit.log (one line per event, flushed
// immediately so a hard freeze still leaves the trail behind).
//
// Prefix convention: "svc" = PolkitAuthService, "c" = C listener bridge,
// "dlg" = auth dialog. Cancel-path entries are ENTER/BEFORE/AFTER pairs — if
// an AFTER line is missing, the call between them is where it wedged.
inline void polkit_log(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  debug_log("polkit", "%s", buf);
}

#endif
