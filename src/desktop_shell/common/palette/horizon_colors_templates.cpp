#include "desktop_shell/common/palette/horizon_colors_templates.hpp"

#include "color/horizon_colors.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" char** environ;

namespace fs = std::filesystem;

namespace eh::horizon_colors {
namespace {

std::string getenv_str(const char* k) {
  const char* v = std::getenv(k);
  return v ? std::string(v) : std::string{};
}



std::string config_dir() {
  std::string xdg = getenv_str("XDG_CONFIG_HOME");
  if (!xdg.empty()) return xdg;
  std::string h = getenv_str("HOME");
  if (!h.empty()) return h + "/.config";
  return "/tmp";
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
}

bool append_file(std::ostringstream& out, const fs::path& p) {
  std::ifstream in(p);
  if (!in) return false;
  out << in.rdbuf();
  out << '\n';
  return true;
}

bool exe_on_path(const char* exe) {
  const char* path_env = std::getenv("PATH");
  if (!path_env || !exe || !exe[0]) return false;
  std::string_view path(path_env);
  while (!path.empty()) {
    const size_t sep = path.find(':');
    std::string_view dir = path.substr(0, sep);
    if (!dir.empty()) {
      const std::string candidate = std::string(dir) + "/" + exe;
      if (::access(candidate.c_str(), X_OK) == 0) return true;
    }
    if (sep == std::string_view::npos) break;
    path.remove_prefix(sep + 1);
  }
  return false;
}

bool matugen_bundle_gtk_layout_is_current(const fs::path& root) {
  const fs::path gtk = root / "matugen" / "configs" / "gtk.toml";
  std::error_code ec;
  if (!fs::is_regular_file(gtk, ec)) return true;
  std::ifstream in(gtk);
  if (!in) return true;
  const std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return buf.find("[templates.ehgtk_shell_gtk4]") != std::string::npos;
}

std::optional<fs::path> bundle_near_executable() {
#ifdef __linux__
  std::vector<char> buf(8192);
  const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (n <= 0) return std::nullopt;
  buf[static_cast<size_t>(n)] = '\0';
  fs::path probe(fs::path(buf.data()).parent_path());
  for (int i = 0; i < 12; ++i) {
    if (fs::exists(probe / "matugen" / "configs" / "base.toml") && matugen_bundle_gtk_layout_is_current(probe))
      return fs::weakly_canonical(probe);
    const fs::path cand = probe / "share" / "event-horizon";
    if (fs::exists(cand / "matugen" / "configs" / "base.toml") && matugen_bundle_gtk_layout_is_current(cand))
      return fs::weakly_canonical(cand);
    if (!probe.has_parent_path()) break;
    const fs::path parent = probe.parent_path();
    if (parent == probe) break;
    probe = parent;
  }
#endif
  return std::nullopt;
}

std::optional<fs::path> bundle_root() {
  std::string er = getenv_str("EH_MATUGEN_ROOT");
  if (!er.empty()) {
    fs::path p(er);
    if (fs::exists(p / "matugen" / "configs" / "base.toml") && matugen_bundle_gtk_layout_is_current(p)) return p;
  }
  if (auto bx = bundle_near_executable()) return bx;
#ifdef EH_SYSTEM_MATUGEN_BUNDLE
  {
    const fs::path sys(EH_SYSTEM_MATUGEN_BUNDLE);
    if (fs::exists(sys / "matugen" / "configs" / "base.toml") && matugen_bundle_gtk_layout_is_current(sys)) return sys;
  }
#endif
  std::string h = getenv_str("HOME");
  if (h.empty()) return std::nullopt;
  fs::path p = fs::path(h) / ".config" / "quickshell";
  if (fs::exists(p / "matugen" / "configs" / "base.toml") && matugen_bundle_gtk_layout_is_current(p)) return p;
  return std::nullopt;
}

constexpr const char* kVscodeConfigDirs[] = {"Code", "Cursor", "VSCodium", "Code - OSS", "Code - Insiders"};

bool vscode_outputs_ok(const std::string& cfg_main) {
  if (exe_on_path("code") || exe_on_path("codium") || exe_on_path("cursor")) return true;
  const fs::path c(cfg_main);
  for (const char* d : kVscodeConfigDirs) {
    if (fs::exists(c / d)) return true;
  }
  return false;
}

std::string vscode_user_dir_for_hc(const std::string& cfg_main) {
  const fs::path base(cfg_main);
  if (exe_on_path("cursor") && fs::exists(base / "Cursor")) return (base / "Cursor" / "User").string();
  if (exe_on_path("code") && fs::exists(base / "Code")) return (base / "Code" / "User").string();
  if (exe_on_path("codium") && fs::exists(base / "VSCodium")) return (base / "VSCodium" / "User").string();
  for (const char* d : kVscodeConfigDirs) {
    if (fs::exists(base / d)) return (base / d / "User").string();
  }
  if (exe_on_path("cursor")) return (base / "Cursor" / "User").string();
  if (exe_on_path("code")) return (base / "Code" / "User").string();
  if (exe_on_path("codium")) return (base / "VSCodium" / "User").string();
  return (base / "Code" / "User").string();
}

fs::path vscode_marketplace_extensions_dir(const std::string& home, const std::string& user_dir) {
  if (user_dir.find("/Cursor/") != std::string::npos) return fs::path(home) / ".cursor" / "extensions";
  if (user_dir.find("/VSCodium/") != std::string::npos) return fs::path(home) / ".vscode-oss" / "extensions";
  if (user_dir.find("/Code - Insiders/") != std::string::npos) return fs::path(home) / ".vscode-insiders" / "extensions";
  if (user_dir.find("/Code - OSS/") != std::string::npos) return fs::path(home) / ".vscode-oss" / "extensions";
  return fs::path(home) / ".vscode" / "extensions";
}

void ensure_vscode_eh_theme_extension(const fs::path& root, const fs::path& ext_dir) {
  std::error_code ec;
  fs::create_directories(ext_dir, ec);
  const fs::path src_pkg = root / "matugen" / "vscode-eh-themes" / "package.json";
  const fs::path dst_pkg = ext_dir / "package.json";
  if (!fs::is_regular_file(src_pkg)) return;
  fs::copy_file(src_pkg, dst_pkg, fs::copy_options::overwrite_existing, ec);
}

void bump_mtime_now(const fs::path& p) {
  std::error_code ec;
  fs::last_write_time(p, std::chrono::file_clock::now(), ec);
}

void touch_vscode_theme_extension_outputs(const fs::path& ext_eh_dir) {
  std::error_code ec;
  static constexpr const char* kNames[] = {"matugen-eh-dark.json", "matugen-eh-light.json", "matugen-eh-default.json"};
  for (const char* name : kNames) {
    const fs::path f = ext_eh_dir / name;
    if (fs::is_regular_file(f, ec)) bump_mtime_now(f);
  }
}

void fix_paths(std::string& cfg, const fs::path& shell_dir, const std::string& cfg_dir, const std::string& home,
               const std::string& vscode_user_dir, const std::string& vscode_ext_eh_themes_dir) {
  replace_all(cfg, "SHELL_DIR", shell_dir.string());
  replace_all(cfg, "CONFIG_DIR", cfg_dir);
  replace_all(cfg, "VSCODE_USER_DIR", vscode_user_dir);
  replace_all(cfg, "VSCODE_EXT_EH_THEMES_DIR", vscode_ext_eh_themes_dir);
  replace_all(cfg, "HOME_DIR", home);
  replace_all(cfg, "CACHE_DIR", home + "/.cache");
  replace_all(cfg, "DATA_DIR", home + "/.local/share");
  replace_all(cfg, "EMACS_DIR", home + "/.emacs.d");
  replace_all(cfg, "input_path = '~/", "input_path = '" + home + "/");
  replace_all(cfg, "input_path = '~\\./", "input_path = '" + home + "/.");
  replace_all(cfg, "input_path = './matugen/templates/",
              "input_path = '" + (shell_dir / "matugen" / "templates").string() + "/");
  replace_all(cfg, "input_path = '" + home + "/.config/quickshell/matugen/templates/",
              "input_path = '" + (shell_dir / "matugen" / "templates").string() + "/");
  replace_all(cfg, "output_path = '~/", "output_path = '" + home + "/");
  replace_all(cfg, "output_path = '~\\./", "output_path = '" + home + "/.");
}

// TOML template entry parser.

struct TemplateEntry {
  std::string name;
  std::string input_path;
  std::string output_path;
};

std::vector<TemplateEntry> parse_template_entries(const std::string& toml_body) {
  std::vector<TemplateEntry> entries;
  std::istringstream in(toml_body);
  std::string line;
  std::string current_name;

  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();

    if (line.size() > 12 && line.substr(0, 11) == "[templates." && line.back() == ']') {
      current_name = line.substr(11, line.size() - 12);
      if (!current_name.empty()) {
        entries.push_back({});
        entries.back().name = current_name;
      }
      continue;
    }

    if (!line.empty() && line.front() == '[') {
      current_name.clear();
      continue;
    }

    if (current_name.empty() || entries.empty()) continue;

    auto parse_quoted = [&](const std::string& key) -> std::string {
      const std::string needle = key + " = ";
      auto pos = line.find(needle);
      if (pos == std::string::npos) return {};
      pos += needle.size();
      if (pos >= line.size()) return {};
      char q = line[pos];
      if (q != '\'' && q != '"') return {};
      auto end = line.find(q, pos + 1);
      if (end == std::string::npos) return {};
      return line.substr(pos + 1, end - pos - 1);
    };

    auto& entry = entries.back();
    if (entry.input_path.empty()) entry.input_path = parse_quoted("input_path");
    if (entry.output_path.empty()) entry.output_path = parse_quoted("output_path");
  }

  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [](const TemplateEntry& e) { return e.input_path.empty() || e.output_path.empty(); }),
      entries.end());
  return entries;
}

