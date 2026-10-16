#include "backends/mango/mango_backends.h"
#include "wl/core/connection.hpp"

#include <cstdlib>
#include <string>

MangoOutputSource::MangoOutputSource(wspace::mango::MangoSession& rt) : m_rt(rt) {
  MANGOWM_DEBUG("MangoOutputSource ctor this=%p", (void*)this);
}

std::optional<std::string> MangoOutputSource::focusedOutputName() const {
  return std::nullopt;
}

namespace wspace::mango {

bool mangoSetOutputPower(eh::wayland::WaylandLink& wl, bool on) {
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
