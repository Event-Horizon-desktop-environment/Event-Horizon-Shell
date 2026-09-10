#define _GNU_SOURCE 1
#include "desktop_shell/common/palette/matugen_external_templates.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/bench/debug_profile.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <spawn.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <sys/wait.h>
#include <unordered_set>
#include <vector>

extern "C" char** environ;

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string getenv_str(const char* k) {
   
  const char* v = std::getenv(k);
  return v ? std::string(v) : std::string{};
}

[[nodiscard]] bool matugen_trace_enabled() noexcept {
   
  return eh::debug_profile::env_bool("EH_MATUGEN_DEBUG") || eh::debug_profile::env_bool("EH_SETTINGS_DEBUG");
}

void matugen_trace(const char* fmt, ...) {
   
  if (!matugen_trace_enabled()) return;
  va_list ap;
  va_start(ap, fmt);
  std::fputs("[eh-matugen] ", stderr);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
}

[[nodiscard]] std::string config_dir() {
   
  std::string xdg = getenv_str("XDG_CONFIG_HOME");
  if (!xdg.empty()) return xdg;
  std::string h = getenv_str("HOME");
  if (!h.empty()) return h + "/.config";
  return "/tmp";
}

[[nodiscard]] bool matugen_bundle_gtk_layout_is_current(const fs::path& root) {
   
  const fs::path gtk = root / "matugen" / "configs" / "gtk.toml";
  std::error_code ec;
  if (!fs::is_regular_file(gtk, ec)) return true;
  std::ifstream in(gtk);
  if (!in) return true;
  const std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return buf.find("[templates.ehgtk_shell_gtk4]") != std::string::npos;
}

