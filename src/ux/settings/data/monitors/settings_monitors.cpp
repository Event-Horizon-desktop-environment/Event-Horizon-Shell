#include "ux/settings/data/monitors/settings_monitors.hpp"
#include "ux/settings/utils/monitors/settings_monitors_drm_probe.hpp"

#include "desktop_shell/unified/compositor_kind.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace eh::settings_monitors {
namespace {

namespace fs = std::filesystem;

constexpr bool kHyprMonitorCapsHyprctlJsonPrimary = false;

[[nodiscard]] std::string_view json_key_needle(std::string_view key, char (&buf)[128]) {
  buf[0] = '"';
  std::size_t nk = key.size();
  if (nk > 126) nk = 126;
  std::memcpy(buf + 1, key.data(), nk);
  buf[nk + 1] = '"';
  return std::string_view(buf, nk + 2);
}

std::string trim_copy(std::string s) {
   
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

[[nodiscard]] std::string trim_leading_blank_lines(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const size_t nl = s.find('\n', i);
    const std::string line = nl == std::string::npos ? s.substr(i) : s.substr(i, nl - i);
    if (!trim_copy(line).empty()) break;
    i = nl == std::string::npos ? s.size() : nl + 1;
  }
  return s.substr(i);
}

[[nodiscard]] std::string read_command_stdout(const char* cmd) {
  FILE* p = popen(cmd, "r");
  if (!p) return {};
  std::string out;
  std::array<char, 8192> buf{};
  while (size_t n = fread(buf.data(), 1, buf.size(), p)) out.append(buf.data(), n);
  pclose(p);
  return out;
}

[[nodiscard]] std::string home_dir() {
  if (const char* h = std::getenv("HOME")) return h;
  return {};
}

[[nodiscard]] bool hyprland_lua_config_active() {
  const std::string home = home_dir();
  if (home.empty()) return false;
  return fs::exists(fs::path(home) / ".config/hypr/hyprland.lua");
}

[[nodiscard]] std::optional<std::string> json_string_value(std::string_view obj, std::string_view key) {
  char needle_buf[128];
  const std::string_view needle = json_key_needle(key, needle_buf);
  size_t p = obj.find(needle);
  if (p == std::string_view::npos) return std::nullopt;
  p = obj.find(':', p + needle.size());
  if (p == std::string_view::npos) return std::nullopt;
  ++p;
  while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
  if (p >= obj.size()) return std::nullopt;
  if (obj[p] == '"') {
    ++p;
    std::string out;
    while (p < obj.size()) {
      char c = obj[p++];
      if (c == '\\' && p < obj.size()) {
        out += obj[p++];
        continue;
      }
      if (c == '"') break;
      out += c;
    }
    return out;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<double> json_number_after_key(std::string_view obj, std::string_view key) {
  char needle_buf[128];
  const std::string_view needle = json_key_needle(key, needle_buf);
  size_t p = obj.find(needle);
  if (p == std::string_view::npos) return std::nullopt;
  p = obj.find(':', p + needle.size());
  if (p == std::string_view::npos) return std::nullopt;
  ++p;
  while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
  char* end = nullptr;
  double v = std::strtod(obj.data() + p, &end);
  if (end == obj.data() + p) return std::nullopt;
  return v;
}

[[nodiscard]] std::optional<bool> json_bool_after_key(std::string_view obj, std::string_view key) {
  char needle_buf[128];
  const std::string_view needle = json_key_needle(key, needle_buf);
  size_t p = obj.find(needle);
  if (p == std::string_view::npos) return std::nullopt;
  p = obj.find(':', p + needle.size());
  if (p == std::string_view::npos) return std::nullopt;
  ++p;
  while (p < obj.size() && std::isspace(static_cast<unsigned char>(obj[p]))) ++p;
  if (p + 4 <= obj.size() && obj.substr(p, 4) == "true") return true;
  if (p + 5 <= obj.size() && obj.substr(p, 5) == "false") return false;
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> json_array_inner_after_key(std::string_view obj, std::string_view key) {
  char needle_buf[128];
  const std::string_view needle = json_key_needle(key, needle_buf);
  size_t p = obj.find(needle);
  if (p == std::string_view::npos) return std::nullopt;
  p = obj.find('[', p + needle.size());
  if (p == std::string_view::npos) return std::nullopt;
  size_t start = p + 1;
  int depth = 1;
  bool in_str = false;
  bool esc = false;
  for (size_t i = start; i < obj.size(); ++i) {
    char c = obj[i];
    if (esc) {
      esc = false;
      continue;
    }
    if (in_str) {
      if (c == '\\')
        esc = true;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '[')
      ++depth;
    else if (c == ']') {
      --depth;
      if (depth == 0) return obj.substr(start, i - start);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<std::string_view> split_top_json_objects(std::string_view inner) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < inner.size() && std::isspace(static_cast<unsigned char>(inner[i]))) ++i;
  while (i < inner.size()) {
    if (inner[i] != '{') break;
    size_t start = i;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (; i < inner.size(); ++i) {
      char c = inner[i];
      if (esc) {
        esc = false;
        continue;
      }
      if (in_str) {
        if (c == '\\')
          esc = true;
        else if (c == '"')
          in_str = false;
        continue;
      }
      if (c == '"') {
        in_str = true;
        continue;
      }
      if (c == '{') ++depth;
      if (c == '}') {
        --depth;
        if (depth == 0) {
          out.push_back(inner.substr(start, i - start + 1));
          ++i;
          while (i < inner.size() && (std::isspace(static_cast<unsigned char>(inner[i])) || inner[i] == ',')) ++i;
          break;
        }
      }
    }
    if (depth != 0) break;
  }
  return out;
}

[[nodiscard]] std::optional<std::string_view> json_root_array_inner(std::string_view doc) {
  size_t lb = doc.find('[');
  if (lb == std::string_view::npos) return std::nullopt;
  size_t start = lb + 1;
  int depth = 1;
  bool in_str = false;
  bool esc = false;
  for (size_t i = start; i < doc.size(); ++i) {
    char c = doc[i];
    if (esc) {
      esc = false;
      continue;
    }
    if (in_str) {
      if (c == '\\')
        esc = true;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '[')
      ++depth;
    else if (c == ']') {
      --depth;
      if (depth == 0) return doc.substr(start, i - start);
    }
  }
  return std::nullopt;
}

void parse_hypr_mode_string(const std::string& mode_str, int* w, int* h, double* hz) {
   
  *w = *h = 0;
  *hz = 0;
  size_t at = mode_str.find('@');
  std::string res = at == std::string::npos ? mode_str : mode_str.substr(0, at);
  size_t x = res.find('x');
  if (x != std::string::npos) {
    *w = std::atoi(res.c_str());
    *h = std::atoi(res.c_str() + x + 1);
  }
  if (at != std::string::npos && hz) {
    std::string hzpart = mode_str.substr(at + 1);
    if (hzpart.size() >= 2 && hzpart.compare(hzpart.size() - 2, 2, "Hz") == 0) hzpart.resize(hzpart.size() - 2);
    *hz = std::strtod(hzpart.c_str(), nullptr);
  }
}

void merge_caps_from_hypr_monitor(std::string_view mobj, OutputCaps* caps) {
   
  auto modes_inner = json_array_inner_after_key(mobj, "availableModes");
  if (modes_inner) {
    std::string blob(modes_inner->begin(), modes_inner->end());
    size_t p = 0;
    while (p < blob.size()) {
      size_t q = blob.find('"', p);
      if (q == std::string::npos) break;
      ++q;
      std::string item;
      while (q < blob.size()) {
        char c = blob[q++];
        if (c == '\\' && q < blob.size()) {
          item += blob[q++];
          continue;
        }
        if (c == '"') break;
        item += c;
      }
      int w = 0, h = 0;
      double hz = 0;
      parse_hypr_mode_string(item, &w, &h, &hz);
      if (w > 0 && h > 0) {
        std::string rk = std::to_string(w) + "x" + std::to_string(h);
        if (std::find(caps->resolutions.begin(), caps->resolutions.end(), rk) == caps->resolutions.end())
          caps->resolutions.push_back(rk);
        if (hz > 0) {
          auto& vec = caps->resolution_refresh_hz[rk];
          if (std::find(vec.begin(), vec.end(), hz) == vec.end()) vec.push_back(hz);
          if (std::find(caps->all_refresh_hz.begin(), caps->all_refresh_hz.end(), hz) == caps->all_refresh_hz.end())
            caps->all_refresh_hz.push_back(hz);
        }
      }
      p = q;
    }
  }
  if (auto bb = json_bool_after_key(mobj, "vrr")) caps->vrr_capable = *bb;
  if (auto bb = json_bool_after_key(mobj, "hdr")) caps->hdr_hint = *bb;
  if (auto s = json_string_value(mobj, "colorManagementPreset")) {
    std::string sl = *s;
    for (char& ch : sl) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (sl.find("hdr") != std::string::npos) caps->hdr_hint = true;
  }
  if (auto s = json_string_value(mobj, "make")) caps->make = *s;
  if (auto s = json_string_value(mobj, "model")) caps->model = *s;
  if (auto s = json_string_value(mobj, "description")) caps->description = *s;
  if (auto n = json_number_after_key(mobj, "width")) caps->width = static_cast<int>(*n);
  if (auto n = json_number_after_key(mobj, "height")) caps->height = static_cast<int>(*n);
  if (auto n = json_number_after_key(mobj, "refreshRate"))
    caps->current_hz = *n;
  else if (auto n2 = json_number_after_key(mobj, "refresh"))
    caps->current_hz = *n2;

  for (auto& pr : caps->resolution_refresh_hz) {
    auto& v = pr.second;
    std::sort(v.begin(), v.end(), [](double a, double b) { return a > b; });
  }
  std::sort(caps->all_refresh_hz.begin(), caps->all_refresh_hz.end(),
            [](double a, double b) { return a > b; });
  std::sort(caps->resolutions.begin(), caps->resolutions.end(), [](const std::string& a, const std::string& b) {
    auto pa = a.find('x');
    auto pb = b.find('x');
    if (pa == std::string::npos || pb == std::string::npos) return a < b;
    long wa = std::atol(a.c_str()) * std::atol(a.c_str() + pa + 1);
    long wb = std::atol(b.c_str()) * std::atol(b.c_str() + pb + 1);
    return wa > wb;
  });
}

// Normalize `cm` / `colorManagementPreset` compositor strings to monitorv2 tokens (see the compositor's CM preset docs).
[[nodiscard]] std::string hypr_cm_token_from_preset(std::string_view preset_sv) {
  std::string s(preset_sv.begin(), preset_sv.end());
  s = trim_copy(s);
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s.empty()) return {};
  static constexpr const char* kKnown[] = {"auto", "srgb", "dcip3", "dp3", "adobe", "wide", "edid", "hdr", "hdredid"};
  for (const char* k : kKnown) {
    if (s == k) return k;
  }
  if (s.find("hdr") != std::string::npos) {
    if (s.find("edid") != std::string::npos) return "hdredid";
    return "hdr";
  }
  if (s == "dci-p3" || s == "dci_p3") return "dcip3";
  if (s.find("dcip3") != std::string::npos || (s.find("dci") != std::string::npos && s.find("p3") != std::string::npos))
    return "dcip3";
  if ((s.find("display") != std::string::npos && s.find("p3") != std::string::npos) || s == "p3") return "dp3";
  if (s.find("adobe") != std::string::npos) return "adobe";
  if (s.find("wide") != std::string::npos) return "wide";
  if (s.find("edid") != std::string::npos) return "edid";
  if (s.find("srgb") != std::string::npos) return "srgb";
  if (s.find("auto") != std::string::npos) return "auto";
  return {};
}

// Normalize `sdr_eotf` compositor values to the internal token (""=follow render:cm_sdr_eotf,
// "1"=piecewise sRGB, "2"=Gamma 2.2). Accepts both the legacy integer values (0/1/2) and the
// newer string names (default/srgb/gamma22).
[[nodiscard]] std::string hypr_sdr_eotf_token(std::string val) {
  val = trim_copy(val);
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = trim_copy(val.substr(1, val.size() - 2));
  for (char& c : val) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (val.empty() || val == "0" || val == "default" || val == "follow") return {};
  if (val == "1" || val == "srgb" || val == "piecewise") return "1";
  if (val == "2" || val == "gamma22" || val == "gamma2.2") return "2";
  return val;
}

[[nodiscard]] MonitorRow row_from_hypr_json(std::string_view mobj) {
  MonitorRow r;
  if (auto n = json_string_value(mobj, "name")) r.name = *n;
  int w = 0, h = 0;
  if (auto n = json_number_after_key(mobj, "width")) w = static_cast<int>(*n);
  if (auto n = json_number_after_key(mobj, "height")) h = static_cast<int>(*n);
  if (w > 0 && h > 0) r.resolution = std::to_string(w) + "x" + std::to_string(h);
  double hz = 0;
  if (auto n = json_number_after_key(mobj, "refreshRate"))
    hz = *n;
  else if (auto n2 = json_number_after_key(mobj, "refresh"))
    hz = *n2;
  if (hz > 0) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", hz);
    r.refresh_rate = buf;
  }
  int ix = 0, iy = 0;
  if (auto n = json_number_after_key(mobj, "x")) ix = static_cast<int>(*n);
  if (auto n = json_number_after_key(mobj, "y")) iy = static_cast<int>(*n);
  r.position = std::to_string(ix) + "x" + std::to_string(iy);
  if (auto n = json_number_after_key(mobj, "scale")) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", *n);
    r.scale = buf;
  }
  if (auto n = json_number_after_key(mobj, "transform")) r.transform = std::to_string(static_cast<int>(*n));
  if (auto b = json_bool_after_key(mobj, "disabled")) r.disabled = *b;
  if (auto s = json_string_value(mobj, "bitDepth")) r.bitdepth = *s;
  else if (auto s2 = json_string_value(mobj, "bitdepth")) r.bitdepth = *s2;
  if (auto s = json_string_value(mobj, "cm")) r.cm = hypr_cm_token_from_preset(*s);
  if (r.cm.empty()) {
    if (auto s = json_string_value(mobj, "colorManagementPreset")) r.cm = hypr_cm_token_from_preset(*s);
  }
  if (auto s = json_string_value(mobj, "icc")) r.icc = *s;
  if (auto n = json_number_after_key(mobj, "vrr")) r.vrr = std::to_string(static_cast<int>(*n));
  if (auto s = json_string_value(mobj, "mirror")) r.mirror = *s;
  else if (auto s2 = json_string_value(mobj, "mirrorOf")) r.mirror = *s2;

  auto emit_g = [&](double x) {
    char b[64];
    std::snprintf(b, sizeof(b), "%g", x);
    return std::string(b);
  };
  auto try_hdr_num = [&](std::initializer_list<const char*> keys, std::string* out) {
    for (const char* k : keys) {
      if (auto n = json_number_after_key(mobj, k)) {
        *out = emit_g(*n);
        return;
      }
    }
  };
  try_hdr_num({"sdrbrightness", "sdrBrightness"}, &r.sdrbrightness);
  try_hdr_num({"sdrsaturation", "sdrSaturation"}, &r.sdrsaturation);
  if (auto n = json_number_after_key(mobj, "sdr_eotf"))
    r.sdr_eotf = std::to_string(static_cast<int>(*n));
  else if (auto n2 = json_number_after_key(mobj, "sdrEotf"))
    r.sdr_eotf = std::to_string(static_cast<int>(*n2));
  else if (auto s = json_string_value(mobj, "sdr_eotf"))
    r.sdr_eotf = hypr_sdr_eotf_token(*s);
  else if (auto s2 = json_string_value(mobj, "sdrEotf"))
    r.sdr_eotf = hypr_sdr_eotf_token(*s2);
  if (auto n = json_number_after_key(mobj, "supports_hdr"))
    r.supports_hdr = std::to_string(static_cast<int>(*n));
  else if (auto n2 = json_number_after_key(mobj, "supportsHDR"))
    r.supports_hdr = std::to_string(static_cast<int>(*n2));
  if (auto n = json_number_after_key(mobj, "supports_wide_color"))
    r.supports_wide_color = std::to_string(static_cast<int>(*n));
  else if (auto n2 = json_number_after_key(mobj, "supportsWideColor"))
    r.supports_wide_color = std::to_string(static_cast<int>(*n2));
  try_hdr_num({"sdr_min_luminance", "sdrMinLuminance"}, &r.sdr_min_luminance);
  try_hdr_num({"sdr_max_luminance", "sdrMaxLuminance"}, &r.sdr_max_luminance);
  try_hdr_num({"min_luminance", "minLuminance"}, &r.min_luminance);
  if (auto n = json_number_after_key(mobj, "max_luminance"))
    r.max_luminance = std::to_string(static_cast<int>(*n));
  else if (auto n2 = json_number_after_key(mobj, "maxLuminance"))
    r.max_luminance = std::to_string(static_cast<int>(*n2));
  if (auto n = json_number_after_key(mobj, "max_avg_luminance"))
    r.max_avg_luminance = std::to_string(static_cast<int>(*n));
  else if (auto n2 = json_number_after_key(mobj, "maxAvgLuminance"))
    r.max_avg_luminance = std::to_string(static_cast<int>(*n2));
  return r;
}

// Compositor config value: trim, strip optional quotes, then drop a trailing inline `# …` comment.
[[nodiscard]] std::string hypr_conf_value_token(std::string val) {
  for (size_t i = 0; i < val.size(); ++i) {
    if (val[i] == '#') {
      val.resize(i);
      break;
    }
  }
  val = trim_copy(val);
  if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = trim_copy(val.substr(1, val.size() - 2));
  return val;
}

struct ParsedMonitorv2Block {
  MonitorRow row;
  std::unordered_set<std::string> keys;
};

[[nodiscard]] std::optional<ParsedMonitorv2Block> parse_hypr_monitorv2_inner_body(const std::string& inner) {
  ParsedMonitorv2Block out;
  std::istringstream in(inner);
  std::string ln;
  while (std::getline(in, ln)) {
    std::string t = trim_copy(ln);
    if (t.empty() || t[0] == '#') continue;
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim_copy(t.substr(0, eq));
    std::string val = hypr_conf_value_token(t.substr(eq + 1));
    out.keys.insert(key);
    if (key == "output")
      out.row.name = val;
    else if (key == "disabled") {
      std::string vl = val;
      for (char& c : vl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      out.row.disabled = (vl == "true" || vl == "1" || vl == "yes");
    } else if (key == "mode") {
      size_t at = val.find('@');
      if (at != std::string::npos) {
        out.row.resolution = trim_copy(val.substr(0, at));
        out.row.refresh_rate = trim_copy(val.substr(at + 1));
      } else
        out.row.resolution = val;
    } else if (key == "position")
      out.row.position = val;
    else if (key == "scale")
      out.row.scale = val;
    else if (key == "transform")
      out.row.transform = val;
    else if (key == "bitdepth")
      out.row.bitdepth = val;
    else if (key == "cm")
      out.row.cm = hypr_cm_token_from_preset(val);
    else if (key == "icc")
      out.row.icc = val;
    else if (key == "vrr")
      out.row.vrr = val;
    else if (key == "mirror")
      out.row.mirror = val;
    else if (key == "sdrbrightness")
      out.row.sdrbrightness = val;
    else if (key == "sdrsaturation")
      out.row.sdrsaturation = val;
    else if (key == "sdr_eotf")
      out.row.sdr_eotf = hypr_sdr_eotf_token(val);
    else if (key == "supports_hdr")
      out.row.supports_hdr = val;
    else if (key == "supports_wide_color")
      out.row.supports_wide_color = val;
    else if (key == "sdr_min_luminance")
      out.row.sdr_min_luminance = val;
    else if (key == "sdr_max_luminance")
      out.row.sdr_max_luminance = val;
    else if (key == "min_luminance")
      out.row.min_luminance = val;
    else if (key == "max_luminance")
      out.row.max_luminance = val;
    else if (key == "max_avg_luminance")
      out.row.max_avg_luminance = val;
  }
  if (out.row.name.empty()) return std::nullopt;
  return out;
}

[[nodiscard]] std::vector<std::string> extract_hypr_monitorv2_inner_bodies(const std::string& content) {
  std::vector<std::string> lines;
  {
    std::istringstream is(content);
    std::string ln;
    while (std::getline(is, ln)) lines.push_back(std::move(ln));
  }
  std::vector<std::string> bodies;
  for (size_t i = 0; i < lines.size();) {
    std::string t = trim_copy(lines[i]);
    if (t.rfind("monitorv2", 0) == 0 && t.find('{') != std::string::npos) {
      ++i;
      std::ostringstream blk;
      int brace = 1;
      while (i < lines.size() && brace > 0) {
        for (char c : lines[i]) {
          if (c == '{') ++brace;
          else if (c == '}') --brace;
        }
        blk << lines[i] << '\n';
        ++i;
      }
      bodies.push_back(blk.str());
      continue;
    }
    ++i;
  }
  return bodies;
}

static void overlay_monitorv2_file_fields(MonitorRow* base, const ParsedMonitorv2Block& p) {
   
  for (const std::string& key : p.keys) {
    if (key == "output") continue;
    if (key == "disabled") {
      base->disabled = p.row.disabled;
      continue;
    }
    if (key == "mode") {
      if (!p.row.resolution.empty()) base->resolution = p.row.resolution;
      if (!p.row.refresh_rate.empty()) base->refresh_rate = p.row.refresh_rate;
      continue;
    }
    if (key == "position") base->position = p.row.position;
    else if (key == "scale")
      base->scale = p.row.scale;
    else if (key == "transform")
      base->transform = p.row.transform;
    else if (key == "bitdepth")
      base->bitdepth = p.row.bitdepth;
    else if (key == "cm")
      base->cm = p.row.cm;
    else if (key == "icc")
      base->icc = p.row.icc;
    else if (key == "vrr")
      base->vrr = p.row.vrr;
    else if (key == "mirror")
      base->mirror = p.row.mirror;
    else if (key == "sdrbrightness")
      base->sdrbrightness = p.row.sdrbrightness;
    else if (key == "sdrsaturation")
      base->sdrsaturation = p.row.sdrsaturation;
    else if (key == "sdr_eotf")
      base->sdr_eotf = p.row.sdr_eotf;
    else if (key == "supports_hdr")
      base->supports_hdr = p.row.supports_hdr;
    else if (key == "supports_wide_color")
      base->supports_wide_color = p.row.supports_wide_color;
    else if (key == "sdr_min_luminance")
      base->sdr_min_luminance = p.row.sdr_min_luminance;
    else if (key == "sdr_max_luminance")
      base->sdr_max_luminance = p.row.sdr_max_luminance;
    else if (key == "min_luminance")
      base->min_luminance = p.row.min_luminance;
    else if (key == "max_luminance")
      base->max_luminance = p.row.max_luminance;
    else if (key == "max_avg_luminance")
      base->max_avg_luminance = p.row.max_avg_luminance;
  }
}

[[nodiscard]] std::string strip_hypr_monitor_blocks_v2(const std::string& content) {
  std::vector<std::string> lines;
  {
    std::istringstream in(content);
    std::string ln;
    while (std::getline(in, ln)) lines.push_back(std::move(ln));
  }
  std::ostringstream out;
  for (size_t i = 0; i < lines.size();) {
    std::string t = trim_copy(lines[i]);
    if (t.rfind("monitor=", 0) == 0) {
      ++i;
      while (i < lines.size() && trim_copy(lines[i]).rfind("monitor:", 0) == 0) ++i;
      continue;
    }
    if (t.rfind("monitorv2", 0) == 0 && t.find('{') != std::string::npos) {
      ++i;
      int brace = 1;
      while (i < lines.size() && brace > 0) {
        for (char c : lines[i]) {
          if (c == '{') ++brace;
          else if (c == '}') --brace;
        }
        ++i;
      }
      // Blank lines between monitorv2 blocks are layout only; keeping them in preamble made the
      // leading section grow on every load→strip→save cycle.
      size_t j = i;
      while (j < lines.size() && trim_copy(lines[j]).empty()) ++j;
      if (j < lines.size()) {
        const std::string nt = trim_copy(lines[j]);
        if (nt.rfind("monitorv2", 0) == 0 && nt.find('{') != std::string::npos) {
          i = j;
          continue;
        }
      }
      while (i < j) {
        out << lines[i] << '\n';
        ++i;
      }
      continue;
    }
    out << lines[i] << '\n';
    ++i;
  }
  return out.str();
}

[[nodiscard]] std::string build_hypr_monitorv2_block(const MonitorRow& m) {
  std::ostringstream o;
  o << "monitorv2 {\n";
  if (m.name.find(' ') != std::string::npos || m.name.find('\t') != std::string::npos)
    o << "  output = \"" << m.name << "\"\n";
  else
    o << "  output = " << m.name << '\n';
  if (m.disabled) {
    o << "  disabled = true\n";
  } else {
    if (!m.resolution.empty()) {
      o << "  mode = " << m.resolution;
      if (!m.refresh_rate.empty()) o << "@" << m.refresh_rate;
      o << '\n';
    }
    if (!m.position.empty()) o << "  position = " << m.position << '\n';
    if (!m.scale.empty() && m.scale != "1") o << "  scale = " << m.scale << '\n';
    if (!m.transform.empty() && m.transform != "0") o << "  transform = " << m.transform << '\n';
    if (!m.bitdepth.empty()) o << "  bitdepth = " << m.bitdepth << '\n';
    if (!m.cm.empty()) o << "  cm = " << m.cm << '\n';
    if (!m.icc.empty()) o << "  icc = " << m.icc << '\n';
    if (!m.vrr.empty() && m.vrr != "0") o << "  vrr = " << m.vrr << '\n';
    if (!m.mirror.empty()) {
      if (m.mirror.find_first_of(" \t") != std::string::npos)
        o << "  mirror = \"" << m.mirror << "\"\n";
      else
        o << "  mirror = " << m.mirror << '\n';
    }
    auto emit_num = [&](const char* key, const std::string& val) {
      if (val.empty()) return;
      o << "  " << key << " = " << val << '\n';
    };
    emit_num("sdrbrightness", m.sdrbrightness);
    emit_num("sdrsaturation", m.sdrsaturation);
    emit_num("sdr_eotf", m.sdr_eotf);
    emit_num("supports_hdr", m.supports_hdr);
    emit_num("supports_wide_color", m.supports_wide_color);
    emit_num("sdr_min_luminance", m.sdr_min_luminance);
    emit_num("sdr_max_luminance", m.sdr_max_luminance);
    emit_num("min_luminance", m.min_luminance);
    emit_num("max_luminance", m.max_luminance);
    emit_num("max_avg_luminance", m.max_avg_luminance);
  }
  o << "}\n";
  return o.str();
}

// Lua hl.monitor helpers.

// Convert the inner body of hl.monitor({...}) to hyprlang-like key=value lines
// so the existing parse_hypr_monitorv2_inner_body can be reused.
[[nodiscard]] std::string lua_inner_to_hyprlang(const std::string& s) {
  std::istringstream in(s);
  std::ostringstream out;
  std::string ln;
  while (std::getline(in, ln)) {
    size_t dd = ln.find("--");
    if (dd != std::string::npos) ln.resize(dd);
    std::string t = trim_copy(ln);
    if (t.empty()) continue;
    if (t.back() == ',') t.pop_back();
    t = trim_copy(t);
    if (t.empty()) continue;
    out << t << '\n';
  }
  return out.str();
}

// Extract the inner bodies of all hl.monitor({ ... }) blocks.
[[nodiscard]] std::vector<std::string> extract_hypr_monitor_lua_bodies(const std::string& content) {
  std::vector<std::string> lines;
  { std::istringstream is(content); std::string ln; while (std::getline(is, ln)) lines.push_back(std::move(ln)); }
  std::vector<std::string> bodies;
  for (size_t i = 0; i < lines.size();) {
    std::string t = trim_copy(lines[i]);
    if (t.rfind("hl.monitor(", 0) == 0 && t.find('{') != std::string::npos) {
      ++i;
      int brace = 1;
      std::ostringstream blk;
      while (i < lines.size() && brace > 0) {
        for (char c : lines[i]) { if (c == '{') ++brace; else if (c == '}') --brace; }
        blk << lines[i] << '\n';
        ++i;
      }
      bodies.push_back(blk.str());
      continue;
    }
    ++i;
  }
  return bodies;
}

// Strip hl.monitor blocks, keeping the preamble lines intact.
[[nodiscard]] std::string strip_hypr_monitor_lua_blocks(const std::string& content) {
  std::vector<std::string> lines;
  { std::istringstream in(content); std::string ln; while (std::getline(in, ln)) lines.push_back(std::move(ln)); }
  std::ostringstream out;
  for (size_t i = 0; i < lines.size();) {
    std::string t = trim_copy(lines[i]);
    if (t.rfind("hl.monitor(", 0) == 0 && t.find('{') != std::string::npos) {
      ++i;
      int brace = 1;
      while (i < lines.size() && brace > 0) {
        for (char c : lines[i]) { if (c == '{') ++brace; else if (c == '}') --brace; }
        ++i;
      }
      size_t j = i;
      while (j < lines.size() && trim_copy(lines[j]).empty()) ++j;
      if (j < lines.size()) {
        const std::string nt = trim_copy(lines[j]);
        if (nt.rfind("hl.monitor(", 0) == 0 && nt.find('{') != std::string::npos) { i = j; continue; }
      }
      while (i < j) { out << lines[i] << '\n'; ++i; }
      continue;
    }
    out << lines[i] << '\n';
    ++i;
  }
  return out.str();
}

// Build a Lua hl.monitor({...}) block from a MonitorRow.
[[nodiscard]] std::string build_hypr_monitor_lua_block(const MonitorRow& m) {
  std::ostringstream o;
  auto q = [](const std::string& v) -> std::string {
    return "\"" + v + "\"";
  };
  o << "hl.monitor({\n";
  o << "  output = " << q(m.name) << ",\n";
  if (m.disabled) {
    o << "  disabled = true,\n";
  } else {
    if (!m.resolution.empty()) {
      o << "  mode = " << q(m.resolution + (m.refresh_rate.empty() ? "" : "@" + m.refresh_rate)) << ",\n";
    }
    if (!m.position.empty()) o << "  position = " << q(m.position) << ",\n";
    if (!m.scale.empty() && m.scale != "1") o << "  scale = " << m.scale << ",\n";
    if (!m.transform.empty() && m.transform != "0") o << "  transform = " << m.transform << ",\n";
    if (!m.bitdepth.empty()) o << "  bitdepth = " << m.bitdepth << ",\n";
    if (!m.cm.empty()) o << "  cm = " << q(m.cm) << ",\n";
    if (!m.icc.empty()) o << "  icc = " << q(m.icc) << ",\n";
    if (!m.vrr.empty() && m.vrr != "0") o << "  vrr = " << m.vrr << ",\n";
    if (!m.mirror.empty()) o << "  mirror = " << q(m.mirror) << ",\n";
    auto emit_num = [&](const char* key, const std::string& val) {
      if (val.empty()) return;
      o << "  " << key << " = " << val << ",\n";
    };
    emit_num("sdrbrightness", m.sdrbrightness);
    emit_num("sdrsaturation", m.sdrsaturation);
    if (!m.sdr_eotf.empty()) {
      const char* eotf = "default";
      if (m.sdr_eotf == "1") eotf = "srgb";
      else if (m.sdr_eotf == "2") eotf = "gamma22";
      o << "  sdr_eotf = " << q(eotf) << ",\n";
    }
    emit_num("supports_hdr", m.supports_hdr);
    emit_num("supports_wide_color", m.supports_wide_color);
    emit_num("sdr_min_luminance", m.sdr_min_luminance);
    emit_num("sdr_max_luminance", m.sdr_max_luminance);
    emit_num("min_luminance", m.min_luminance);
    emit_num("max_luminance", m.max_luminance);
    emit_num("max_avg_luminance", m.max_avg_luminance);
  }
  o << "})\n";
  return o.str();
}

[[nodiscard]] std::string build_niri_output_block(const MonitorRow& m) {
  std::ostringstream o;
  o << "output \"" << m.name << "\" {\n";
  if (m.disabled) {
    o << "    off\n";
  } else {
    if (!m.resolution.empty()) {
      o << "    mode \"" << m.resolution;
      if (!m.refresh_rate.empty()) o << "@" << m.refresh_rate;
      o << "\"\n";
    }
    if (!m.position.empty()) {
      size_t xpos = m.position.find('x');
      if (xpos != std::string::npos) {
        std::string xs = m.position.substr(0, xpos);
        std::string ys = m.position.substr(xpos + 1);
        o << "    position x=" << xs << " y=" << ys << '\n';
      }
    }
    if (!m.scale.empty() && m.scale != "1") o << "    scale " << m.scale << '\n';
    if (!m.transform.empty() && m.transform != "0") o << "    transform \"" << m.transform << "\"\n";
    if (!m.vrr.empty()) {
      if (m.vrr == "2")
        o << "    variable-refresh-rate on-demand=true\n";
      else if (m.vrr == "1")
        o << "    variable-refresh-rate\n";
    }
  }
  o << "}\n\n";
  return o.str();
}

[[nodiscard]] std::string mango_escape_pattern(const std::string& plain) {
  static const char* meta = ".*+?^${}()|[]\\";
  std::string out = "^";
  for (char c : plain) {
    if (std::strchr(meta, c)) out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('$');
  return out;
}

[[nodiscard]] std::string build_mango_monitorrule(const MonitorRow& m) {
  std::string pattern = mango_escape_pattern(m.name);
  std::ostringstream parts;
  parts << "monitorrule=name:" << pattern;
  if (!m.resolution.empty()) {
    size_t x = m.resolution.find('x');
    if (x != std::string::npos) {
      parts << ",width:" << m.resolution.substr(0, x);
      parts << ",height:" << m.resolution.substr(x + 1);
    }
  }
  if (!m.refresh_rate.empty()) parts << ",refresh:" << m.refresh_rate;
  if (!m.position.empty()) {
    size_t x = m.position.find('x');
    if (x != std::string::npos) {
      parts << ",x:" << m.position.substr(0, x);
      parts << ",y:" << m.position.substr(x + 1);
    }
  }
  double sc = std::strtod(m.scale.c_str(), nullptr);
  if (std::isnan(sc) || sc <= 0) sc = 1;
  parts << ",scale:" << sc;
  int vv = std::atoi(m.vrr.c_str());
  if (vv < 0) vv = 0;
  if (vv > 1) vv = 1;
  parts << ",vrr:" << vv;
  int ti = std::atoi(m.transform.c_str());
  if (ti < 0) ti = 0;
  if (ti > 7) ti = 7;
  parts << ",rr:" << ti;
  return parts.str();
}

[[nodiscard]] std::string strip_mango_monitorrules(const std::string& content) {
  std::vector<std::string> lines;
  std::istringstream in(content);
  std::string ln;
  while (std::getline(in, ln)) lines.push_back(std::move(ln));
  std::ostringstream out;
  for (auto& line : lines) {
    std::string t = trim_copy(line);
    std::string u = t;
    if (!u.empty() && u[0] == '#') {
      u = trim_copy(u.substr(1));
    }
    if (u.rfind("monitorrule=", 0) == 0) continue;
    out << line << '\n';
  }
  std::string r = out.str();
  while (!r.empty() && r.back() == '\n') r.pop_back();
  return r;
}

double wlr_refresh_to_hz(double r) {
   
  if (r > 500) return r / 1000.0;
  return r;
}

[[nodiscard]] std::optional<std::string_view> json_braced_object_after_key(std::string_view obj, std::string_view key) {
  char needle_buf[128];
  const std::string_view needle = json_key_needle(key, needle_buf);
  size_t p = obj.find(needle);
  if (p == std::string_view::npos) return std::nullopt;
  p = obj.find('{', p + needle.size());
  if (p == std::string_view::npos) return std::nullopt;
  const size_t start = p;
  int depth = 0;
  bool in_str = false;
  bool esc = false;
  for (size_t i = start; i < obj.size(); ++i) {
    char c = obj[i];
    if (esc) {
      esc = false;
      continue;
    }
    if (in_str) {
      if (c == '\\')
        esc = true;
      else if (c == '"')
        in_str = false;
      continue;
    }
    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '{') ++depth;
    if (c == '}') {
      --depth;
      if (depth == 0) return obj.substr(start, i - start + 1);
    }
  }
  return std::nullopt;
}

void merge_caps_wlr_output(std::string_view obj, OutputCaps* caps) {
   
  if (auto s = json_string_value(obj, "name")) caps->description = *s;
  auto modes_inner = json_array_inner_after_key(obj, "modes");
  if (modes_inner) {
    for (std::string_view piece : split_top_json_objects(*modes_inner)) {
      int w = static_cast<int>(json_number_after_key(piece, "width").value_or(0));
      int h = static_cast<int>(json_number_after_key(piece, "height").value_or(0));
      double ref = json_number_after_key(piece, "refresh").value_or(0);
      double hz = wlr_refresh_to_hz(ref);
      if (w > 0 && h > 0) {
        std::string rk = std::to_string(w) + "x" + std::to_string(h);
        if (std::find(caps->resolutions.begin(), caps->resolutions.end(), rk) == caps->resolutions.end())
          caps->resolutions.push_back(rk);
        if (hz > 0) {
          caps->resolution_refresh_hz[rk].push_back(hz);
          if (std::find(caps->all_refresh_hz.begin(), caps->all_refresh_hz.end(), hz) == caps->all_refresh_hz.end())
            caps->all_refresh_hz.push_back(hz);
        }
      }
    }
  }
  if (auto b = json_bool_after_key(obj, "adaptive_sync")) caps->vrr_capable = *b;
  std::optional<std::string_view> cur = json_braced_object_after_key(obj, "current_mode");
  if (!cur) cur = json_braced_object_after_key(obj, "mode");
  if (cur) {
    caps->width = static_cast<int>(json_number_after_key(*cur, "width").value_or(0));
    caps->height = static_cast<int>(json_number_after_key(*cur, "height").value_or(0));
    double ref = json_number_after_key(*cur, "refresh").value_or(0);
    caps->current_hz = wlr_refresh_to_hz(ref);
  }
  for (auto& pr : caps->resolution_refresh_hz) {
    auto& v = pr.second;
    std::sort(v.begin(), v.end(), [](double a, double b) { return a > b; });
  }
  std::sort(caps->all_refresh_hz.begin(), caps->all_refresh_hz.end(),
            [](double a, double b) { return a > b; });
}

[[nodiscard]] int niri_transform_to_index(std::string_view tr) {
  std::string s(tr.begin(), tr.end());
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (s.find("flipped270") != std::string::npos || s == "flipped270") return 7;
  if (s.find("flipped180") != std::string::npos) return 6;
  if (s.find("flipped90") != std::string::npos) return 5;
  if (s.find("flipped") != std::string::npos && s.find("90") == std::string::npos) return 4;
  if (s.find("270") != std::string::npos) return 3;
  if (s.find("180") != std::string::npos) return 2;
  if (s.find("90") != std::string::npos) return 1;
  return 0;
}

void merge_caps_niri_output(std::string_view obj, OutputCaps* caps) {
   
  auto modes_inner = json_array_inner_after_key(obj, "modes");
  if (modes_inner) {
    for (std::string_view piece : split_top_json_objects(*modes_inner)) {
      int w = static_cast<int>(json_number_after_key(piece, "width").value_or(0));
      int h = static_cast<int>(json_number_after_key(piece, "height").value_or(0));
      double ref = json_number_after_key(piece, "refresh_rate").value_or(0);
      double hz = ref > 500 ? ref / 1000.0 : ref;
      if (w > 0 && h > 0) {
        std::string rk = std::to_string(w) + "x" + std::to_string(h);
        if (std::find(caps->resolutions.begin(), caps->resolutions.end(), rk) == caps->resolutions.end())
          caps->resolutions.push_back(rk);
        if (hz > 0) {
          caps->resolution_refresh_hz[rk].push_back(hz);
          if (std::find(caps->all_refresh_hz.begin(), caps->all_refresh_hz.end(), hz) == caps->all_refresh_hz.end())
            caps->all_refresh_hz.push_back(hz);
        }
      }
    }
  }
  double cur_idx = json_number_after_key(obj, "current_mode").value_or(-1);
  auto modes_blob = json_array_inner_after_key(obj, "modes");
  if (modes_blob && cur_idx >= 0) {
    auto objs = split_top_json_objects(*modes_blob);
    size_t ci = static_cast<size_t>(cur_idx);
    if (ci < objs.size()) {
      int w = static_cast<int>(json_number_after_key(objs[ci], "width").value_or(0));
      int h = static_cast<int>(json_number_after_key(objs[ci], "height").value_or(0));
      double ref = json_number_after_key(objs[ci], "refresh_rate").value_or(0);
      caps->width = w;
      caps->height = h;
      caps->current_hz = ref > 500 ? ref / 1000.0 : ref;
    }
  }
  auto log_inner = [&]() -> std::optional<std::string_view> {
    const std::string needle = "\"logical\"";
    size_t p = obj.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p = obj.find('{', p + needle.size());
    if (p == std::string_view::npos) return std::nullopt;
    size_t start = p;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (size_t i = start; i < obj.size(); ++i) {
      char c = obj[i];
      if (esc) {
        esc = false;
        continue;
      }
      if (in_str) {
        if (c == '\\')
          esc = true;
        else if (c == '"')
          in_str = false;
        continue;
      }
      if (c == '"') {
        in_str = true;
        continue;
      }
      if (c == '{') ++depth;
      if (c == '}') {
        --depth;
        if (depth == 0) return obj.substr(start, i - start + 1);
      }
    }
    return std::nullopt;
  }();
  if (log_inner) {
    if (auto vs = json_bool_after_key(obj, "vrr_supported")) caps->vrr_capable = *vs;
  }
  for (auto& pr : caps->resolution_refresh_hz) {
    auto& v = pr.second;
    std::sort(v.begin(), v.end(), [](double a, double b) { return a > b; });
  }
}

[[nodiscard]] MonitorRow row_from_niri_json(std::string_view name, std::string_view obj) {
  MonitorRow r;
  r.name = std::string(name);
  double cur_idx = json_number_after_key(obj, "current_mode").value_or(-1);
  auto modes_inner = json_array_inner_after_key(obj, "modes");
  if (modes_inner && cur_idx >= 0) {
    auto objs = split_top_json_objects(*modes_inner);
    size_t ci = static_cast<size_t>(cur_idx);
    if (ci < objs.size()) {
      int w = static_cast<int>(json_number_after_key(objs[ci], "width").value_or(0));
      int h = static_cast<int>(json_number_after_key(objs[ci], "height").value_or(0));
      double ref = json_number_after_key(objs[ci], "refresh_rate").value_or(0);
      if (w > 0 && h > 0) r.resolution = std::to_string(w) + "x" + std::to_string(h);
      if (ref > 0) {
        double hz = ref > 500 ? ref / 1000.0 : ref;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f", hz);
        r.refresh_rate = buf;
      }
    }
  }
  // logical block
  size_t lp = obj.find("\"logical\"");
  if (lp != std::string_view::npos) {
    lp = obj.find('{', lp);
    if (lp != std::string_view::npos) {
      size_t start = lp;
      int depth = 0;
      bool in_str = false;
      bool esc = false;
      std::optional<std::string_view> blob;
      for (size_t i = start; i < obj.size(); ++i) {
        char c = obj[i];
        if (esc) {
          esc = false;
          continue;
        }
        if (in_str) {
          if (c == '\\')
            esc = true;
          else if (c == '"')
            in_str = false;
          continue;
        }
        if (c == '"') {
          in_str = true;
          continue;
        }
        if (c == '{') ++depth;
        if (c == '}') {
          --depth;
          if (depth == 0) {
            blob = obj.substr(start, i - start + 1);
            break;
          }
        }
      }
      if (blob) {
        int lx = static_cast<int>(json_number_after_key(*blob, "x").value_or(0));
        int ly = static_cast<int>(json_number_after_key(*blob, "y").value_or(0));
        r.position = std::to_string(lx) + "x" + std::to_string(ly);
        if (auto sc = json_number_after_key(*blob, "scale")) {
          char buf[32];
          std::snprintf(buf, sizeof(buf), "%g", *sc);
          r.scale = buf;
        }
        if (auto tr = json_string_value(*blob, "transform")) r.transform = std::to_string(niri_transform_to_index(*tr));
      }
    }
  } else {
    r.disabled = true;
  }
  if (auto b = json_bool_after_key(obj, "vrr_enabled")) r.vrr = *b ? "1" : "0";
  return r;
}

[[nodiscard]] std::vector<std::pair<std::string, std::string_view>> niri_root_outputs(std::string_view doc) {
  std::vector<std::pair<std::string, std::string_view>> out;
  size_t i = 0;
  while (i < doc.size() && std::isspace(static_cast<unsigned char>(doc[i]))) ++i;
  if (i >= doc.size() || doc[i] != '{') return out;
  ++i;
  while (i < doc.size()) {
    while (i < doc.size() && (std::isspace(static_cast<unsigned char>(doc[i])) || doc[i] == ',')) ++i;
    if (i >= doc.size() || doc[i] == '}') break;
    if (doc[i] != '"') break;
    ++i;
    std::string key;
    while (i < doc.size()) {
      char c = doc[i++];
      if (c == '\\' && i < doc.size()) {
        key += doc[i++];
        continue;
      }
      if (c == '"') break;
      key += c;
    }
    while (i < doc.size() && std::isspace(static_cast<unsigned char>(doc[i]))) ++i;
    if (i >= doc.size() || doc[i] != ':') break;
    ++i;
    while (i < doc.size() && std::isspace(static_cast<unsigned char>(doc[i]))) ++i;
    if (i >= doc.size() || doc[i] != '{') break;
    size_t ob = i;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (; i < doc.size(); ++i) {
      char c = doc[i];
      if (esc) {
        esc = false;
        continue;
      }
      if (in_str) {
        if (c == '\\')
          esc = true;
        else if (c == '"')
          in_str = false;
        continue;
      }
      if (c == '"') {
        in_str = true;
        continue;
      }
      if (c == '{') ++depth;
      if (c == '}') {
        --depth;
        if (depth == 0) {
          out.push_back({key, doc.substr(ob, i - ob + 1)});
          ++i;
          break;
        }
      }
    }
  }
  return out;
}

}  // namespace

void MonitorsTabState::refresh_from_system() {
   
  kind = detect_compositor_kind();
  caps.clear();
  outputs.clear();
  preamble.clear();
  dirty = false;
  status.clear();

  const std::string home = home_dir();
  if (home.empty()) {
    status = "HOME not set";
    capture_baseline();
    return;
  }

  if (kind == CompositorKind::Hyprland) {
    const bool luaCfg = hyprland_lua_config_active();
    conf_path = luaCfg ? (home + "/.config/hypr/monitors.lua") : (home + "/.config/hypr/monitors.conf");
    std::unordered_map<std::string, std::string> hypr_monitor_json_by_name;
    std::string hypr_json = read_command_stdout("hyprctl -j monitors 2>/dev/null");
    if (!hypr_json.empty()) {
      if (auto inner = json_root_array_inner(hypr_json)) {
        for (std::string_view mobj : split_top_json_objects(*inner)) {
          MonitorRow row = row_from_hypr_json(mobj);
          if (!row.name.empty()) {
            hypr_monitor_json_by_name[row.name] = std::string(mobj);
            OutputCaps c;
            if (kHyprMonitorCapsHyprctlJsonPrimary)
              merge_caps_from_hypr_monitor(mobj, &c);
            caps[row.name] = std::move(c);
            outputs.push_back(std::move(row));
          }
        }
      }
    }

    std::string file_content;
    std::ifstream f(conf_path);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      file_content = ss.str();
      preamble = trim_leading_blank_lines(
          luaCfg ? strip_hypr_monitor_lua_blocks(file_content) : strip_hypr_monitor_blocks_v2(file_content));
    }

    const std::vector<std::string> bodies = luaCfg
        ? extract_hypr_monitor_lua_bodies(file_content)
        : extract_hypr_monitorv2_inner_bodies(file_content);
    if (!outputs.empty()) {
      for (const std::string& body : bodies) {
        auto parsed = luaCfg
            ? parse_hypr_monitorv2_inner_body(lua_inner_to_hyprlang(body))
            : parse_hypr_monitorv2_inner_body(body);
        if (!parsed) continue;
        for (MonitorRow& row : outputs) {
          if (row.name == parsed->row.name) {
            overlay_monitorv2_file_fields(&row, *parsed);
            break;
          }
        }
      }
    } else {
      for (const std::string& body : bodies) {
        auto parsed = luaCfg
            ? parse_hypr_monitorv2_inner_body(lua_inner_to_hyprlang(body))
            : parse_hypr_monitorv2_inner_body(body);
        if (!parsed) continue;
        outputs.push_back(parsed->row);
        if (!caps.count(parsed->row.name)) caps[parsed->row.name] = OutputCaps{};
      }
      if (outputs.empty()) status = "No monitors from hyprctl (is Hyprland running?) and no monitor blocks in config";
    }

    drm_probe_merge_caps_into(&caps);
    if (!kHyprMonitorCapsHyprctlJsonPrimary) {
      for (auto& [name, blob] : hypr_monitor_json_by_name) {
        OutputCaps& oc = caps[name];
        if (oc.resolutions.empty())
          merge_caps_from_hypr_monitor(blob, &oc);
      }
    }

    capture_baseline();
    return;
  }

  if (kind == CompositorKind::Niri) {
    conf_path = home + "/.config/niri/eh/outputs.kdl";
    std::string nj = read_command_stdout("niri msg -j outputs 2>/dev/null");
    if (!nj.empty()) {
      for (auto& [name, blob] : niri_root_outputs(nj)) {
        OutputCaps c;
        merge_caps_niri_output(blob, &c);
        caps[name] = std::move(c);
        outputs.push_back(row_from_niri_json(name, blob));
      }
    }
    std::ifstream f(conf_path);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      preamble = ss.str();
    }
    if (outputs.empty()) status = "Niri — no outputs (socket OK?)";
    capture_baseline();
    return;
  }

