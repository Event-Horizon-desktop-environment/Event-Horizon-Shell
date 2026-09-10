#include "services/vram_boost/vram_boost_detail.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <string>

namespace eh::service::vramboost {

namespace {

std::string_view next_token(std::string_view& line) {
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
    line.remove_prefix(1);
  }
  const size_t start = 0;
  size_t end = start;
  while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
    ++end;
  }
  const std::string_view token = line.substr(start, end - start);
  line.remove_prefix(end);
  return token;
}

bool try_parse_u64(std::string_view tok, uint64_t* out) {
  if (tok.empty()) return false;
  uint64_t value = 0;
  const auto res = std::from_chars(tok.data(), tok.data() + tok.size(), value, 10);
  if (res.ec != std::errc() || res.ptr != tok.data() + tok.size()) return false;
  *out = value;
  return true;
}

void strip_suffix(std::string& s, std::string_view suffix) {
  while (s.size() >= suffix.size() && s.ends_with(suffix)) {
    s.resize(s.size() - suffix.size());
  }
}

} // namespace

std::string normalize_app_id(std::string_view appId) {
  std::string n;
  n.reserve(appId.size());
  for (char ch : appId) {
    n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  const size_t slash = n.find_last_of('/');
  if (slash != std::string::npos) n.erase(0, slash + 1);
  strip_suffix(n, ".desktop");
  strip_suffix(n, ".appimage");
  strip_suffix(n, ".exe");
  return n;
}

bool comm_matches(std::string_view comm, std::string_view normalizedAppId) {
  const std::string normComm = normalize_app_id(comm);
  if (normComm.empty() || normalizedAppId.empty()) return false;
  if (normComm == normalizedAppId) return true;
  // A short comm is a weak signal (e.g. "sh", "Xorg"); require some specificity.
  if (normComm.size() < 3) return false;
  return normalizedAppId.ends_with(normComm);
}

bool controllers_have_dmem(std::string_view controllers) {
  for (;;) {
    const std::string_view tok = next_token(controllers);
    if (tok.empty()) break;
    if (tok == "dmem") return true;
  }
  return false;
}

bool parse_dmem_capacity(std::string_view text, std::vector<DmemRegion>& out) {
  bool any = false;
  std::string_view remaining = text;
  for (;;) {
    const size_t nl = remaining.find('\n');
    const std::string_view line = nl == std::string_view::npos ? remaining : remaining.substr(0, nl);
    if (!line.empty()) {
      std::string_view rest = line;
      const std::string_view name = next_token(rest);
      const std::string_view sizeTok = next_token(rest);
      uint64_t size = 0;
      if (!name.empty() && !sizeTok.empty() && try_parse_u64(sizeTok, &size)) {
        DmemRegion r;
        r.name = std::string(name);
        r.capacity = size;
        out.push_back(std::move(r));
        any = true;
      }
    }
    if (nl == std::string_view::npos) break;
    remaining.remove_prefix(nl + 1);
  }
  return any;
}

bool parse_dmem_current(std::string_view text, std::string_view region, uint64_t* bytes) {
  std::string_view remaining = text;
  for (;;) {
    const size_t nl = remaining.find('\n');
    const std::string_view line = nl == std::string_view::npos ? remaining : remaining.substr(0, nl);
    if (!line.empty()) {
      std::string_view rest = line;
      const std::string_view name = next_token(rest);
      const std::string_view sizeTok = next_token(rest);
      uint64_t size = 0;
      if (name == region && try_parse_u64(sizeTok, &size)) {
        *bytes = size;
        return true;
      }
    }
    if (nl == std::string_view::npos) break;
    remaining.remove_prefix(nl + 1);
  }
  return false;
}

std::vector<DmemRegion> probe_dmem_regions() {
  std::vector<DmemRegion> regions;
  std::ifstream in("/sys/fs/cgroup/dmem.capacity");
  if (!in) return regions;
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  (void)parse_dmem_capacity(text, regions);
  return regions;
}

std::string primary_vram_region(const std::vector<DmemRegion>& regions) {
  std::string fallback;
  for (const auto& r : regions) {
    if (fallback.empty()) fallback = r.name;
    if (r.name.find("vidmem") != std::string::npos) return r.name;
  }
  for (const auto& r : regions) {
    if (r.name.find("vram") != std::string::npos) return r.name;
  }
  return fallback;
}

} // namespace eh::service::vramboost