[[nodiscard]] std::optional<fs::path> bundle_near_executable() {
   
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

[[nodiscard]] std::optional<fs::path> bundle_root() {
   
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

void replace_all(std::string& s, const std::string& from, const std::string& to) {
   
  if (from.empty()) return;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
}

void rewrite_legacy_event16(std::string& s) {
   
  if (s.find("event16.color") == std::string::npos) return;
  static constexpr const char* kMap[] = {"base00", "base08", "base0b", "base0a", "base0d", "base0e", "base0c", "base05",
                                         "base03", "base08", "base0b", "base0a", "base0d", "base0e", "base0c", "base07"};
  for (int i = 15; i >= 0; --i) {
    replace_all(s, std::string("event16.color") + std::to_string(i) + ".",
                std::string("base16.") + kMap[static_cast<size_t>(i)] + ".");
  }
}

void normalize_base16_default_variant(std::string& s) {
   
  static constexpr char kHexDigits[] = "0123456789abcdefABCDEF";
  for (size_t h = 0; h + 1 < sizeof(kHexDigits); ++h) {
    const std::string prefix = std::string("base16.base0") + kHexDigits[h] + ".";
    replace_all(s, prefix + "default.", prefix + "dark.");
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

bool append_file(std::ostringstream& out, const fs::path& p) {
   
  std::ifstream in(p);
  if (!in) return false;
  out << in.rdbuf();
  out << '\n';
  return true;
}

[[nodiscard]] bool exe_on_path(const char* exe) {
   
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

constexpr const char* kVscodeConfigDirs[] = {"Code", "Cursor", "VSCodium", "Code - OSS", "Code - Insiders"};

[[nodiscard]] bool vscode_outputs_ok(const std::string& cfg_main) {
   
  if (exe_on_path("code") || exe_on_path("codium") || exe_on_path("cursor")) return true;
  const fs::path c(cfg_main);
  for (const char* d : kVscodeConfigDirs) {
    if (fs::exists(c / d)) return true;
  }
  return false;
}

[[nodiscard]] std::string vscode_user_dir_for_matugen(const std::string& cfg_main) {
   
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

[[nodiscard]] fs::path vscode_marketplace_extensions_dir(const std::string& home, const std::string& user_dir) {
   
  if (user_dir.find("/Cursor/") != std::string::npos) return fs::path(home) / ".cursor" / "extensions";
  if (user_dir.find("/VSCodium/") != std::string::npos) return fs::path(home) / ".vscode-oss" / "extensions";
  if (user_dir.find("/Code - Insiders/") != std::string::npos) return fs::path(home) / ".vscode-insiders" / "extensions";
  if (user_dir.find("/Code - OSS/") != std::string::npos) return fs::path(home) / ".vscode-oss" / "extensions";
  return fs::path(home) / ".vscode" / "extensions";
}

void ensure_vscode_eh_theme_extension(const fs::path& bundle_root, const fs::path& ext_dir) {
   
  std::error_code ec;
  fs::create_directories(ext_dir, ec);
  const fs::path src_pkg = bundle_root / "matugen" / "vscode-eh-themes" / "package.json";
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

void merge_vesktop_vencord_settings_after_matugen(const fs::path& vesktop_data_dir) {
   
  matugen_trace("vesktop merge: Vencord DATA_DIR=%s\n", vesktop_data_dir.string().c_str());
  try {
    const fs::path settings_path = vesktop_data_dir / "settings" / "settings.json";
    const fs::path theme_css = vesktop_data_dir / "themes" / "midnight.theme.css";
    nlohmann::json root = nlohmann::json::object();
    const bool had_settings = fs::is_regular_file(settings_path);
    matugen_trace("vesktop merge: settings.json path=%s exists=%d\n", settings_path.string().c_str(), had_settings ? 1 : 0);
    if (had_settings) {
      std::ifstream in(settings_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try {
        root = nlohmann::json::parse(raw, nullptr, true, true, true);
      } catch (const nlohmann::json::parse_error& e) {
        matugen_trace("vesktop merge: parse error: %s\n", e.what());
        std::fprintf(stderr,
                     "[event-horizon] matugen: %s: JSON parse error (%s) — treating as empty object\n",
                     settings_path.string().c_str(), e.what());
        root = nlohmann::json::object();
      }
    }
    static constexpr const char* kThemeFile = "midnight.theme.css";
    nlohmann::json& et = root["enabledThemes"];
    if (!et.is_array()) {
      matugen_trace("vesktop merge: enabledThemes was not an array; replacing with []\n");
      et = nlohmann::json::array();
    }
    matugen_trace("vesktop merge: enabledThemes before=%s\n", et.dump().c_str());
    bool found = false;
    for (const auto& x : et) {
      if (x.is_string() && x.get<std::string>() == kThemeFile) {
        found = true;
        break;
      }
    }
    if (!found) {
      et.push_back(kThemeFile);
      matugen_trace("vesktop merge: appended %s to enabledThemes\n", kThemeFile);
    } else {
      matugen_trace("vesktop merge: %s already in enabledThemes\n", kThemeFile);
    }
    matugen_trace("vesktop merge: enabledThemes after=%s\n", et.dump().c_str());

    std::error_code ec;
    fs::create_directories(settings_path.parent_path(), ec);
    matugen_trace("vesktop merge: mkdir settings parent ec=%d msg=%s\n", ec.value(), ec.message().c_str());
    std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      matugen_trace("vesktop merge: FAILED open settings for write: %s\n", settings_path.string().c_str());
      return;
    }
    const std::string dumped = root.dump(2);
    out << dumped << '\n';
    matugen_trace("vesktop merge: wrote settings.json bytes=%zu\n", dumped.size());

    std::error_code ec_sz;
    const auto sz = fs::file_size(theme_css, ec_sz);
    matugen_trace("vesktop merge: theme css %s size=%llu err=%d\n", theme_css.string().c_str(),
                  static_cast<unsigned long long>(ec_sz ? 0ULL : sz), ec_sz.value());
  } catch (const std::exception& e) {
    matugen_trace("vesktop merge: exception: %s\n", e.what());
    std::fprintf(stderr, "[event-horizon] matugen: Vesktop settings merge failed: %s\n", e.what());
  }
}

void merge_vscode_user_settings_after_matugen(const fs::path& user_dir, const fs::path& material_json,
                                              bool merge_material, bool set_workbench_theme, const std::string& mode_norm) {
   
  try {
    const fs::path settings_path = user_dir / "settings.json";
    nlohmann::json root = nlohmann::json::object();
    if (fs::is_regular_file(settings_path)) {
      std::ifstream in(settings_path);
      const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      try {
        root = nlohmann::json::parse(raw, nullptr, true, true, true);
      } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr,
                     "[event-horizon] matugen: %s: JSON parse error (%s) — treating as empty object\n",
                     settings_path.string().c_str(), e.what());
        root = nlohmann::json::object();
      }
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
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[event-horizon] matugen: VS Code settings merge failed: %s\n", e.what());
  }
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
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[event-horizon] Heroic prefs merge failed: %s\n", e.what());
  }
}

void merge_fluxer_prefs(const std::string& cfg_main, const std::string& home) {
  try {
    const fs::path master = fs::path(cfg_main) / "fluxer" / "themes" / "event-horizon.css";
    if (!fs::is_regular_file(master)) return;
    const std::array<const char*, 2> kFluxerDirs = {"fluxercanary", "fluxer"};
    for (const char* name : kFluxerDirs) {
      const fs::path user_dir = fs::path(home) / ".config" / name;
      if (!fs::is_directory(user_dir)) continue;
      const fs::path theme_dir = user_dir / "themes";
      std::error_code ec;
      fs::create_directories(theme_dir, ec);
      const fs::path theme_path = theme_dir / "event-horizon.css";
      std::string want;
      {
        std::ifstream in(master);
        want.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      }
      if (want.empty()) continue;
      std::string have;
      if (fs::is_regular_file(theme_path)) {
        std::ifstream in(theme_path);
        have.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      }
      bool changed = false;
      if (have != want) {
        std::ofstream out(theme_path, std::ios::binary | std::ios::trunc);
        if (out) {
          out << want;
          changed = true;
        }
      }
      const fs::path settings_path = user_dir / "settings.json";
      nlohmann::json root = nlohmann::json::object();
      if (fs::is_regular_file(settings_path)) {
        std::ifstream in(settings_path);
        const std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        try {
          root = nlohmann::json::parse(raw, nullptr, true, true, true);
        } catch (...) {}
      }
      const std::string entry = theme_path.string();
      bool present = false;
      if (root.is_object() && root.contains("theme_allowed_local_files") && root["theme_allowed_local_files"].is_array()) {
        for (const auto& s : root["theme_allowed_local_files"]) {
          if (s.is_string() && s.get<std::string>() == entry) {
            present = true;
            break;
          }
        }
      }
      if (!present) {
        if (!root.is_object()) root = nlohmann::json::object();
        nlohmann::json& arr = root["theme_allowed_local_files"];
        if (!arr.is_array()) arr = nlohmann::json::array();
        arr.push_back(entry);
        std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
        if (out) {
          out << root.dump(2) << '\n';
          changed = true;
        }
      }
      if (changed) {
        matugen_trace("fluxer post: seeded themes + allowlist in %s\n", user_dir.c_str());
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[event-horizon] Fluxer prefs merge failed: %s\n", e.what());
  }
}

void sync_alacritty_after_matugen(const std::string& cfg_main) {
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
          matugen_trace("alacritty sync: already imported, nothing to do\n");
          return;
        }
      }
    }
    const std::string entry = "event-theme.toml";
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
        const size_t lb = t.find('['), rb = t.rfind(']');
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
        } else if (rb == std::string::npos) {
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
    matugen_trace("alacritty sync: %s\n",
                  existed ? "added import to existing config" : "created config with event-horizon import");
  } catch (const std::exception& e) {
    std::fprintf(stderr, "[event-horizon] Alacritty sync failed: %s\n", e.what());
  }
}

void sync_otter_term_after_matugen(const std::string& cfg_main, const std::string&) {
   
  const fs::path matugen_file = fs::path(cfg_main) / "otter-shell" / "otter-term-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) {
    matugen_trace("otter-term sync: matugen file not found %s\n", matugen_file.string().c_str());
    return;
  }
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
  if (matugen_keys.empty()) {
    matugen_trace("otter-term sync: no color entries found\n");
    return;
  }
  matugen_trace("otter-term sync: loaded %zu color entries from %s\n", matugen_keys.size(), matugen_file.string().c_str());
  const fs::path conf_path = fs::path(cfg_main) / "otter-shell" / "otter-term.conf";
  if (!fs::is_regular_file(conf_path, ec)) {
    matugen_trace("otter-term sync: config not found %s\n", conf_path.string().c_str());
    return;
  }
  std::ifstream in(conf_path);
  std::vector<std::string> lines;
  int replaced = 0;
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
            ++replaced;
            break;
          }
        }
      }
    }
    lines.push_back(std::move(ln));
  }
  matugen_trace("otter-term sync: replaced %d color lines\n", replaced);
  if (replaced == 0) return;
  std::ofstream out(conf_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    matugen_trace("otter-term sync: failed to write %s\n", conf_path.string().c_str());
    return;
  }
  for (const auto& l : lines) {
    out << l << '\n';
  }
  matugen_trace("otter-term sync: written %zu lines to %s\n", lines.size(), conf_path.string().c_str());
}

