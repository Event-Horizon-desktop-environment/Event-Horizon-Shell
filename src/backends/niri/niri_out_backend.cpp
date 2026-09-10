#include "backends/niri/niri_backends.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kFocusedReq = "\"FocusedOutput\"\n";

std::optional<std::string> trimName(nlohmann::json const& v) {
  if (v.is_string()) {
    std::string s = v.get<std::string>();
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) { return std::nullopt; }
    auto e = s.find_last_not_of(" \t\r\n");
    s = s.substr(b, e - b + 1);
    return s.empty() ? std::nullopt : std::optional{std::move(s)};
  }
  if (v.is_object()) {
    auto it = v.find("name");
    if (it != v.end()) { return trimName(*it); }
  }
  return std::nullopt;
}

std::optional<std::string> parseFocused(nlohmann::json const& json) {
  if (!json.is_object()) { return std::nullopt; }
  if (json.contains("Err")) { return std::nullopt; }
  auto ok = json.find("Ok");
  if (ok == json.end() || !ok->is_object()) { return std::nullopt; }
  auto f = ok->find("FocusedOutput");
  if (f == ok->end() || f->is_null()) { return std::nullopt; }
  return trimName(*f);
}

} // namespace

NiriOutputBackend::NiriOutputBackend(NiriRuntime& rt) : m_state(rt) {}

std::optional<std::string> NiriOutputBackend::focusedOutputName() const {
  auto resp = m_state.sendQuery(kFocusedReq);
  if (resp.has_value()) { return parseFocused(*resp); }
  return std::nullopt;
}

bool niriSetOutputPower(NiriRuntime& rt, bool on) {
  return rt.dispatchAction(nlohmann::json{
    {on ? "PowerOnMonitors" : "PowerOffMonitors", nlohmann::json::object()},
  });
}