  if (kind == CompositorKind::Mango) {
    conf_path = home + "/.config/mango/monitors.conf";
    std::string wr = read_command_stdout("wlr-randr --json 2>/dev/null");
    if (!wr.empty()) {
      std::optional<std::string_view> inner = json_root_array_inner(wr);
      if (!inner) {
        size_t lb = wr.find('[');
        if (lb != std::string::npos) inner = wr.substr(lb + 1);
      }
      if (inner) {
        for (std::string_view obj : split_top_json_objects(*inner)) {
          auto nm = json_string_value(obj, "name");
          if (!nm) continue;
          OutputCaps c;
          merge_caps_wlr_output(obj, &c);
          caps[*nm] = std::move(c);
          MonitorRow r;
          r.name = *nm;
          r.scale = "1";
          r.transform = "0";
          if (c.width > 0 && c.height > 0) r.resolution = std::to_string(c.width) + "x" + std::to_string(c.height);
          if (c.current_hz > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", c.current_hz);
            r.refresh_rate = buf;
          }
          int ix = static_cast<int>(json_number_after_key(obj, "x").value_or(0));
          int iy = static_cast<int>(json_number_after_key(obj, "y").value_or(0));
          r.position = std::to_string(ix) + "x" + std::to_string(iy);
          outputs.push_back(std::move(r));
        }
      }
    }
    std::ifstream f(conf_path);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      preamble = strip_mango_monitorrules(ss.str());
    }
    if (outputs.empty()) status = "Mango — no outputs from wlr-randr";
    capture_baseline();
    return;
  }

  status = "Compositor not supported for monitor config (Hyprland, Niri, or Mango required)";
  capture_baseline();
}