static bool parse_hex_rgb_external(const std::string& hex, double& r, double& g, double& b) {
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

static std::string fmt_float_external(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6f", v);
  return buf;
}

void sync_horizon_files_after_matugen(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-files-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) {
    matugen_trace("horizon-files sync: matugen file not found %s\n", matugen_file.string().c_str());
    return;
  }
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
  if (hex_map.empty()) {
    matugen_trace("horizon-files sync: no color entries found\n");
    return;
  }

  struct ColorMapping { const char* hex_key; const char* r_key, *g_key, *b_key; };
  static const ColorMapping mappings[] = {
    {"dock_fill",              "matugenDockFillR",              "matugenDockFillG",              "matugenDockFillB"},
    {"panel_fill",             "matugenPanelFillR",             "matugenPanelFillG",             "matugenPanelFillB"},
    {"drawer_dim",             "matugenDrawerDimR",             "matugenDrawerDimG",             "matugenDrawerDimB"},
    {"outline",                "matugenOutlineR",               "matugenOutlineG",               "matugenOutlineB"},
    {"accent",                 "matugenAccentR",                "matugenAccentG",                "matugenAccentB"},
    {"text",                   "matugenTextR",                  "matugenTextG",                  "matugenTextB"},
    {"notif_critical_bg",      "matugenNotifCriticalBgR",       "matugenNotifCriticalBgG",       "matugenNotifCriticalBgB"},
    {"notif_critical_outline", "matugenNotifCriticalOutlineR",  "matugenNotifCriticalOutlineG",  "matugenNotifCriticalOutlineB"},
  };

  const fs::path settings_path = fs::path(cfg_main) / "event-horizon" / "state-settings.toml";
  std::vector<std::string> settings_lines;
  if (fs::is_regular_file(settings_path, ec)) {
    std::ifstream sin(settings_path);
    while (std::getline(sin, ln)) {
      if (!ln.empty() && ln.back() == '\r') ln.pop_back();
      settings_lines.push_back(std::move(ln));
    }
  }

  std::unordered_map<std::string, std::string> replacements;
  for (const auto& m : mappings) {
    auto it = hex_map.find(m.hex_key);
    if (it == hex_map.end()) continue;
    double r, g, b;
    if (!parse_hex_rgb_external(it->second, r, g, b)) continue;
    replacements[m.r_key] = fmt_float_external(r);
    replacements[m.g_key] = fmt_float_external(g);
    replacements[m.b_key] = fmt_float_external(b);
  }
  if (replacements.empty()) return;

  // Patch [appearance]: update keys in place, drop misplaced occurrences that
  // earlier versions appended past the section (they used to land in whatever
  // table closed the file, e.g. [dock]), and insert missing keys right after
  // the [appearance] header — never at EOF.
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

  for (auto& line : settings_lines) {
    if (line.find("matugenPaletteOk") != std::string::npos) {
      line = "matugenPaletteOk = true";
      break;
    }
  }

  // Serialize and skip the write when unchanged — see the matching guard in
  // horizon_colors_templates.cpp: rewriting identical bytes into the watched
  // config dir re-triggers config reloads in an endless inotify loop.
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
      if (existing == body) {
        matugen_trace("horizon-files sync: unchanged; skipped %s\n", settings_path.string().c_str());
        return;
      }
    }
  }

  std::ofstream out(settings_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    matugen_trace("horizon-files sync: failed to write %s\n", settings_path.string().c_str());
    return;
  }
  for (const auto& l : settings_lines) out << l << '\n';
  matugen_trace("horizon-files sync: written %zu lines to %s\n", settings_lines.size(), settings_path.string().c_str());
}