// Role name → index mapping.

static const std::unordered_map<std::string, uint8_t> kRoleMap = {
    {"primary", 0},
    {"on_primary", 1},
    {"primary_container", 2},
    {"on_primary_container", 3},
    {"secondary", 4},
    {"on_secondary", 5},
    {"secondary_container", 6},
    {"on_secondary_container", 7},
    {"tertiary", 8},
    {"on_tertiary", 9},
    {"tertiary_container", 10},
    {"on_tertiary_container", 11},
    {"error", 12},
    {"on_error", 13},
    {"error_container", 14},
    {"on_error_container", 15},
    {"surface", 16},
    {"on_surface", 17},
    {"surface_variant", 18},
    {"on_surface_variant", 19},
    {"surface_dim", 20},
    {"surface_bright", 21},
    {"surface_container_lowest", 22},
    {"surface_container_low", 23},
    {"surface_container", 24},
    {"surface_container_high", 25},
    {"surface_container_highest", 26},
    {"inverse_surface", 27},
    {"inverse_on_surface", 28},
    {"inverse_primary", 29},
    {"outline", 30},
    {"outline_variant", 31},
    {"shadow", 32},
    {"scrim", 33},
    {"surface_tint", 34},
    {"primary_fixed", 35},
    {"primary_fixed_dim", 36},
    {"on_primary_fixed", 37},
    {"on_primary_fixed_variant", 38},
    {"secondary_fixed", 39},
    {"secondary_fixed_dim", 40},
    {"on_secondary_fixed", 41},
    {"on_secondary_fixed_variant", 42},
    {"tertiary_fixed", 43},
    {"tertiary_fixed_dim", 44},
    {"on_tertiary_fixed", 45},
    {"on_tertiary_fixed_variant", 46},
};

eh::color::Argb resolve_color_token(const std::unordered_map<uint8_t, eh::color::Argb>& roles,
                                    const std::string& name, bool is_dark) {
  auto it = kRoleMap.find(name);
  if (it != kRoleMap.end()) {
    auto ri = roles.find(it->second);
    if (ri != roles.end()) return ri->second;
  }
  if (name == "background") {
    auto r = roles.find(16);
    return r != roles.end() ? r->second : (is_dark ? 0xFF000000 : 0xFFFFFFFF);
  }
  if (name == "on_background") {
    auto r = roles.find(17);
    return r != roles.end() ? r->second : (is_dark ? 0xFFFFFFFF : 0xFF000000);
  }
  if (name == "source_color") {
    return 0;
  }
  return is_dark ? 0xFFFFFFFF : 0xFF000000;
}

// Base16 palette.

struct Base16Palette {
  eh::color::Argb colors[16];
};

Base16Palette generate_base16(const std::unordered_map<uint8_t, eh::color::Argb>& roles, bool is_dark) {
  Base16Palette b{};
  if (is_dark) {
    b.colors[0x00] = roles.count(20) ? roles.at(20) : 0;
    b.colors[0x01] = roles.count(22) ? roles.at(22) : 0;
    b.colors[0x02] = roles.count(23) ? roles.at(23) : 0;
    b.colors[0x03] = roles.count(24) ? roles.at(24) : 0;
    b.colors[0x04] = roles.count(25) ? roles.at(25) : 0;
    b.colors[0x05] = roles.count(17) ? roles.at(17) : 0;
    b.colors[0x06] = roles.count(19) ? roles.at(19) : 0;
    b.colors[0x07] = roles.count(0)  ? roles.at(0)  : 0;
    b.colors[0x08] = roles.count(12) ? roles.at(12) : 0;
    b.colors[0x09] = roles.count(8)  ? roles.at(8)  : 0;
    b.colors[0x0A] = roles.count(4)  ? roles.at(4)  : 0;
    b.colors[0x0B] = roles.count(2)  ? roles.at(2)  : 0;
    b.colors[0x0C] = roles.count(6)  ? roles.at(6)  : 0;
    b.colors[0x0D] = roles.count(3)  ? roles.at(3)  : 0;
    b.colors[0x0E] = roles.count(7)  ? roles.at(7)  : 0;
    b.colors[0x0F] = roles.count(14) ? roles.at(14) : 0;
  } else {
    b.colors[0x00] = roles.count(21) ? roles.at(21) : 0;
    b.colors[0x01] = roles.count(26) ? roles.at(26) : 0;
    b.colors[0x02] = roles.count(25) ? roles.at(25) : 0;
    b.colors[0x03] = roles.count(24) ? roles.at(24) : 0;
    b.colors[0x04] = roles.count(23) ? roles.at(23) : 0;
    b.colors[0x05] = roles.count(17) ? roles.at(17) : 0;
    b.colors[0x06] = roles.count(19) ? roles.at(19) : 0;
    b.colors[0x07] = roles.count(0)  ? roles.at(0)  : 0;
    b.colors[0x08] = roles.count(12) ? roles.at(12) : 0;
    b.colors[0x09] = roles.count(8)  ? roles.at(8)  : 0;
    b.colors[0x0A] = roles.count(4)  ? roles.at(4)  : 0;
    b.colors[0x0B] = roles.count(2)  ? roles.at(2)  : 0;
    b.colors[0x0C] = roles.count(6)  ? roles.at(6)  : 0;
    b.colors[0x0D] = roles.count(3)  ? roles.at(3)  : 0;
    b.colors[0x0E] = roles.count(7)  ? roles.at(7)  : 0;
    b.colors[0x0F] = roles.count(14) ? roles.at(14) : 0;
  }
  return b;
}

// HSL lighten.

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