void MonitorsTabState::capture_baseline() {
   
  baseline_outputs = outputs;
  baseline_preamble = preamble;
}

void MonitorsTabState::revert_edits() {
   
  outputs = baseline_outputs;
  preamble = baseline_preamble;
  dirty = false;
  status.clear();
}

bool MonitorsTabState::save_and_reload(std::string& err_out) {
   
  err_out.clear();
  if (kind != CompositorKind::Hyprland && kind != CompositorKind::Niri && kind != CompositorKind::Mango) {
    err_out = "Unsupported compositor";
    return false;
  }
  if (conf_path.empty()) {
    err_out = "No config path";
    return false;
  }

  std::error_code ec;
  fs::create_directories(fs::path(conf_path).parent_path(), ec);
  if (ec) {
    err_out = ec.message();
    return false;
  }

  std::ostringstream body;
  if (kind == CompositorKind::Hyprland) {
    const bool luaCfg = hyprland_lua_config_active();
    const std::string pre = trim_leading_blank_lines(preamble);
    const std::string home = home_dir();
    conf_path = luaCfg ? (home + "/.config/hypr/monitors.lua") : (home + "/.config/hypr/monitors.conf");
    body << pre;
    if (!pre.empty() && pre.back() != '\n') body << '\n';
    for (const auto& m : outputs)
      body << (luaCfg ? build_hypr_monitor_lua_block(m) : build_hypr_monitorv2_block(m));
    std::ofstream out(conf_path, std::ios::trunc);
    if (!out) {
      err_out = luaCfg ? "Failed to write monitors.lua" : "Failed to write monitors.conf";
      return false;
    }
    out << body.str();
    out.close();
    int rc = std::system("hyprctl reload 2>/dev/null");
    (void)rc;
    dirty = false;
    preamble = pre;
    capture_baseline();
    status.clear();
    return true;
  }

  if (kind == CompositorKind::Niri) {
    for (const auto& m : outputs) body << build_niri_output_block(m);
    std::ofstream out(conf_path, std::ios::trunc);
    if (!out) {
      err_out = "Failed to write outputs.kdl";
      return false;
    }
    out << body.str();
    out.close();
    std::system("niri msg action reload-config-or-panic 2>/dev/null");
    dirty = false;
    capture_baseline();
    status.clear();
    return true;
  }

  // Mango
  body << preamble;
  if (!preamble.empty() && preamble.back() != '\n') body << '\n';
  for (const auto& m : outputs) {
    std::string line = build_mango_monitorrule(m);
    if (m.disabled)
      body << "# " << line << '\n';
    else
      body << line << '\n';
  }
  std::ofstream out(conf_path, std::ios::trunc);
  if (!out) {
    err_out = "Failed to write monitors.conf";
    return false;
  }
  out << body.str();
  out.close();
  std::system("mmsg -d reload_config 2>/dev/null");
  dirty = false;
  capture_baseline();
  status.clear();
  return true;
}

}  // namespace eh::settings_monitors
