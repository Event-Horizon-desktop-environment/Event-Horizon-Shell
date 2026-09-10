#include "backends/hyprland/hyprland_backends.h"

#include <charconv>
#include <format>

namespace wspace::hyprland {

std::string fmtAddress(const std::uint64_t addr) {
  return std::format("{:x}", addr);
}

std::optional<std::uint64_t> parseHexAddress(const std::string_view in) {
  if (in.empty()) {
    return std::nullopt;
  }
  auto trimmed = in;
  if (trimmed.starts_with("0x") || trimmed.starts_with("0X")) {
    trimmed = trimmed.substr(2);
  }
  if (trimmed.empty()) {
    return std::nullopt;
  }
  std::uint64_t result = 0;
  const auto [end, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), result, 16);
  if (ec != std::errc{} || end != trimmed.data() + trimmed.size()) {
    return std::nullopt;
  }
  return result;
}

std::string canonicalWindowId(const std::string_view raw) {
  auto parsed = parseHexAddress(raw);
  if (!parsed.has_value()) {
    return {};
  }
  return fmtAddress(*parsed);
}

bool idMatch(const std::string_view a, const std::string_view b) {
  auto ca = canonicalWindowId(a);
  auto cb = canonicalWindowId(b);
  return !ca.empty() && ca == cb;
}

} // namespace wspace::hyprland