eh::color::Argb lighten_argb(eh::color::Argb c, float amount) {
  float r = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
  float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
  float b = static_cast<float>(c & 0xFF) / 255.0f;
  float a = static_cast<float>((c >> 24) & 0xFF) / 255.0f;

  float cmax = std::max({r, g, b});
  float cmin = std::min({r, g, b});
  float l = (cmax + cmin) / 2.0f;
  float h = 0, s = 0;

  if (cmax != cmin) {
    float d = cmax - cmin;
    s = l > 0.5f ? d / (2.0f - cmax - cmin) : d / (cmax + cmin);
    if (cmax == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (cmax == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h /= 6.0f;
  }

  l = clampf(l + amount / 100.0f, 0.0f, 1.0f);

  auto hue2rgb = [](float p, float q, float t) -> float {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 0.5f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
  };

  float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
  float p = 2 * l - q;
  r = hue2rgb(p, q, h + 1.0f / 3.0f);
  g = hue2rgb(p, q, h);
  b = hue2rgb(p, q, h - 1.0f / 3.0f);

  auto to_byte = [](float v) -> uint8_t {
    int i = static_cast<int>(v * 255.0f + 0.5f);
    return static_cast<uint8_t>(clampf(static_cast<float>(i), 0, 255));
  };

  return (to_byte(a) << 24) | (to_byte(r) << 16) | (to_byte(g) << 8) | to_byte(b);
}

// Template placeholder replacement.

std::string hex_from_argb(eh::color::Argb c) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%06x", c & 0x00FFFFFF);
  return std::string(buf);
}

// Replace all {{ namespace.token.mode.hex }} placeholders
void replace_namespace_placeholders(std::string& content, const std::string& ns,
                                    auto lookup_fn, bool is_dark) {
  // Handle both "{{ns." and "{{ ns." (with/without space) spacing
  const std::string opens[] = {"{{" + ns + ".", "{{ " + ns + "."};
  for (const auto& open : opens) {
  std::string::size_type pos = 0;
  while ((pos = content.find(open, pos)) != std::string::npos) {
    auto end_close = content.find("}}", pos);
    if (end_close == std::string::npos) break;

    std::string token = content.substr(pos + open.size(), end_close - pos - open.size());

    // Handle optional spaces
    while (!token.empty() && token.front() == ' ') token.erase(token.begin());
    while (!token.empty() && token.back() == ' ') token.pop_back();

    // Split on '.' to get parts: token.mode.hex  or  token.mode.hex | filter: N
    std::string token_name;
    std::string mode;
    std::string fmt;
    float filter_amount = 0;
    bool has_filter = false;

    // Check for filter first
    auto pipe_pos = token.find('|');
    std::string main_part = (pipe_pos != std::string::npos) ? token.substr(0, pipe_pos) : token;
    if (pipe_pos != std::string::npos) {
      std::string filter_str = token.substr(pipe_pos + 1);
      while (!filter_str.empty() && filter_str.front() == ' ') filter_str.erase(filter_str.begin());
      // Parse "lighten: N.N"
      auto colon = filter_str.find(':');
      if (colon != std::string::npos && filter_str.substr(0, colon) == "lighten") {
        std::string val = filter_str.substr(colon + 1);
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        filter_amount = std::stof(val);
        has_filter = true;
      }
    }

    // Parse main_part: token_name.mode.fmt
    auto last_dot = main_part.rfind('.');
    if (last_dot == std::string::npos) { pos = end_close + 2; continue; }
    fmt = main_part.substr(last_dot + 1);
    auto second_last_dot = main_part.rfind('.', last_dot - 1);
    if (second_last_dot == std::string::npos) { pos = end_close + 2; continue; }
    mode = main_part.substr(second_last_dot + 1, last_dot - second_last_dot - 1);
    token_name = main_part.substr(0, second_last_dot);

    // Accept "dark", "light", or "default" as mode; "default" uses current is_dark
    if (mode == "default") {
      mode = is_dark ? "dark" : "light";
    }

    // Only replace if mode matches current is_dark
    if ((is_dark && mode == "dark") || (!is_dark && mode == "light")) {
      eh::color::Argb color = lookup_fn(token_name);
      if (has_filter) {
        color = lighten_argb(color, filter_amount);
      }
      // Format output based on fmt token (hex, hex_stripped, red, green, blue, rgb)
      std::string replacement;
      if (fmt == "hex_stripped") {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "%06x", color & 0x00FFFFFF);
        replacement = buf;
      } else if (fmt == "red") {
        replacement = std::to_string((color >> 16) & 0xFF);
      } else if (fmt == "green") {
        replacement = std::to_string((color >> 8) & 0xFF);
      } else if (fmt == "blue") {
        replacement = std::to_string(color & 0xFF);
      } else if (fmt == "rgb") {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d, %d, %d",
                      (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        replacement = buf;
      } else {
        // Default: hex with # prefix
        replacement = hex_from_argb(color);
      }
      content.replace(pos, end_close + 2 - pos, replacement);
      pos += replacement.size();
      continue;
    }

    pos = end_close + 2;
  }
  } // for open pattern
}

void process_template(std::string& content, const std::unordered_map<uint8_t, eh::color::Argb>& roles,
                      const Base16Palette& base16, eh::color::Argb source_color, bool is_dark) {
  // {{ colors.X.dark.hex }}
  replace_namespace_placeholders(content, "colors",
    [&](const std::string& name) -> eh::color::Argb {
      if (name == "source_color") return source_color;
      return resolve_color_token(roles, name, is_dark);
    }, is_dark);

  // {{ base16.XX.dark.hex }}
  replace_namespace_placeholders(content, "base16",
    [&](const std::string& name) -> eh::color::Argb {
      if (name.size() == 6 && name.substr(0, 4) == "base") {
        int idx = -1;
        const std::string hex = name.substr(4);
        if (hex.size() == 2) {
          auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
          };
          const int hi = digit(hex[0]);
          const int lo = digit(hex[1]);
          if (hi >= 0 && lo >= 0) idx = hi * 16 + lo;
        }
        if (idx >= 0 && idx < 16) return base16.colors[idx];
      }
      return 0;
    }, is_dark);

  // {{ event16.colorN.dark.hex }} — legacy, same as base16 with mapping
  static constexpr int kEvent16Map[] = {0, 8, 11, 10, 13, 14, 12, 5, 3, 8, 11, 10, 13, 14, 12, 7};
  replace_namespace_placeholders(content, "event16",
    [&](const std::string& name) -> eh::color::Argb {
      if (name.size() >= 5 && name.substr(0, 5) == "color") {
        int idx = -1;
        try { idx = std::stoi(name.substr(5)); } catch (...) {}
        if (idx >= 0 && idx < 16) return base16.colors[kEvent16Map[idx]];
      }
      return 0;
    }, is_dark);
}

// Post-palette hooks (reused from external templates).

