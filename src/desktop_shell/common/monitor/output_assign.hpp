#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace eh::shell {

inline std::string trim_output_assign(std::string_view s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
  if (start == s.size()) return {};
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end-1]))) end--;
  return std::string(s.substr(start, end - start));
}

inline bool output_assign_is_auto(std::string_view s) {
  std::string t = trim_output_assign(s);
  if (t.empty()) return true;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "auto";
}

inline bool output_assign_is_all_displays(std::string_view s) {
  std::string t = trim_output_assign(s);
  if (t.empty()) return false;
  for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return t == "all";
}

}