void sync_horizon_photo_after_matugen(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-photo-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) {
    matugen_trace("horizon-photo sync: matugen file not found %s\n", matugen_file.string().c_str());
    return;
  }
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
  if (hex_map.empty()) {
    matugen_trace("horizon-photo sync: no color entries found\n");
    return;
  }

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

  const char* home = std::getenv("HOME");
  if (!home || !home[0]) return;
  const fs::path photo_dir = fs::path(home) / ".local" / "state" / "event-horizon" / "Horizon-photo";
  fs::create_directories(photo_dir, ec);
  const fs::path color_path = photo_dir / "theme-colors.toml";
  std::ofstream out(color_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    matugen_trace("horizon-photo sync: failed to write %s\n", color_path.string().c_str());
    return;
  }

  out << "# Auto-generated by Event Horizon color engine\n";
  out << "# Wallpaper-derived M3 theme colors (float RGB 0.0-1.0)\n\n";
  for (const auto& m : mappings) {
    auto it = hex_map.find(m.hex_key);
    if (it == hex_map.end()) continue;
    double r, g, b;
    if (!parse_hex_rgb_external(it->second, r, g, b)) continue;
    out << m.r_key << " = " << fmt_float_external(r) << "\n";
    out << m.g_key << " = " << fmt_float_external(g) << "\n";
    out << m.b_key << " = " << fmt_float_external(b) << "\n\n";
  }
  matugen_trace("horizon-photo sync: written theme colors to %s\n", color_path.string().c_str());
}