void merge_vesktop_vencord_settings(const fs::path& vesktop_data_dir) {
  try {
    const fs::path settings_path = vesktop_data_dir / "settings" / "settings.json";
    const fs::path theme_css = vesktop_data_dir / "themes" / "midnight.theme.css";
    if (!fs::is_regular_file(theme_css)) return;
    nlohmann::json root = nlohmann::json::object();
    if (fs::is_regular_file(settings_path)) {
      std::ifstream in(settings_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try { root = nlohmann::json::parse(raw, nullptr, true, true, true); } catch (...) {}
    }
    nlohmann::json& et = root["enabledThemes"];
    if (!et.is_array()) et = nlohmann::json::array();
    bool found = false;
    for (const auto& x : et) {
      if (x.is_string() && x.get<std::string>() == "midnight.theme.css") { found = true; break; }
    }
    if (!found) et.push_back("midnight.theme.css");
    std::error_code ec;
    fs::create_directories(settings_path.parent_path(), ec);
    std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << root.dump(2) << '\n';
  } catch (...) {}
}

void sync_btop(const std::string& cfg_main) {
  std::error_code ec;
  const fs::path conf = fs::path(cfg_main) / "btop" / "btop.conf";
  fs::create_directories(conf.parent_path(), ec);
  std::string raw;
  if (fs::is_regular_file(conf)) {
    std::ifstream in(conf);
    raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  std::vector<std::string> lines;
  {
    std::istringstream is(raw);
    std::string ln;
    while (std::getline(is, ln)) {
      if (!ln.empty() && ln.back() == '\r') ln.pop_back();
      lines.push_back(std::move(ln));
    }
  }
  static constexpr const char* kThemeLine = R"(color_theme = "eh-matugen")";
  bool replaced = false;
  for (auto& ln : lines) {
    std::string_view sv(ln);
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
    if (sv.size() >= 11 && sv.substr(0, 11) == "color_theme") {
      ln = kThemeLine;
      replaced = true;
      break;
    }
  }
  if (!replaced) lines.emplace_back(kThemeLine);
  {
    std::ofstream out(conf, std::ios::binary | std::ios::trunc);
    if (!out) return;
    for (const auto& l : lines) out << l << '\n';
  }
  char argv0[] = "pkill";
  char argv1[] = "-SIGUSR2";
  char argv2[] = "-x";
  char argv3[] = "btop";
  char* argv[] = {argv0, argv1, argv2, argv3, nullptr};
  pid_t pid = -1;
  if (posix_spawnp(&pid, "pkill", nullptr, nullptr, argv, environ) != 0 || pid < 0) return;
  (void)waitpid(pid, nullptr, 0);
}

void sync_otter_term(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "otter-shell" / "otter-term-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) return;
  std::ifstream min(matugen_file);
  if (!min) return;
  std::vector<std::string> matugen_keys, matugen_hex;
  std::string ln;
  while (std::getline(min, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    const auto eq = ln.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = ln.substr(0, eq);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    if (key.size() < 7 || key.substr(0, 7) != "colors_") continue;
    std::string val = (eq + 1 < ln.size()) ? ln.substr(eq + 1) : "";
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
    if (val.size() >= 8 && val[0] == '"' && val[1] == '#') {
      size_t end = val.find('"', 2);
      if (end != std::string::npos) {
        matugen_keys.push_back(key);
        matugen_hex.push_back(val.substr(2, end - 2));
      }
    }
  }
  if (matugen_keys.empty()) return;
  const fs::path conf_path = fs::path(cfg_main) / "otter-shell" / "otter-term.conf";
  if (!fs::is_regular_file(conf_path, ec)) return;
  std::ifstream in(conf_path);
  std::vector<std::string> lines;
  while (std::getline(in, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    const auto eq = ln.find('=');
    if (eq != std::string::npos && eq > 0) {
      std::string key = ln.substr(0, eq);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      if (key.size() >= 7 && key.substr(0, 7) == "colors_") {
        for (size_t i = 0; i < matugen_keys.size(); ++i) {
          if (key == matugen_keys[i]) {
            std::string existing_alpha = (key == "colors_background") ? "BF" : "";
            if (existing_alpha.empty()) {
              auto v_start = ln.find_first_not_of(" \t", eq + 1);
              if (v_start != std::string::npos) {
                std::string oval = ln.substr(v_start);
                if (oval.size() >= 11 && oval[0] == '"' && oval[1] == '#' && oval[10] == '"')
                  existing_alpha = oval.substr(8, 2);
              }
            }
            ln = key + " = \"#" + matugen_hex[i] + existing_alpha + "\"";
            break;
          }
        }
      }
    }
    lines.push_back(std::move(ln));
  }
  std::ofstream out(conf_path, std::ios::binary | std::ios::trunc);
  if (!out) return;
  for (const auto& l : lines) out << l << '\n';
}

// Horizon Files sync.
// Reads the generated horizon-files-matugen.conf (hex values) and writes
// float RGB values into ~/.config/event-horizon/state-settings.toml [appearance].

static bool parse_hex_rgb(const std::string& hex, double& r, double& g, double& b) {
  std::string h = hex;
  if (!h.empty() && h[0] == '#') h = h.substr(1);
  if (h.size() != 6) return false;
  try {
    unsigned long val = std::stoul(h, nullptr, 16);
    r = ((val >> 16) & 0xFF) / 255.0;
    g = ((val >> 8) & 0xFF) / 255.0;
    b = (val & 0xFF) / 255.0;
    return true;
  } catch (...) { return false; }
}

static std::string fmt_float(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  return buf;
}

void sync_horizon_files(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-files-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) return;
  std::ifstream min(matugen_file);
  if (!min) return;

  // Parse key = "#RRGGBB" lines
  std::unordered_map<std::string, std::string> hex_map;
  std::string ln;
  while (std::getline(min, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    auto eq = ln.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = ln.substr(0, eq);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    std::string val = (eq + 1 < ln.size()) ? ln.substr(eq + 1) : "";
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
      val = val.substr(1, val.size() - 2);
    }
    if (!val.empty()) hex_map[key] = val;
  }
  if (hex_map.empty()) return;

  // Map template keys → state-settings.toml [appearance] TOML keys
  struct ColorMapping {
    const char* hex_key;
    const char* r_key, *g_key, *b_key;
  };
  // Legacy chrome keys (consumed by older horizon-files builds) followed by
  // the full M3 role set in m3::ColorRole enum order (outline is covered by
  // the legacy row above, so it has no duplicate entry).
  static const ColorMapping mappings[] = {
    {"dock_fill",                 "matugenDockFillR",                 "matugenDockFillG",                 "matugenDockFillB"},
    {"panel_fill",                "matugenPanelFillR",                "matugenPanelFillG",                "matugenPanelFillB"},
    {"drawer_dim",                "matugenDrawerDimR",                "matugenDrawerDimG",                "matugenDrawerDimB"},
    {"outline",                   "matugenOutlineR",                  "matugenOutlineG",                  "matugenOutlineB"},
    {"accent",                    "matugenAccentR",                   "matugenAccentG",                   "matugenAccentB"},
    {"text",                      "matugenTextR",                     "matugenTextG",                     "matugenTextB"},
    {"notif_critical_bg",         "matugenNotifCriticalBgR",          "matugenNotifCriticalBgG",          "matugenNotifCriticalBgB"},
    {"notif_critical_outline",    "matugenNotifCriticalOutlineR",     "matugenNotifCriticalOutlineG",     "matugenNotifCriticalOutlineB"},

    {"primary",                   "matugenPrimaryR",                   "matugenPrimaryG",                   "matugenPrimaryB"},
    {"on_primary",                "matugenOnPrimaryR",                 "matugenOnPrimaryG",                 "matugenOnPrimaryB"},
    {"primary_container",         "matugenPrimaryContainerR",          "matugenPrimaryContainerG",          "matugenPrimaryContainerB"},
    {"on_primary_container",      "matugenOnPrimaryContainerR",        "matugenOnPrimaryContainerG",        "matugenOnPrimaryContainerB"},

    {"secondary",                 "matugenSecondaryR",                 "matugenSecondaryG",                 "matugenSecondaryB"},
    {"on_secondary",              "matugenOnSecondaryR",               "matugenOnSecondaryG",               "matugenOnSecondaryB"},
    {"secondary_container",       "matugenSecondaryContainerR",        "matugenSecondaryContainerG",        "matugenSecondaryContainerB"},
    {"on_secondary_container",    "matugenOnSecondaryContainerR",      "matugenOnSecondaryContainerG",      "matugenOnSecondaryContainerB"},

    {"tertiary",                  "matugenTertiaryR",                  "matugenTertiaryG",                  "matugenTertiaryB"},
    {"on_tertiary",               "matugenOnTertiaryR",                "matugenOnTertiaryG",                "matugenOnTertiaryB"},
    {"tertiary_container",        "matugenTertiaryContainerR",         "matugenTertiaryContainerG",         "matugenTertiaryContainerB"},
    {"on_tertiary_container",     "matugenOnTertiaryContainerR",       "matugenOnTertiaryContainerG",       "matugenOnTertiaryContainerB"},

    {"error",                     "matugenErrorR",                     "matugenErrorG",                     "matugenErrorB"},
    {"on_error",                  "matugenOnErrorR",                   "matugenOnErrorG",                   "matugenOnErrorB"},
    {"error_container",           "matugenErrorContainerR",            "matugenErrorContainerG",            "matugenErrorContainerB"},
    {"on_error_container",        "matugenOnErrorContainerR",          "matugenOnErrorContainerG",          "matugenOnErrorContainerB"},

    {"surface",                   "matugenSurfaceR",                   "matugenSurfaceG",                   "matugenSurfaceB"},
    {"on_surface",                "matugenOnSurfaceR",                 "matugenOnSurfaceG",                 "matugenOnSurfaceB"},
    {"surface_variant",           "matugenSurfaceVariantR",            "matugenSurfaceVariantG",            "matugenSurfaceVariantB"},
    {"on_surface_variant",        "matugenOnSurfaceVariantR",          "matugenOnSurfaceVariantG",          "matugenOnSurfaceVariantB"},

    {"surface_dim",               "matugenSurfaceDimR",                "matugenSurfaceDimG",                "matugenSurfaceDimB"},
    {"surface_bright",            "matugenSurfaceBrightR",             "matugenSurfaceBrightG",             "matugenSurfaceBrightB"},
    {"surface_container_lowest",  "matugenSurfaceContainerLowestR",    "matugenSurfaceContainerLowestG",    "matugenSurfaceContainerLowestB"},
    {"surface_container_low",     "matugenSurfaceContainerLowR",       "matugenSurfaceContainerLowG",       "matugenSurfaceContainerLowB"},
    {"surface_container",         "matugenSurfaceContainerR",          "matugenSurfaceContainerG",          "matugenSurfaceContainerB"},
    {"surface_container_high",    "matugenSurfaceContainerHighR",      "matugenSurfaceContainerHighG",      "matugenSurfaceContainerHighB"},
    {"surface_container_highest", "matugenSurfaceContainerHighestR",   "matugenSurfaceContainerHighestG",   "matugenSurfaceContainerHighestB"},

    {"inverse_surface",           "matugenInverseSurfaceR",            "matugenInverseSurfaceG",            "matugenInverseSurfaceB"},
    {"inverse_on_surface",        "matugenInverseOnSurfaceR",          "matugenInverseOnSurfaceG",          "matugenInverseOnSurfaceB"},
    {"inverse_primary",           "matugenInversePrimaryR",            "matugenInversePrimaryG",            "matugenInversePrimaryB"},

    {"outline_variant",           "matugenOutlineVariantR",            "matugenOutlineVariantG",            "matugenOutlineVariantB"},
    {"shadow",                    "matugenShadowR",                    "matugenShadowG",                    "matugenShadowB"},
    {"scrim",                     "matugenScrimR",                     "matugenScrimG",                     "matugenScrimB"},
    {"surface_tint",              "matugenSurfaceTintR",               "matugenSurfaceTintG",               "matugenSurfaceTintB"},

    {"primary_fixed",             "matugenPrimaryFixedR",              "matugenPrimaryFixedG",              "matugenPrimaryFixedB"},
    {"primary_fixed_dim",         "matugenPrimaryFixedDimR",           "matugenPrimaryFixedDimG",           "matugenPrimaryFixedDimB"},
    {"on_primary_fixed",          "matugenOnPrimaryFixedR",            "matugenOnPrimaryFixedG",            "matugenOnPrimaryFixedB"},
    {"on_primary_fixed_variant",  "matugenOnPrimaryFixedVariantR",     "matugenOnPrimaryFixedVariantG",     "matugenOnPrimaryFixedVariantB"},

    {"secondary_fixed",           "matugenSecondaryFixedR",            "matugenSecondaryFixedG",            "matugenSecondaryFixedB"},
    {"secondary_fixed_dim",       "matugenSecondaryFixedDimR",         "matugenSecondaryFixedDimG",         "matugenSecondaryFixedDimB"},
    {"on_secondary_fixed",        "matugenOnSecondaryFixedR",          "matugenOnSecondaryFixedG",          "matugenOnSecondaryFixedB"},
    {"on_secondary_fixed_variant","matugenOnSecondaryFixedVariantR",   "matugenOnSecondaryFixedVariantG",   "matugenOnSecondaryFixedVariantB"},

    {"tertiary_fixed",            "matugenTertiaryFixedR",             "matugenTertiaryFixedG",             "matugenTertiaryFixedB"},
    {"tertiary_fixed_dim",        "matugenTertiaryFixedDimR",          "matugenTertiaryFixedDimG",          "matugenTertiaryFixedDimB"},
    {"on_tertiary_fixed",         "matugenOnTertiaryFixedR",           "matugenOnTertiaryFixedG",           "matugenOnTertiaryFixedB"},
    {"on_tertiary_fixed_variant", "matugenOnTertiaryFixedVariantR",    "matugenOnTertiaryFixedVariantG",    "matugenOnTertiaryFixedVariantB"},
  };

  // Read existing state-settings.toml
  const fs::path settings_path = fs::path(cfg_main) / "event-horizon" / "state-settings.toml";
  std::vector<std::string> settings_lines;
  if (fs::is_regular_file(settings_path, ec)) {
    std::ifstream sin(settings_path);
    while (std::getline(sin, ln)) {
      if (!ln.empty() && ln.back() == '\r') ln.pop_back();
      settings_lines.push_back(std::move(ln));
    }
  }

  // Build set of keys to insert/update
  std::unordered_map<std::string, std::string> replacements;
  for (const auto& m : mappings) {
    auto it = hex_map.find(m.hex_key);
    if (it == hex_map.end()) continue;
    double r, g, b;
    if (!parse_hex_rgb(it->second, r, g, b)) continue;
    replacements[m.r_key] = fmt_float(r);
    replacements[m.g_key] = fmt_float(g);
    replacements[m.b_key] = fmt_float(b);
  }
  if (replacements.empty()) return;

  // Patch [appearance]: update keys in place, drop misplaced occurrences that
  // earlier versions appended past the section (they used to land in whatever
  // table closed the file, e.g. [dock]), and insert missing keys right after
  // the [appearance] header — never at EOF.
  int replaced = 0;
  {
    bool has_header = false;
    for (const auto& line : settings_lines) {
      if (line == "[appearance]") { has_header = true; break; }
    }

    std::unordered_set<std::string> pending;
    for (const auto& [key, val] : replacements) pending.insert(key);

    std::vector<std::string> out;
    out.reserve(settings_lines.size() + replacements.size());
    bool inside_appearance = false;
    bool header_emitted = false;
    auto flush_pending = [&]() {
      for (const auto& [key, val] : replacements) {
        if (!pending.count(key)) continue;
        out.push_back(key + " = " + val);
      }
      pending.clear();
    };
    for (auto& line : settings_lines) {
      const bool is_table_header = !line.empty() && line[0] == '[';
      if (is_table_header) {
        if (inside_appearance && !pending.empty()) flush_pending();
        inside_appearance = (line == "[appearance]");
        if (inside_appearance) header_emitted = true;
        out.push_back(line);
        continue;
      }
      auto eq = line.find('=');
      if (eq == std::string::npos || eq == 0) {
        out.push_back(line);
        continue;
      }
      std::string key = line.substr(0, eq);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      auto it = replacements.find(key);
      if (it == replacements.end()) {
        out.push_back(std::move(line));
        continue;
      }
      if (inside_appearance || !has_header) {
        out.push_back(key + " = " + it->second);
        ++replaced;
      }
      pending.erase(key);
    }
    if (!header_emitted) {
      out.push_back("[appearance]");
      inside_appearance = true;
    }
    if (!pending.empty()) flush_pending();

    settings_lines = std::move(out);
  }

  // Set matugenPaletteOk = true
  for (auto& line : settings_lines) {
    if (line.find("matugenPaletteOk") != std::string::npos) {
      line = "matugenPaletteOk = true";
      break;
    }
  }

  // Serialize the final content and skip the write when nothing changed:
  // state-settings.toml lives inside the inotify-watched config dir, so
  // rewriting identical bytes on every palette pass re-triggers config
  // reloads (supervisor + all children) in an endless feedback loop.
  std::string body;
  body.reserve(settings_lines.size() * 32);
  for (const auto& l : settings_lines) {
    body += l;
    body += '\n';
  }
  {
    std::ifstream cur(settings_path, std::ios::binary);
    if (cur) {
      const std::string existing((std::istreambuf_iterator<char>(cur)),
                                 std::istreambuf_iterator<char>());
      if (existing == body) return;
    }
  }

  std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
  if (!out) return;
  for (const auto& l : settings_lines) out << l << '\n';
  std::fprintf(stderr, "[eh-hc-tpl] horizon-files: synced %d color fields to %s\n",
               replaced / 3, settings_path.string().c_str());
}

// Horizon Photo sync.
// Reads horizon-photo-matugen.conf and writes float RGB to Horizon Photo's
// runtime color file so it can load wallpaper-derived M3 theme on startup.

void sync_horizon_photo(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-photo-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) return;
  std::ifstream min(matugen_file);
  if (!min) return;

  std::unordered_map<std::string, std::string> hex_map;
  std::string ln;
  while (std::getline(min, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    auto eq = ln.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = ln.substr(0, eq);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    std::string val = (eq + 1 < ln.size()) ? ln.substr(eq + 1) : "";
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    if (!val.empty()) hex_map[key] = val;
  }
  if (hex_map.empty()) return;

  struct ColorMapping { const char* hex_key; const char* r_key, *g_key, *b_key; };
  static const ColorMapping mappings[] = {
    {"surface",                "surface_r",                "surface_g",                "surface_b"},
    {"surface_container",      "surface_container_r",      "surface_container_g",      "surface_container_b"},
    {"surface_container_high", "surface_container_high_r", "surface_container_high_g", "surface_container_high_b"},
    {"on_surface",             "on_surface_r",             "on_surface_g",             "on_surface_b"},
    {"on_surface_variant",     "on_surface_variant_r",     "on_surface_variant_g",     "on_surface_variant_b"},
    {"primary",                "primary_r",                "primary_g",                "primary_b"},
    {"primary_container",      "primary_container_r",      "primary_container_g",      "primary_container_b"},
    {"on_primary_container",   "on_primary_container_r",   "on_primary_container_g",   "on_primary_container_b"},
    {"outline",                "outline_r",                "outline_g",                "outline_b"},
    {"outline_variant",        "outline_variant_r",        "outline_variant_g",        "outline_variant_b"},
    {"tonal_container",        "tonal_container_r",        "tonal_container_g",        "tonal_container_b"},
  };

  // Write float RGB to Horizon Photo's runtime color file
  const fs::path photo_dir = getenv_str("HOME") + "/.local/state/event-horizon/Horizon-photo";
  fs::create_directories(photo_dir, ec);
  const fs::path color_path = photo_dir / "theme-colors.toml";
  std::ofstream out(color_path, std::ios::binary | std::ios::trunc);
  if (!out) return;

  out << "# Auto-generated by Event Horizon color engine\n";
  out << "# Wallpaper-derived M3 theme colors (float RGB 0.0-1.0)\n\n";
  for (const auto& m : mappings) {
    auto it = hex_map.find(m.hex_key);
    if (it == hex_map.end()) continue;
    double r, g, b;
    if (!parse_hex_rgb(it->second, r, g, b)) continue;
    out << m.r_key << " = " << fmt_float(r) << "\n";
    out << m.g_key << " = " << fmt_float(g) << "\n";
    out << m.b_key << " = " << fmt_float(b) << "\n\n";
  }
  std::fprintf(stderr, "[eh-hc-tpl] horizon-photo: wrote M3 theme colors to %s\n",
               color_path.string().c_str());
}

void sync_horizon_calendar(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-calendar-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) return;
  std::ifstream min(matugen_file);
  if (!min) return;

  std::vector<std::pair<std::string, std::string>> entries;
  std::string ln;
  while (std::getline(min, ln)) {
    if (!ln.empty() && ln.back() == '\r') ln.pop_back();
    auto eq = ln.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = ln.substr(0, eq);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    if (key.empty() || key.front() == '#') continue;
    std::string val = (eq + 1 < ln.size()) ? ln.substr(eq + 1) : "";
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(0, 1);
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    if (!val.empty()) entries.emplace_back(std::move(key), std::move(val));
  }
  if (entries.empty()) return;

  const fs::path settings_path = fs::path(cfg_main) / "event-horizon" / "state-settings.toml";
  std::string section = "dark";
  if (fs::is_regular_file(settings_path, ec)) {
    std::ifstream sin(settings_path);
    bool in_appearance = false;
    while (std::getline(sin, ln)) {
      if (!ln.empty() && ln.back() == '\r') ln.pop_back();
      const auto t = ln.find_first_not_of(" \t");
      if (t == std::string::npos) continue;
      if (ln[t] == '[') {
        in_appearance = ln.compare(t, 12, "[appearance]") == 0;
        continue;
      }
      if (!in_appearance) continue;
      auto eq = ln.find('=');
      if (eq == std::string::npos || eq == 0) continue;
      std::string key = ln.substr(0, eq);
      while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
      if (key != "matugen_mode") continue;
      std::string mode = ln.substr(eq + 1);
      while (!mode.empty() && (mode.front() == ' ' || mode.front() == '\t')) mode.erase(0, 1);
      if (mode.size() >= 2 && mode.front() == '"' && mode.back() == '"') mode = mode.substr(1, mode.size() - 2);
      section = (mode == "light") ? "light" : "dark";
      break;
    }
  }

  std::string body;
  body.reserve(entries.size() * 32 + 128);
  body += "# Horizon Calendar theme\n";
  body += "# Auto-generated by Event Horizon from the wallpaper palette;\n";
  body += "# manual edits are overwritten on the next palette pass.\n\n[";
  body += section;
  body += "]\n";
  for (const auto& [key, val] : entries) {
    body += key;
    body += " = \"";
    body += val;
    body += "\"\n";
  }

  const fs::path theme_path = fs::path(cfg_main) / "horizon-calendar" / "theme.toml";
  {
    std::ifstream cur(theme_path, std::ios::binary);
    if (cur) {
      const std::string existing((std::istreambuf_iterator<char>(cur)), std::istreambuf_iterator<char>());
      if (existing == body) return;
    }
  }
  fs::create_directories(theme_path.parent_path(), ec);
  std::ofstream out(theme_path, std::ios::binary | std::ios::trunc);
  if (!out) return;
  out << body;
  std::fprintf(stderr, "[eh-hc-tpl] horizon-calendar: wrote %zu colors to %s [%s]\n", entries.size(),
               theme_path.string().c_str(), section.c_str());
}

void sync_alacritty(const std::string& cfg_main) {
  try {
    const fs::path conf = fs::path(cfg_main) / "alacritty" / "alacritty.toml";
    const bool existed = fs::is_regular_file(conf);
    std::vector<std::string> lines;
    if (existed) {
      std::ifstream in(conf);
      std::string ln;
      while (std::getline(in, ln)) {
        if (!ln.empty() && ln.back() == '\r') ln.pop_back();
        lines.push_back(ln);
        if (ln.find("event-theme.toml") != std::string::npos) {
          std::fprintf(stderr, "[eh-hc-tpl] alacritty sync: already imported, nothing to do\n");
          return;
        }
      }
    }
    const std::string general = "[general]";
    bool patched = false;
    size_t hdr = std::string::npos;
    for (size_t i = 0; i < lines.size(); ++i) {
      std::string t = lines[i];
      while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
      if (t == general) { hdr = i; break; }
    }
    if (hdr != std::string::npos) {
      std::string indent;
      {
        size_t k = 0;
        while (k < lines[hdr].size() && (lines[hdr][k] == ' ' || lines[hdr][k] == '\t')) ++k;
        indent = lines[hdr].substr(0, k);
      }
      size_t block_end = lines.size();
      for (size_t i = hdr + 1; i < lines.size(); ++i) {
        std::string t = lines[i];
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
        if (!t.empty() && t[0] == '[') { block_end = i; break; }
      }
      bool found_import = false;
      for (size_t i = hdr + 1; i < block_end; ++i) {
        std::string t = lines[i];
        const size_t lead = t.find_first_not_of(" \t");
        const std::string tt = (lead == std::string::npos) ? "" : t.substr(lead);
        if (tt.rfind("import", 0) != 0) continue;
        if (tt.size() <= 7 || (tt[6] != '=' && tt[6] != ' ')) continue;
        found_import = true;
        const size_t lb = t.find('[');
        if (lb == std::string::npos) break;
        std::string trim_end = tt;
        while (!trim_end.empty() && (trim_end.back() == ' ' || trim_end.back() == '\t')) trim_end.pop_back();
        if (trim_end.back() == ']') {
          const size_t last = t.find_last_of(']');
          std::string inner = t.substr(lb + 1, last - lb - 1);
          const size_t fb = inner.find_first_not_of(" \t");
          const size_t fe = inner.find_last_not_of(" \t");
          if (fb == std::string::npos || fe == std::string::npos || fe < fb) {
            const size_t lead = t.find_first_not_of(" \t");
            lines[i] = ((lead == std::string::npos) ? "" : t.substr(0, lead)) + "import = [\"event-theme.toml\"]";
          } else {
            lines[i] = t.substr(0, last) + ", \"event-theme.toml\"" + t.substr(last);
          }
          patched = true;
        } else if (t.rfind(']') == std::string::npos) {
          for (size_t j = i + 1; j < block_end; ++j) {
            std::string ct = lines[j];
            const size_t cl = ct.find_first_not_of(" \t");
            const std::string ctt = (cl == std::string::npos) ? "" : ct.substr(cl);
            if (!ctt.empty() && ctt[0] == ']') {
              std::string elem_indent = "  ";
              if (cl != std::string::npos && cl > 0) elem_indent = ct.substr(0, cl);
              lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(j), elem_indent + "\"event-theme.toml\",");
              patched = true;
              break;
            }
          }
        }
        break;
      }
      if (!found_import) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(hdr) + 1,
                     indent + "import = [\"event-theme.toml\"]");
        patched = true;
      }
    } else {
      if (!lines.empty() && !lines.back().empty()) lines.push_back("");
      lines.push_back(general);
      lines.push_back("import = [\"event-theme.toml\"]");
      patched = true;
    }
    if (!patched) return;
    std::error_code ec;
    fs::create_directories(conf.parent_path(), ec);
    std::ofstream out(conf, std::ios::binary | std::ios::trunc);
    if (!out) return;
    bool first = true;
    for (const auto& l : lines) {
      if (!first) out << '\n';
      out << l;
      first = false;
    }
    out << '\n';
    std::fprintf(stderr, "[eh-hc-tpl] alacritty sync: %s\n",
                 existed ? "added import to existing config" : "created config with event-horizon import");
  } catch (...) {}
}

