#include "backends/mango/mango_backends.h"
#include "wl/core/connection.hpp"

#include <cstdlib>
#include <string>

MangoOutputBackend::MangoOutputBackend(wspace::mango::MangoRuntime& rt) : m_rt(rt) {
  MANGOWM_DEBUG("MangoOutputBackend ctor this=%p", (void*)this);
}

std::optional<std::string> MangoOutputBackend::focusedOutputName() const {
  return std::nullopt;
}

namespace wspace::mango {

bool mangoSetOutputPower(eh::wayland::WaylandConnection& wl, bool on) {
  bool any = false;
  for (auto const& out : wl.logical_outputs()) {
    if (out.name.empty()) { continue; }
    std::string cmd = on
      ? "mmsg -s -d enable_monitor," + out.name + " >/dev/null 2>&1"
      : "mmsg -s -d disable_monitor," + out.name + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) == 0) { any = true; }
  }
  return any;
}

} // namespace wspace::mango