void sync_horizon_calendar_after_matugen(const std::string& cfg_main) {
  const fs::path matugen_file = fs::path(cfg_main) / "event-horizon" / "horizon-calendar-matugen.conf";
  std::error_code ec;
  if (!fs::is_regular_file(matugen_file, ec)) {
    matugen_trace("horizon-calendar sync: matugen file not found %s\n", matugen_file.string().c_str());
    return;
  }
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
  if (entries.empty()) {
    matugen_trace("horizon-calendar sync: no color entries found\n");
    return;
  }

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
      if (existing == body) {
        matugen_trace("horizon-calendar sync: unchanged; skipped %s\n", theme_path.string().c_str());
        return;
      }
    }
  }
  fs::create_directories(theme_path.parent_path(), ec);
  std::ofstream out(theme_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    matugen_trace("horizon-calendar sync: failed to write %s\n", theme_path.string().c_str());
    return;
  }
  out << body;
  matugen_trace("horizon-calendar sync: wrote %zu colors to %s [%s]\n", entries.size(),
                theme_path.string().c_str(), section.c_str());
}

void sync_btop_after_matugen(const std::string& cfg_main) {
   
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
    for (size_t i = 0; i < lines.size(); ++i) {
      out << lines[i] << '\n';
    }
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

[[nodiscard]] bool template_tree_has_legacy_event16(const fs::path& tpl_root) {
   
  std::error_code ec;
  if (!fs::is_directory(tpl_root, ec)) return false;
  ec.clear();
  for (fs::recursive_directory_iterator it(tpl_root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file()) continue;
    std::ifstream fin(it->path());
    if (!fin) continue;
    std::string chunk((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
    if (chunk.find("event16.color") != std::string::npos) return true;
  }
  return false;
}

[[nodiscard]] bool copy_templates_sanitized(const fs::path& tpl_src, const fs::path& dst_root) {
   
  std::error_code ec;
  fs::create_directories(dst_root, ec);
  ec.clear();
  for (fs::recursive_directory_iterator it(tpl_src, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) return false;
    const fs::path& p = it->path();
    fs::path rel = fs::relative(p, tpl_src, ec);
    if (ec) continue;
    if (it->is_directory()) {
      fs::create_directories(dst_root / rel, ec);
    } else if (it->is_regular_file()) {
      fs::path out_path = dst_root / rel;
      fs::create_directories(out_path.parent_path(), ec);
      std::ifstream fin(p);
      if (!fin) return false;
      std::string content((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
      rewrite_legacy_event16(content);
      normalize_base16_default_variant(content);
      std::ofstream fout(out_path);
      if (!fout) return false;
      fout << content;
    }
  }
  return true;
}

[[nodiscard]] bool run_matugen_image(const fs::path& cwd, const fs::path& cfg_file, const std::string& image_abs,
                                     const char* mode_arg, const std::string& type_arg) {
   
  matugen_trace("spawn: cwd=%s cfg=%s image=%s -m %s -t %s\n", cwd.string().c_str(), cfg_file.string().c_str(),
                image_abs.c_str(), mode_arg, type_arg.c_str());

  posix_spawn_file_actions_t fa{};
  if (posix_spawn_file_actions_init(&fa) != 0) {
    matugen_trace("posix_spawn_file_actions_init failed errno=%d\n", errno);
    return false;
  }
  if (!cwd.empty() && posix_spawn_file_actions_addchdir_np(&fa, cwd.string().c_str()) != 0) {
    matugen_trace("posix_spawn addchdir failed errno=%d\n", errno);
    posix_spawn_file_actions_destroy(&fa);
    return false;
  }
  if (posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0) {
    posix_spawn_file_actions_destroy(&fa);
    return false;
  }

  const std::string cfg_s = cfg_file.string();
  std::vector<std::string> parts = {"matugen",       "-c",          cfg_s,           "image",
                                      image_abs,     "--source-color-index", "0", "-m",
                                      std::string(mode_arg), "-t", type_arg};
  std::vector<std::vector<char>> storage;
  storage.reserve(parts.size());
  std::vector<char*> argv;
  argv.reserve(parts.size() + 1);
  for (auto& p : parts) {
    storage.emplace_back(p.begin(), p.end());
    storage.back().push_back('\0');
    argv.push_back(storage.back().data());
  }
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int spawn_err = posix_spawnp(&pid, "matugen", &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  if (spawn_err != 0 || pid <= 0) {
    matugen_trace("posix_spawnp(matugen) failed err=%d errno=%d\n", spawn_err, errno);
    return false;
  }

  int st = 0;
  (void)waitpid(pid, &st, 0);
  if (WIFEXITED(st)) {
    const int code = WEXITSTATUS(st);
    if (code != 0) matugen_trace("matugen exited status=%d (see stderr above if matugen printed)\n", code);
    else matugen_trace("matugen exited status=0\n");
    return code == 0;
  }
  if (WIFSIGNALED(st)) matugen_trace("matugen killed by signal %d\n", WTERMSIG(st));
  else matugen_trace("matugen waitpid ended oddly st=%d\n", st);
  return false;
}

}

namespace eh::matugen {

bool matugen_external_bundle_available() {   return bundle_root().has_value(); }

void apply_external_matugen_templates(const eh::config::ShellConfig& config) {
   
  const auto& ap = config.appearance;
  matugen_trace("apply_external_matugen_templates: enter matugenThemingEnabled=%d matugenPaletteOk=%d\n",
                ap.matugenThemingEnabled ? 1 : 0, ap.matugenPaletteOk ? 1 : 0);
  if (!ap.matugenThemingEnabled || !ap.matugenPaletteOk) {
    matugen_trace("exit early: matugen off or palette not ok\n");
    return;
  }
  const auto root = bundle_root();
  if (!root) {
    matugen_trace("exit early: bundle_root() null (check EH_MATUGEN_ROOT, install, gtk.toml marker)\n");
    return;
  }
  matugen_trace("bundle_root=%s\n", root->string().c_str());
  if (!exe_on_path("matugen")) {
    matugen_trace("exit early: matugen not on PATH\n");
    return;
  }

  const std::string wp = eh::config::normalize_wallpaper_path_for_matugen(config.wallpaperImage);
  if (wp.empty()) {
    matugen_trace("exit early: wallpaper path empty after normalize\n");
    return;
  }
  if (::access(wp.c_str(), R_OK) != 0) {
    matugen_trace("exit early: wallpaper not readable errno=%d path=%s\n", errno, wp.c_str());
    return;
  }
  matugen_trace("wallpaper image=%s\n", wp.c_str());

  const std::string cfg_main = config_dir();
  const std::string home = getenv_str("HOME");
  matugen_trace("config_dir (XDG)=%s HOME=%s EH_MATUGEN_ROOT=%s\n", cfg_main.c_str(), home.c_str(),
                getenv_str("EH_MATUGEN_ROOT").c_str());
  if (home.empty()) {
    matugen_trace("exit early: HOME empty\n");
    return;
  }

  const fs::path base = *root / "matugen" / "configs" / "base.toml";
  const fs::path cfgd = *root / "matugen" / "configs";
  const auto& T = ap.matugenOutputs;

  std::ostringstream merged;
  if (T.runBundledToml) {
    if (!append_file(merged, base)) {
      matugen_trace("exit early: failed to read base.toml %s\n", base.string().c_str());
      return;
    }
  } else {
    merged << "[config]\n\n[templates]\n\n";
    matugen_trace("merged: runBundledToml=0 using stub [config]/[templates] only\n");
  }

  auto maybe_append = [&](bool on, const char* fname, auto&& pred) {
    if (!on) return;
    if (!pred()) return;
    const fs::path fp = cfgd / fname;
    if (!append_file(merged, fp)) matugen_trace("append_file failed: %s\n", fp.string().c_str());
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
  maybe_append(T.kittyTheme, "kitty-theme.toml", [&] { return exe_on_path("kitty"); });
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
    return exe_on_path("vesktop") && fs::exists(fs::path(cfg_main) / "vesktop");
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
  maybe_append(T.fluxer, "fluxer.toml", [&] {
    return exe_on_path("fluxer-canary") || exe_on_path("fluxer") ||
           fs::exists(fs::path(home) / ".config/fluxercanary") ||
           fs::exists(fs::path(home) / ".config/fluxer");
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

  {
    const bool obs_exe = exe_on_path("obs") || exe_on_path("obs-studio");
    const bool obs_flatpak = fs::exists(fs::path(home) / ".var/app/com.obsproject.Studio/config/obs-studio");
    matugen_trace("obs gate: toggle=%d exe=%d flatpak_dir=%d\n", T.obs ? 1 : 0, obs_exe ? 1 : 0,
                  obs_flatpak ? 1 : 0);
  }

  {
    const bool v_on = T.vesktop;
    const bool v_exe = exe_on_path("vesktop");
    const bool v_dir = fs::exists(fs::path(cfg_main) / "vesktop");
    matugen_trace(
        "vesktop gate: toggle=%d exe_on_path(vesktop)=%d cfg_dir/vesktop exists=%d path=%s\n", v_on ? 1 : 0,
        v_exe ? 1 : 0, v_dir ? 1 : 0, (fs::path(cfg_main) / "vesktop").string().c_str());
  }

  const std::string vscode_ud = vscode_user_dir_for_matugen(cfg_main);
  const fs::path vscode_ext_eh_dir = vscode_marketplace_extensions_dir(home, vscode_ud) / "eh-matugen-themes";
  const bool want_vscode = (T.vscodeMaterial || T.vscodeColorThemes) && vscode_outputs_ok(cfg_main);
  if (want_vscode) {
    ensure_vscode_eh_theme_extension(*root, vscode_ext_eh_dir);
    std::error_code ec_mk;
    fs::create_directories(fs::path(vscode_ud), ec_mk);
  }

  std::string body = merged.str();
  const bool has_vesktop_tpl = body.find("[templates.vesktop]") != std::string::npos;
  matugen_trace("merged TOML size=%zu contains [templates.vesktop]=%d\n", body.size(), has_vesktop_tpl ? 1 : 0);
  fix_paths(body, *root, cfg_main, home, vscode_ud, vscode_ext_eh_dir.string());
  if (body.find("SHELL_DIR") != std::string::npos) matugen_trace("warn: merged config still contains literal SHELL_DIR (substitution bug?)\n");

  fs::path san_dir;
  {
    const fs::path tpl_src = fs::weakly_canonical(*root / "matugen" / "templates");
    const std::string tpl_src_str = tpl_src.string();
    if (template_tree_has_legacy_event16(tpl_src)) {
      matugen_trace("templates: legacy event16 placeholders found; sanitizing copy from %s\n", tpl_src_str.c_str());
      std::array<char, 64> pat{};
      static constexpr char kPat[] = "/tmp/eh-mtpl-XXXXXX";
      if (sizeof(kPat) > pat.size()) {
        matugen_trace("exit early: san_dir pattern buffer too small\n");
        return;
      }
      std::memcpy(pat.data(), kPat, sizeof(kPat));
      if (::mkdtemp(pat.data()) == nullptr) {
        matugen_trace("exit early: mkdtemp eh-mtpl failed\n");
        return;
      }
      san_dir = pat.data();
      if (!copy_templates_sanitized(tpl_src, san_dir)) {
        matugen_trace("exit early: copy_templates_sanitized failed\n");
        std::error_code ec_rd;
        fs::remove_all(san_dir, ec_rd);
        return;
      }
      replace_all(body, tpl_src_str, fs::weakly_canonical(san_dir).string());
    }
  }

  std::array<char, 512> tbuf{};
  {
    const char* pat = "/tmp/eh-matugen-tpl-XXXXXX";
    if (std::strlen(pat) >= tbuf.size()) return;
    std::memcpy(tbuf.data(), pat, std::strlen(pat) + 1);
  }
  const int fd = ::mkstemp(tbuf.data());
  if (fd < 0) {
    matugen_trace("exit early: mkstemp matugen cfg failed errno=%d\n", errno);
    if (!san_dir.empty()) {
      std::error_code ec_rd;
      fs::remove_all(san_dir, ec_rd);
    }
    return;
  }
  const fs::path tmp_path(tbuf.data());
  matugen_trace("wrote merged matugen config tmp=%s bytes=%zu\n", tmp_path.string().c_str(), body.size());
  {
    const char* p = body.data();
    size_t left = body.size();
    while (left > 0) {
      const ssize_t nw = ::write(fd, p, left);
      if (nw <= 0) {
        matugen_trace("exit early: write merged cfg failed nw=%zd errno=%d\n", nw, errno);
        ::close(fd);
        std::error_code ec_rm;
        fs::remove(tmp_path, ec_rm);
        if (!san_dir.empty()) fs::remove_all(san_dir, ec_rm);
        return;
      }
      left -= static_cast<size_t>(nw);
      p += static_cast<size_t>(nw);
    }
  }
  ::close(fd);

  const std::string scheme = normalize_matugen_scheme(ap.matugenScheme);
  const std::string mode = normalize_matugen_mode(ap.matugenMode);
  const char* m_arg = (mode == "light") ? "light" : "dark";

  const bool matugen_ok = run_matugen_image(*root, tmp_path, wp, m_arg, scheme);
  std::error_code ec_rm;
  fs::remove(tmp_path, ec_rm);
  if (!san_dir.empty()) fs::remove_all(san_dir, ec_rm);

  if (matugen_ok) {
    matugen_trace("post-matugen hooks: matugen_ok=1\n");
    if (T.otterTerm) sync_otter_term_after_matugen(cfg_main, home);
    if (T.horizonFiles) sync_horizon_files_after_matugen(cfg_main);
    if (T.horizonPhoto) sync_horizon_photo_after_matugen(cfg_main);
    if (T.horizonCalendar) sync_horizon_calendar_after_matugen(cfg_main);
    if (T.btop && exe_on_path("btop")) sync_btop_after_matugen(cfg_main);
    if (T.vesktop && fs::exists(fs::path(cfg_main) / "vesktop")) {
      matugen_trace("vesktop post: running merge + bump mtime on midnight.theme.css\n");
      merge_vesktop_vencord_settings_after_matugen(fs::path(cfg_main) / "vesktop");
      bump_mtime_now(fs::path(cfg_main) / "vesktop" / "themes" / "midnight.theme.css");
    } else {
      matugen_trace("vesktop post: skipped (toggle=%d vesktop_dir_exists=%d)\n", T.vesktop ? 1 : 0,
                    fs::exists(fs::path(cfg_main) / "vesktop") ? 1 : 0);
    }
    if (want_vscode) {
      merge_vscode_user_settings_after_matugen(fs::path(vscode_ud), fs::path(vscode_ud) / "material-code-colors.matugen.json",
                                                T.vscodeMaterial, T.vscodeColorThemes, mode);
      touch_vscode_theme_extension_outputs(vscode_ext_eh_dir);
      bump_mtime_now(fs::path(vscode_ud) / "settings.json");
    }
    if (T.heroic && fs::exists(fs::path(cfg_main) / "heroic" / "themes" / "event-horizon.css")) {
      matugen_trace("heroic post: flipping theme selection to event-horizon\n");
      merge_heroic_prefs(fs::path(cfg_main) / "heroic");
    }
    if (T.alacritty && fs::exists(fs::path(cfg_main) / "alacritty" / "event-theme.toml")) {
      sync_alacritty_after_matugen(cfg_main);
    }
    if (T.fluxer && fs::exists(fs::path(cfg_main) / "fluxer" / "themes" / "event-horizon.css")) {
      matugen_trace("fluxer post: seeding themes + allowlist\n");
      merge_fluxer_prefs(cfg_main, home);
    }
  } else {
    matugen_trace("post-matugen hooks: SKIPPED (matugen_ok=0); vesktop merge not run\n");
  }
}

}