void merge_heroic_prefs(const fs::path& heroic_dir) {
  try {
    const fs::path store_path = heroic_dir / "store" / "config.json";
    if (fs::is_regular_file(store_path)) {
      std::ifstream in(store_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      nlohmann::json root = nlohmann::json::object();
      try { root = nlohmann::json::parse(raw, nullptr, true, true, true); } catch (...) {}
      if (root.is_object() && (!root.contains("theme") || root["theme"] != "event-horizon")) {
        root["theme"] = "event-horizon";
        std::ofstream out(store_path, std::ios::binary | std::ios::trunc);
        if (out) out << root.dump(2) << '\n';
      }
    }
    const fs::path cfg_path = heroic_dir / "config.json";
    if (fs::is_regular_file(cfg_path)) {
      std::ifstream in(cfg_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      nlohmann::json root = nlohmann::json::object();
      try { root = nlohmann::json::parse(raw, nullptr, true, true, true); } catch (...) {}
      if (root.is_object() && root.contains("defaultSettings") && root["defaultSettings"].is_object()) {
        auto& ds = root["defaultSettings"];
        auto& p = ds["customThemesPath"];
        if (!p.is_string() || p.get<std::string>().empty()) {
          p = (heroic_dir / "themes").string();
          std::ofstream out(cfg_path, std::ios::binary | std::ios::trunc);
          if (out) out << root.dump(2) << '\n';
        }
      }
    }
  } catch (...) {}
}

void merge_vscode_user_settings(const fs::path& user_dir, const fs::path& material_json,
                                bool merge_material, bool set_workbench_theme, const std::string& mode_norm) {
  try {
    const fs::path settings_path = user_dir / "settings.json";
    nlohmann::json root = nlohmann::json::object();
    if (fs::is_regular_file(settings_path)) {
      std::ifstream in(settings_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try { root = nlohmann::json::parse(raw, nullptr, true, true, true); } catch (...) {}
    }
    if (merge_material && fs::is_regular_file(material_json)) {
      std::ifstream min(material_json);
      const nlohmann::json mat = nlohmann::json::parse(min, nullptr, true, true, true);
      if (mat.is_object() && mat.contains("material-code.colors") && mat["material-code.colors"].is_object()) {
        root["material-code.colors"] = mat["material-code.colors"];
      }
    }
    if (set_workbench_theme) {
      const char* tname = "Event Horizon Dark";
      if (mode_norm == "light") tname = "Event Horizon Light";
      root["workbench.colorTheme"] = tname;
    }
    std::error_code ec;
    fs::create_directories(user_dir, ec);
    std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << root.dump(2) << '\n';
  } catch (...) {}
}

} // anonymous namespace

// Public API.

void apply_native_templates(const eh::config::ShellConfig& config) {
  const auto& ap = config.appearance;
  std::fprintf(stderr, "[eh-hc-tpl] apply_native_templates: hcNative=%d paletteOk=%d matugenEnabled=%d matugenOk=%d\n",
               ap.horizonColorsNative ? 1 : 0, ap.horizonColorsPaletteOk ? 1 : 0,
               ap.matugenThemingEnabled ? 1 : 0, ap.matugenPaletteOk ? 1 : 0);
  if (!ap.horizonColorsNative || !ap.horizonColorsPaletteOk) {
    std::fprintf(stderr, "[eh-hc-tpl] EXIT EARLY: not active (native=%d paletteOk=%d)\n",
                 ap.horizonColorsNative ? 1 : 0, ap.horizonColorsPaletteOk ? 1 : 0);
    return;
  }

  const auto root = bundle_root();
  if (!root) {
    std::fprintf(stderr, "[eh-hc-tpl] EXIT EARLY: no bundle_root found\n");
    return;
  }
  std::fprintf(stderr, "[eh-hc-tpl] bundle_root=%s\n", root->string().c_str());

  const std::string cfg_main = config_dir();
  const std::string home = getenv_str("HOME");
  if (home.empty()) return;

  const std::string vscode_ud = vscode_user_dir_for_hc(cfg_main);
  const fs::path vscode_ext_eh_dir = vscode_marketplace_extensions_dir(home, vscode_ud) / "eh-matugen-themes";
  const auto& T = ap.matugenOutputs;
  const bool want_vscode = (T.vscodeMaterial || T.vscodeColorThemes) && vscode_outputs_ok(cfg_main);
  if (want_vscode) {
    ensure_vscode_eh_theme_extension(*root, vscode_ext_eh_dir);
    std::error_code ec_mk;
    fs::create_directories(fs::path(vscode_ud), ec_mk);
  }

  // Merge TOML configs (same logic as matugen_external_templates)
  const fs::path cfgd = *root / "matugen" / "configs";
  std::ostringstream merged;
  if (T.runBundledToml) {
    const fs::path base = cfgd / "base.toml";
    if (!append_file(merged, base)) return;
  } else {
    merged << "[config]\n\n[templates]\n\n";
  }

  auto maybe_append = [&](bool on, const char* fname, auto&& pred) {
    if (!on) return;
    if (!pred()) return;
    const fs::path fp = cfgd / fname;
    append_file(merged, fp);
  };

  maybe_append(T.niri, "niri.toml", [&] { return exe_on_path("niri"); });
  maybe_append(T.hyprland, "hyprland.toml", [&] { return std::getenv("HYPRLAND_INSTANCE_SIGNATURE") != nullptr; });
  maybe_append(T.mango, "mangowc.toml", [&] { return exe_on_path("mmsg"); });
  maybe_append(T.gtkShellCss, "gtk.toml", [&] { return true; });
  maybe_append(T.gtkEventColorsLight, "gtk3-light.toml", [&] { return true; });
  maybe_append(T.gtkEventColorsDark, "gtk3-dark.toml", [&] { return true; });
  maybe_append(T.kcolorscheme, "kcolorscheme.toml", [&] { return true; });
  maybe_append(T.qt5ct, "qt5ct.toml", [&] { return exe_on_path("qt5ct"); });
  maybe_append(T.qt6ct, "qt6ct.toml", [&] { return exe_on_path("qt6ct"); });
  maybe_append(T.kittyTheme, "kitty.toml", [&] { return exe_on_path("kitty"); });
  maybe_append(T.kittyTabs, "kitty-tabs.toml", [&] { return exe_on_path("kitty"); });
  maybe_append(T.ghostty, "ghostty.toml", [&] { return exe_on_path("ghostty"); });
  maybe_append(T.wezterm, "wezterm.toml", [&] { return exe_on_path("wezterm"); });
  maybe_append(T.alacritty, "alacritty.toml", [&] { return exe_on_path("alacritty"); });
  maybe_append(T.foot, "foot.toml", [&] { return exe_on_path("foot"); });
  maybe_append(T.otterTerm, "otter-term.toml", [&] { return true; });
  maybe_append(T.btop, "btop.toml", [&] { return exe_on_path("btop"); });
  maybe_append(T.neovim, "neovim.toml", [&] { return exe_on_path("nvim"); });
  maybe_append(T.vscodeMaterial, "vscode-material.toml", [&] { return vscode_outputs_ok(cfg_main); });
  maybe_append(T.vscodeColorThemes, "vscode-color-themes.toml", [&] { return vscode_outputs_ok(cfg_main); });
  maybe_append(T.firefox, "firefox.toml", [&] { return exe_on_path("firefox"); });
  maybe_append(T.pywalfox, "pywalfox.toml", [&] { return exe_on_path("pywalfox"); });
  maybe_append(T.zenbrowser, "zenbrowser.toml", [&] { return fs::exists(fs::path(home) / ".zen"); });
  maybe_append(T.vesktop, "vesktop.toml", [&] {
    const bool exe = exe_on_path("vesktop");
    const bool dir = fs::exists(fs::path(cfg_main) / "vesktop");
    std::fprintf(stderr, "[eh-hc-tpl] vesktop gate: toggle=%d exe=%d dir=%d\n",
                 T.vesktop ? 1 : 0, exe ? 1 : 0, dir ? 1 : 0);
    return exe && dir;
  });
  maybe_append(T.equibop, "equibop.toml", [&] { return fs::exists(fs::path(cfg_main) / "equibop"); });
  maybe_append(T.zed, "zed.toml", [&] { return fs::exists(fs::path(cfg_main) / "zed"); });
  maybe_append(T.steam, "steam.toml", [&] {
    return fs::exists(fs::path(home) / ".steam") || fs::exists(fs::path(home) / ".local/share/Steam");
  });
  maybe_append(T.emacs, "emacs.toml", [&] { return exe_on_path("emacs"); });
  maybe_append(T.dgop, "dgop.toml", [&] { return exe_on_path("dgop"); });
  maybe_append(T.ptyxis, "ptyxis.toml", [&] { return exe_on_path("ptyxis"); });
  maybe_append(T.obs, "obs.toml", [&] {
    return exe_on_path("obs") || exe_on_path("obs-studio") ||
           fs::exists(fs::path(home) / ".var/app/com.obsproject.Studio/config/obs-studio");
  });
  maybe_append(T.heroic, "heroic.toml", [&] {
    return exe_on_path("heroic") || exe_on_path("com.heroicgameslauncher.hgl") ||
           fs::exists(fs::path(home) / ".config/heroic");
  });
  maybe_append(T.horizonFiles, "horizon-files.toml", [&] {
    return fs::exists(fs::path(cfg_main) / "event-horizon");
  });
  maybe_append(T.horizonPhoto, "horizon-photo.toml", [&] {
    return fs::exists(fs::path(cfg_main) / "event-horizon");
  });
  maybe_append(T.horizonCalendar, "horizon-calendar.toml", [&] {
    return fs::exists(fs::path(cfg_main) / "horizon-calendar");
  });

  std::string body = merged.str();
  fix_paths(body, *root, cfg_main, home, vscode_ud, vscode_ext_eh_dir.string());
  std::fprintf(stderr, "[eh-hc-tpl] merged TOML size=%zu contains vesktop=%d\n",
               body.size(),
               body.find("[templates.vesktop]") != std::string::npos ? 1 : 0);

  // Generate palette from wallpaper
  const std::string wp = eh::config::normalize_wallpaper_path_for_matugen(config.wallpaperImage);
  if (wp.empty() || ::access(wp.c_str(), R_OK) != 0) {
    std::fprintf(stderr, "[eh-hc-tpl] EXIT EARLY: wallpaper not accessible '%s'\n", wp.c_str());
    return;
  }
  const std::string scheme = eh::matugen::normalize_matugen_scheme(ap.matugenScheme);
  const std::string modeNorm = eh::matugen::normalize_matugen_mode(ap.matugenMode);
  std::fprintf(stderr, "[eh-hc-tpl] wallpaper='%s' scheme='%s' mode='%s'\n",
               wp.c_str(), scheme.c_str(), modeNorm.c_str());
  const bool is_dark = (modeNorm != "light");
  const eh::color::PaletteResult palette =
      eh::color::generate_palette_from_image_cached(wp, eh::color::scheme_variant_from_name(scheme), is_dark);
  if (!palette.ok) {
    std::fprintf(stderr, "[eh-hc-tpl] EXIT EARLY: palette generation failed\n");
    return;
  }
  std::fprintf(stderr, "[eh-hc-tpl] palette OK: sourceColor=0x%08x roles=%zu\n",
               palette.sourceColorArgb, palette.roles.size());

  // Parse template entries from merged TOML
  auto entries = parse_template_entries(body);
  std::fprintf(stderr, "[eh-hc-tpl] parsed %zu template entries\n", entries.size());
  for (const auto& e : entries) {
    std::fprintf(stderr, "[eh-hc-tpl]   entry: name=%s input=%s output=%s\n",
                 e.name.c_str(), e.input_path.c_str(), e.output_path.c_str());
  }

  // Build role map
  auto roles = palette.roles;
  auto base16 = generate_base16(roles, is_dark);

  // Process each template
  int processed = 0;
  for (const auto& entry : entries) {
    const fs::path input(entry.input_path);
    const fs::path output(entry.output_path);

    if (!fs::is_regular_file(input)) {
      std::fprintf(stderr, "[eh-hc-tpl] SKIP %s: input not found '%s'\n",
                   entry.name.c_str(), input.string().c_str());
      continue;
    }

    std::ifstream in(input);
    if (!in) {
      std::fprintf(stderr, "[eh-hc-tpl] SKIP %s: can't open input\n", entry.name.c_str());
      continue;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    process_template(content, roles, base16, palette.sourceColorArgb, is_dark);

    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::fprintf(stderr, "[eh-hc-tpl] FAILED to write %s\n", output.string().c_str());
      continue;
    }
    out << content;
    ++processed;
    std::fprintf(stderr, "[eh-hc-tpl] WROTE %s (%zu bytes)\n", output.string().c_str(), content.size());
  }
  std::fprintf(stderr, "[eh-hc-tpl] processed %d/%zu templates\n", processed, entries.size());

  // Run post-hooks
  std::fprintf(stderr, "[eh-hc-tpl] post-hooks: otter=%d btop=%d vesktop=%d vscode=%d\n",
               T.otterTerm ? 1 : 0, (T.btop && exe_on_path("btop")) ? 1 : 0,
               (T.vesktop && fs::exists(fs::path(cfg_main) / "vesktop")) ? 1 : 0,
               want_vscode ? 1 : 0);
  if (T.otterTerm) sync_otter_term(cfg_main);
  if (T.horizonFiles) sync_horizon_files(cfg_main);
  if (T.horizonPhoto) sync_horizon_photo(cfg_main);
  if (T.horizonCalendar) sync_horizon_calendar(cfg_main);
  if (T.btop && exe_on_path("btop")) sync_btop(cfg_main);
  if (T.vesktop && fs::exists(fs::path(cfg_main) / "vesktop")) {
    std::fprintf(stderr, "[eh-hc-tpl] vesktop post-hook: merge + bump mtime\n");
    merge_vesktop_vencord_settings(fs::path(cfg_main) / "vesktop");
    bump_mtime_now(fs::path(cfg_main) / "vesktop" / "themes" / "midnight.theme.css");
  }
  if (want_vscode) {
    const std::string scheme = eh::matugen::normalize_matugen_scheme(ap.matugenScheme);
    merge_vscode_user_settings(fs::path(vscode_ud),
                               fs::path(vscode_ud) / "material-code-colors.matugen.json",
                               T.vscodeMaterial, T.vscodeColorThemes, modeNorm);
    touch_vscode_theme_extension_outputs(vscode_ext_eh_dir);
    bump_mtime_now(fs::path(vscode_ud) / "settings.json");
  }
  if (T.heroic && fs::exists(fs::path(cfg_main) / "heroic" / "themes" / "event-horizon.css")) {
    std::fprintf(stderr, "[eh-hc-tpl] heroic post-hook: flipping theme selection to event-horizon\n");
    merge_heroic_prefs(fs::path(cfg_main) / "heroic");
  }
  if (T.alacritty && fs::exists(fs::path(cfg_main) / "alacritty" / "event-theme.toml")) {
    sync_alacritty(cfg_main);
  }
}

} // namespace eh::horizon_colors
