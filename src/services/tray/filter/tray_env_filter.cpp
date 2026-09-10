#include "services/tray/filter/tray_env_filter.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>

namespace eh::shell::dock::tray {
namespace {

void to_lower_inplace(std::string& s) {
   
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::unordered_set<std::string> parse_env_csv_lower(const char* env_name) {
   
  std::unordered_set<std::string> out;
  const char* raw = std::getenv(env_name);
  if (!raw || !*raw) return out;
  std::string acc;
  for (const char* p = raw; *p; ++p) {
    if (*p == ',') {
      if (!acc.empty()) {
        to_lower_inplace(acc);
        out.insert(std::move(acc));
        acc.clear();
      }
    } else if (*p != ' ' && *p != '\t')
      acc.push_back(*p);
  }
  if (!acc.empty()) {
    to_lower_inplace(acc);
    out.insert(std::move(acc));
  }
  return out;
}

bool set_contains_token(const std::unordered_set<std::string>& set, std::string_view token_lower) {
   
  if (set.empty() || token_lower.empty()) return false;
  return set.contains(std::string(token_lower));
}

}

bool dock_tray_item_hidden_by_env(std::string_view id, std::string_view title, std::string_view service,
                                  std::string_view path) {
   
  static const std::unordered_set<std::string> kHide = parse_env_csv_lower("EH_HIDE_TRAYIDS");
  static const std::unordered_set<std::string> kBlock = parse_env_csv_lower("EH_BLACKLIST_TRAYIDS");
  if (kHide.empty() && kBlock.empty()) return false;

  std::string id_l(id);
  std::string title_l(title);
  std::string svc_l(service);
  std::string path_l(path);
  to_lower_inplace(id_l);
  to_lower_inplace(title_l);
  to_lower_inplace(svc_l);
  to_lower_inplace(path_l);
  const std::string buspath = svc_l + path_l;

  auto hit = [&](const std::unordered_set<std::string>& s) -> bool {
    if (set_contains_token(s, id_l)) return true;
    if (set_contains_token(s, title_l)) return true;
    if (set_contains_token(s, svc_l)) return true;
    if (set_contains_token(s, path_l)) return true;
    if (set_contains_token(s, buspath)) return true;
    return false;
  };

  if (hit(kHide)) return true;
  if (hit(kBlock)) return true;
  return false;
}

}
