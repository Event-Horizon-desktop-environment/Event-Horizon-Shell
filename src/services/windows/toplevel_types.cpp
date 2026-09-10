#include "services/windows/toplevel_types.hpp"

#include <cstdlib>

namespace eh::windows {

namespace {

// Parse a decimal integer terminated by `term`. Returns false on any malformed
// input (overflow, missing terminator, empty).
bool parse_num(const std::string& data, size_t& pos, char term, std::uint64_t& out) {
  if (pos >= data.size()) return false;
  size_t start = pos;
  std::uint64_t v = 0;
  while (pos < data.size() && data[pos] != term) {
    const char c = data[pos];
    if (c < '0' || c > '9') return false;
    if (v > (static_cast<std::uint64_t>(-1) - static_cast<std::uint64_t>(c - '0')) / 10) {
      return false;  // overflow
    }
    v = v * 10 + static_cast<std::uint64_t>(c - '0');
    ++pos;
  }
  if (pos >= data.size() || pos == start) return false;  // missing terminator / empty
  ++pos;  // consume terminator
  out = v;
  return true;
}

void append_num(std::string& out, std::uint64_t v) {
  if (v == 0) {
    out.push_back('0');
    return;
  }
  char buf[24];
  int n = 0;
  while (v > 0) {
    buf[n++] = static_cast<char>('0' + (v % 10));
    v /= 10;
  }
  while (n > 0) out.push_back(buf[--n]);
}

bool parse_bytes(const std::string& data, size_t& pos, std::uint64_t len,
                 std::string& out) {
  if (pos + len > data.size()) return false;
  out.assign(data.data() + pos, static_cast<size_t>(len));
  pos += static_cast<size_t>(len);
  return true;
}

} // namespace

void toplevel_encode_record(const ToplevelRecord& r, std::string& out) {
  append_num(out, r.id);
  out.push_back(';');
  append_num(out, r.flags);
  out.push_back(';');
  append_num(out, r.appId.size());
  out.push_back(';');
  out.append(r.appId);
  append_num(out, r.title.size());
  out.push_back(';');
  out.append(r.title);
}

bool toplevel_decode_record(const std::string& data, size_t offset,
                            ToplevelRecord& out, size_t& consumed) {
  size_t pos = offset;
  std::uint64_t id = 0, flags = 0, appLen = 0, titleLen = 0;
  if (!parse_num(data, pos, ';', id)) return false;
  if (!parse_num(data, pos, ';', flags)) return false;
  if (!parse_num(data, pos, ';', appLen)) return false;
  out.id = id;
  out.flags = static_cast<std::uint32_t>(flags);
  out.key.clear();
  if (!parse_bytes(data, pos, appLen, out.appId)) return false;
  if (!parse_num(data, pos, ';', titleLen)) return false;
  if (!parse_bytes(data, pos, titleLen, out.title)) return false;
  consumed = pos - offset;
  return true;
}

void toplevel_encode_list(const std::vector<ToplevelRecord>& list, std::string& out) {
  append_num(out, list.size());
  out.push_back('\n');
  for (const auto& r : list) toplevel_encode_record(r, out);
}

bool toplevel_decode_list(const std::string& data, std::vector<ToplevelRecord>& out) {
  size_t pos = 0;
  std::uint64_t count = 0;
  if (!parse_num(data, pos, '\n', count)) return false;
  out.clear();
  out.reserve(static_cast<size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    ToplevelRecord r;
    size_t consumed = 0;
    if (!toplevel_decode_record(data, pos, r, consumed)) return false;
    pos += consumed;
    out.push_back(std::move(r));
  }
  return true;
}

} // namespace eh::windows
