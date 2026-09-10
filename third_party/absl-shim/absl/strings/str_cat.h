// Compile-time stand-in for the tiny slice of Abseil strings used by the
// vendored material-color-utilities (`StrCat(absl::Hex(argb))`). Lowercase
// hex, unpadded, no prefix — matching `absl::Hex` defaults.
#pragma once

#include <cstdint>
#include <string>

namespace absl {

struct Hex {
  uint64_t value;
};

inline std::string StrCat(Hex hex) {
  static constexpr char kDigits[] = "0123456789abcdef";
  uint64_t v = hex.value;
  if (v == 0) {
    return "0";
  }
  int len = 0;
  for (uint64_t t = v; t != 0; t >>= 4) {
    ++len;
  }
  std::string out(static_cast<size_t>(len), '0');
  for (int i = len - 1; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kDigits[v & 0xF];
    v >>= 4;
  }
  return out;
}

}  // namespace absl
