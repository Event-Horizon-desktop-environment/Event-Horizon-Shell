#include "backends/sway/sway_backends.h"

#include <nlohmann/json.hpp>

SwayOutputSource::SwayOutputSource(wspace::sway::SwaySession& runtime) : m_ctx(runtime) {   }

std::optional<std::string> SwayOutputSource::focusedOutputName() const {
   
  if (!m_ctx.canConnect()) {
    return std::nullopt;
  }

  const auto response = m_ctx.sendCommand(wspace::sway::SwaySession::kGetOutputs, {});
  if (!response.has_value() || !response->is_array()) {
    return std::nullopt;
  }

  for (const auto& item : *response) {
    if (!item.is_object()) continue;
    if (item.value("focused", false)) {
      auto name = item.value("name", std::string{});
      if (!name.empty()) {
        // The name can come back padded, so trim it before returning.
        auto begin = name.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return std::nullopt;
        auto end = name.find_last_not_of(" \t\r\n");
        name = name.substr(begin, end - begin + 1);
        if (!name.empty()) return name;
      }
    }
  }

  return std::nullopt;
}

namespace wspace::sway {

bool swaySetOutputPower(SwaySession& runtime, bool on) {
   
  return runtime.sendCommand(SwaySession::kRunCommand,
                         on ? "output * dpms on" : "output * dpms off")
      .has_value();
}

} // namespace wspace::sway
