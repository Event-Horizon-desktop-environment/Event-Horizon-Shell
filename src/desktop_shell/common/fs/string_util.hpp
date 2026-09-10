#pragma once

#include <string>
#include <vector>

namespace eh::shell::str {

[[nodiscard]] std::string trim(std::string s);
[[nodiscard]] bool file_exists(const std::string& path);
[[nodiscard]] std::vector<std::string> split_colon_list(const char* s);

inline void utf8_pop_back(std::string& s) {
  if (s.empty()) return;
  size_t i = s.size();
  while (i > 0) {
    --i;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if ((c & 0xc0u) != 0x80u) {
      s.resize(i);
      return;
    }
  }
  s.clear();
}

}
