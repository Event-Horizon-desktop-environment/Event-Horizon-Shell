#include "backends/triad/triad_backends.h"

#include <nlohmann/json.hpp>

static nlohmann::json const* locateTriadBlock(nlohmann::json const& msg) {
  if (!msg.is_object()) return nullptr;
  auto it = msg.find("triad");
  return (it != msg.end() && it->is_object()) ? &*it : nullptr;
}

static nlohmann::json const* drillState(nlohmann::json const& body) {
  auto* triad = locateTriadBlock(body);
  if (!triad) return nullptr;
  auto s = triad->find("state");
  return (s != triad->end() && s->is_object()) ? &*s : nullptr;
}

static std::optional<std::uint64_t> toUnsigned(nlohmann::json const& v) {
  if (v.is_number_unsigned()) return v.get<std::uint64_t>();
  if (v.is_number_integer()) {
    auto signedVal = v.get<std::int64_t>();
    if (signedVal >= 0) return static_cast<std::uint64_t>(signedVal);
  }
  return std::nullopt;
}

static std::optional<std::string> extractOutputFromLayout(nlohmann::json const& layout) {
  auto active = layout.find("active_workspace_idx");
  auto workspaces = layout.find("workspaces");
  if (active == layout.end() || workspaces == layout.end() || !workspaces->is_array()) return std::nullopt;
  auto idx = toUnsigned(*active);
  if (!idx) return std::nullopt;
  for (auto const& ws : *workspaces) {
    if (!ws.is_object()) continue;
    auto i = ws.find("workspace_idx");
    auto o = ws.find("output");
    if (i == ws.end() || o == ws.end() || !o->is_string()) continue;
    auto wsIdx = toUnsigned(*i);
    if (wsIdx && *wsIdx == *idx) return o->get<std::string>();
  }
  return std::nullopt;
}

TriadOutputBackend::TriadOutputBackend(wspace::triad::TriadRuntime& rt) : m_backend(rt) {}

std::optional<std::string> TriadOutputBackend::focusedOutputName() const {
  auto resp = m_backend.sendQuery("state");
  if (!resp) return std::nullopt;
  auto* state = drillState(*resp);
  if (!state) return std::nullopt;

  auto result = extractOutputFromLayout(*state);
  if (result && !result->empty()) return result;

  auto layoutIt = state->find("layout");
  if (layoutIt != state->end() && layoutIt->is_object()) {
    result = extractOutputFromLayout(*layoutIt);
    if (result && !result->empty()) return result;
  }
  return std::nullopt;
}

namespace wspace::triad {

bool setMonitorPower(TriadRuntime& backend, bool enable) {
  return backend.dispatchAction(enable ? "power-on-monitors" : "power-off-monitors");
}

} // namespace wspace::triad
