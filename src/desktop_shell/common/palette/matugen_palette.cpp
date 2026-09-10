#include "desktop_shell/common/palette/matugen_palette.hpp"

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/palette/horizon_colors_palette.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"

#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <fcntl.h>
#include <spawn.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern "C" char** environ;

namespace eh::matugen {
namespace {

static constexpr const char* kSchemeCli[] = {
    "scheme-content",    "scheme-expressive", "scheme-fidelity",   "scheme-fruit-salad",
    "scheme-monochrome", "scheme-neutral",    "scheme-rainbow",    "scheme-tonal-spot",
    "scheme-vibrant",
};

bool scheme_is_known(std::string_view s) {
   
  for (const char* v : kSchemeCli) {
    if (s == v) return true;
  }
  return false;
}

}

std::string normalize_matugen_scheme(std::string_view raw) {
   
  std::string s(raw);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  if (scheme_is_known(s)) return s;

  if (s.size() > 4 && s.compare(0, 4, "m3-") == 0) s.erase(0, 4);

  if (s == "content") return "scheme-content";
  if (s == "expressive") return "scheme-expressive";
  if (s == "fidelity" || s == "faithful") return "scheme-fidelity";
  if (s == "fruit-salad" || s == "fruitsalad") return "scheme-fruit-salad";
  if (s == "monochrome") return "scheme-monochrome";
  if (s == "neutral") return "scheme-neutral";
  if (s == "rainbow") return "scheme-rainbow";
  if (s == "tonal-spot" || s == "tonal_spot") return "scheme-tonal-spot";
  if (s == "vibrant") return "scheme-vibrant";

  if (scheme_is_known(std::string_view(s))) return s;

  return "scheme-content";
}

std::string normalize_matugen_mode(std::string_view raw) {
   
  std::string s(raw);
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s == "light") return "light";
  if (s == "auto") return "auto";
  return "dark";
}

namespace {

const char* matugen_m_argument(const std::string& modeNorm) {
   
  if (modeNorm == "light") return "light";
  return "dark";
}

bool want_light_json_branch(const std::string& modeNorm) { return modeNorm == "light"; }

bool hex_byte_pair(const char* p, unsigned& out) {
   
  auto h = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const int hi = h(p[0]);
  const int lo = h(p[1]);
  if (hi < 0 || lo < 0) return false;
  out = static_cast<unsigned>(hi * 16 + lo);
  return true;
}

bool hex6_to_rgb(const char* digit6, float& r, float& g, float& b) {
   
  unsigned rr = 0, gg = 0, bb = 0;
  if (!hex_byte_pair(digit6, rr)) return false;
  if (!hex_byte_pair(digit6 + 2, gg)) return false;
  if (!hex_byte_pair(digit6 + 4, bb)) return false;
  r = rr / 255.f;
  g = gg / 255.f;
  b = bb / 255.f;
  return true;
}

bool extract_branch_hex(const std::string& json, std::string_view key, std::string_view branchQuote,
                        float& r, float& g, float& b) {
   
  const std::string needle = std::string("\"") + std::string(key) + "\"";
  const size_t p = json.find(needle);
  if (p == std::string::npos) return false;
  const size_t zone_end = json.find('{', p);
  if (zone_end == std::string::npos) return false;
  const size_t branch = json.find(branchQuote, zone_end);
  if (branch == std::string::npos || branch > zone_end + 4000) return false;
  for (size_t hs = branch; hs < json.size() && hs < branch + 200; ++hs) {
    if (json[hs] != '#') continue;
    const size_t d0 = hs + 1;
    std::array<char, 7> buf{};
    size_t cnt = 0;
    for (size_t j = d0; j < json.size() && cnt < 6; ++j) {
      const char c = json[j];
      if (std::isxdigit(static_cast<unsigned char>(c)))
        buf[cnt++] = c;
      else if (cnt > 0)
        break;
    }
    if (cnt != 6) return false;
    buf[6] = '\0';
    return hex6_to_rgb(buf.data(), r, g, b);
  }
  return false;
}

bool extract_color_hex(const std::string& json, std::string_view key, bool light, float& r, float& g, float& b) {
   
  const std::string_view lit = light ? std::string_view("\"light\"") : std::string_view("\"dark\"");
  if (extract_branch_hex(json, key, lit, r, g, b)) return true;
  const std::string_view alt = light ? std::string_view("\"dark\"") : std::string_view("\"light\"");
  return extract_branch_hex(json, key, alt, r, g, b);
}

struct PaletteCache {
  std::string path;
  timespec mt{};
  std::string scheme;
  std::string modeNorm;
  bool theming = false;
  bool ok = false;
  float dockR = 0.f, dockG = 0.f, dockB = 0.f;
  float panelR = 0.f, panelG = 0.f, panelB = 0.f;
  float drawerR = 0.f, drawerG = 0.f, drawerB = 0.f;
  float outlineR = 0.f, outlineG = 0.f, outlineB = 0.f;
  float accentR = 0.f, accentG = 0.f, accentB = 0.f;
  float textR = 0.f, textG = 0.f, textB = 0.f;
  float notifCriticalBgR = 0.f, notifCriticalBgG = 0.f, notifCriticalBgB = 0.f;
  float notifCriticalOutlineR = 0.f, notifCriticalOutlineG = 0.f, notifCriticalOutlineB = 0.f;
};

static PaletteCache g_cache;

bool spawn_matugen_stdout(const std::string& absolute_image, const char* modeArg, const std::string& typeArg,
                          std::string& out_stdout) {
   
  std::array<int, 2> pipefd{};
  if (pipe(pipefd.data()) != 0) return false;

  posix_spawn_file_actions_t fa{};
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addclose(&fa, pipefd[0]);
  posix_spawn_file_actions_adddup2(&fa, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  const int devnull = open("/dev/null", O_WRONLY);
  if (devnull >= 0) posix_spawn_file_actions_adddup2(&fa, devnull, STDERR_FILENO);

  std::vector<std::string> parts = {"matugen",       "image",     absolute_image, "--json",
                                    "hex",           "--dry-run", "-m",           std::string(modeArg),
                                    "--source-color-index", "0", "--old-json-output", "-t", typeArg};
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
  if (devnull >= 0) close(devnull);
  posix_spawn_file_actions_destroy(&fa);
  close(pipefd[1]);
  if (spawn_err != 0 || pid <= 0) {
    close(pipefd[0]);
    return false;
  }

  out_stdout.clear();
  std::array<char, 16384> buf;
  ssize_t nr;
  while ((nr = read(pipefd[0], buf.data(), buf.size())) > 0) out_stdout.append(buf.data(), static_cast<size_t>(nr));
  close(pipefd[0]);

  int st = 0;
  (void)waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return false;
  return !out_stdout.empty();
}

[[nodiscard]] std::string matugen_disk_cache_path() {
   
  return eh::config::state_event_horizon_dir() + "/matugen-palette.cache";
}

[[nodiscard]] bool try_load_matugen_disk_cache(const std::string& path, const struct stat& st,
                                                      const std::string& scheme, const std::string& modeNorm) {
   
  std::ifstream in(matugen_disk_cache_path());
  if (!in) return false;
  std::string ver;
  if (!std::getline(in, ver) || (ver != "v2" && ver != "v3")) return false;
  std::string disk_path;
  in >> std::quoted(disk_path);
  if (!in || disk_path != path) return false;
  long long sec = 0;
  long long nsec = 0;
  in >> sec >> nsec;
  if (!in || sec != static_cast<long long>(st.st_mtim.tv_sec) || nsec != static_cast<long long>(st.st_mtim.tv_nsec)) {
    return false;
  }
  std::string disk_scheme;
  std::string disk_mode;
  in >> std::quoted(disk_scheme) >> std::quoted(disk_mode);
  if (!in || disk_scheme != scheme || disk_mode != modeNorm) return false;
  int ok_flag = 0;
  in >> ok_flag;
  if (!in || ok_flag != 1) return false;

  float dr = 0, dg = 0, db = 0, pr = 0, pg = 0, pb = 0, wr = 0, wg = 0, wb = 0;
  float ox = 0, oy = 0, oz = 0, ar = 0, ag = 0, ab = 0;
  float ncr = 0, ncg = 0, ncb = 0, nor = 0, nog = 0, nob = 0;
  in >> dr >> dg >> db >> pr >> pg >> pb >> wr >> wg >> wb >> ox >> oy >> oz >> ar >> ag >> ab;
  if (in.fail()) return false;
  if (ver == "v3") {
    in >> ncr >> ncg >> ncb >> nor >> nog >> nob;
    if (in.fail()) return false;
  }

  g_cache.path = path;
  g_cache.mt = st.st_mtim;
  g_cache.scheme = scheme;
  g_cache.modeNorm = modeNorm;
  g_cache.theming = true;
  g_cache.ok = true;
  g_cache.dockR = dr;
  g_cache.dockG = dg;
  g_cache.dockB = db;
  g_cache.panelR = pr;
  g_cache.panelG = pg;
  g_cache.panelB = pb;
  g_cache.drawerR = wr;
  g_cache.drawerG = wg;
  g_cache.drawerB = wb;
  g_cache.outlineR = ox;
  g_cache.outlineG = oy;
  g_cache.outlineB = oz;
  g_cache.accentR = ar;
  g_cache.accentG = ag;
  g_cache.accentB = ab;
  g_cache.notifCriticalBgR = ncr;
  g_cache.notifCriticalBgG = ncg;
  g_cache.notifCriticalBgB = ncb;
  g_cache.notifCriticalOutlineR = nor;
  g_cache.notifCriticalOutlineG = nog;
  g_cache.notifCriticalOutlineB = nob;
  return true;
}

void save_matugen_disk_cache(const std::string& path, const struct stat& st, const std::string& scheme,
                                    const std::string& modeNorm) {
   
  namespace fs = std::filesystem;
  const std::string dir = eh::config::state_event_horizon_dir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return;
  const std::string final = matugen_disk_cache_path();
  const std::string tmp = final + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << std::setprecision(10);
    out << "v3\n";
    out << std::quoted(path) << '\n';
    out << static_cast<long long>(st.st_mtim.tv_sec) << ' ' << static_cast<long long>(st.st_mtim.tv_nsec) << '\n';
    out << std::quoted(scheme) << ' ' << std::quoted(modeNorm) << '\n';
    out << "1\n";
    out << g_cache.dockR << ' ' << g_cache.dockG << ' ' << g_cache.dockB << '\n';
    out << g_cache.panelR << ' ' << g_cache.panelG << ' ' << g_cache.panelB << '\n';
    out << g_cache.drawerR << ' ' << g_cache.drawerG << ' ' << g_cache.drawerB << '\n';
    out << g_cache.outlineR << ' ' << g_cache.outlineG << ' ' << g_cache.outlineB << '\n';
    out << g_cache.accentR << ' ' << g_cache.accentG << ' ' << g_cache.accentB << '\n';
    out << g_cache.notifCriticalBgR << ' ' << g_cache.notifCriticalBgG << ' ' << g_cache.notifCriticalBgB << '\n';
    out << g_cache.notifCriticalOutlineR << ' ' << g_cache.notifCriticalOutlineG << ' ' << g_cache.notifCriticalOutlineB << '\n';
    out.flush();
    if (!out) {
      fs::remove(tmp, ec);
      return;
    }
  }
  std::error_code ec2;
  fs::rename(tmp, final, ec2);
}

void copy_cache_to_appearance(eh::config::ShellAppearance& appearance) {
   
  appearance.matugenDockFillR = g_cache.dockR;
  appearance.matugenDockFillG = g_cache.dockG;
  appearance.matugenDockFillB = g_cache.dockB;
  appearance.matugenPanelFillR = g_cache.panelR;
  appearance.matugenPanelFillG = g_cache.panelG;
  appearance.matugenPanelFillB = g_cache.panelB;
  appearance.matugenDrawerDimR = g_cache.drawerR;
  appearance.matugenDrawerDimG = g_cache.drawerG;
  appearance.matugenDrawerDimB = g_cache.drawerB;
  appearance.matugenOutlineR = g_cache.outlineR;
  appearance.matugenOutlineG = g_cache.outlineG;
  appearance.matugenOutlineB = g_cache.outlineB;
  appearance.matugenAccentR = g_cache.accentR;
  appearance.matugenAccentG = g_cache.accentG;
  appearance.matugenAccentB = g_cache.accentB;
  appearance.matugenNotifCriticalBgR = g_cache.notifCriticalBgR;
  appearance.matugenNotifCriticalBgG = g_cache.notifCriticalBgG;
  appearance.matugenNotifCriticalBgB = g_cache.notifCriticalBgB;
  appearance.matugenNotifCriticalOutlineR = g_cache.notifCriticalOutlineR;
  appearance.matugenNotifCriticalOutlineG = g_cache.notifCriticalOutlineG;
  appearance.matugenNotifCriticalOutlineB = g_cache.notifCriticalOutlineB;
  appearance.matugenPaletteOk = g_cache.ok;
}

}

void refresh_wallpaper_derived_palette(eh::config::ShellAppearance& appearance, const std::string& path) {
   
  // Route to the native Horizon Colors engine when enabled.
  if (appearance.horizonColorsNative) {
    eh::horizon_colors::refresh_wallpaper_derived_palette_native(appearance, path);
    return;
  }

  const bool bench = eh::settings::trace::bench();
  const auto t0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

  appearance.matugenPaletteOk = false;
  if (!appearance.matugenThemingEnabled) {
    g_cache.theming = false;
    g_cache.path.clear();
    if (bench) {
      std::cerr << "[settings-bench] matugen_palette skip reason=theming_off path_len=" << path.size() << "\n";
    }
    return;
  }
  if (path.empty() || ::access(path.c_str(), R_OK) != 0) {
    if (bench) std::cerr << "[settings-bench] matugen_palette skip reason=no_path_or_unreadable path_len=" << path.size() << "\n";
    return;
  }

  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (bench) std::cerr << "[settings-bench] matugen_palette skip reason=stat_failed\n";
    return;
  }

  const std::string scheme = normalize_matugen_scheme(appearance.matugenScheme);
  const std::string modeNorm = normalize_matugen_mode(appearance.matugenMode);
  appearance.matugenScheme = scheme;
  appearance.matugenMode = modeNorm;

  const bool cache_hit = g_cache.ok && g_cache.theming && g_cache.path == path && g_cache.scheme == scheme &&
                         g_cache.modeNorm == modeNorm && g_cache.mt.tv_sec == st.st_mtim.tv_sec &&
                         g_cache.mt.tv_nsec == st.st_mtim.tv_nsec;
  if (cache_hit) {
    copy_cache_to_appearance(appearance);
    if (bench) {
      const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
      std::cerr << "[settings-bench] matugen_palette cache_hit total_us=" << us << " scheme=\"" << scheme << "\" mode=\""
                << modeNorm << "\" path_len=" << path.size() << "\n";
    }
    return;
  }

  if (try_load_matugen_disk_cache(path, st, scheme, modeNorm)) {
    copy_cache_to_appearance(appearance);
    if (bench) {
      const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
      std::cerr << "[settings-bench] matugen_palette cache_hit_disk total_us=" << us << " scheme=\"" << scheme << "\" mode=\""
                << modeNorm << "\" path_len=" << path.size() << "\n";
    }
    return;
  }

  const char* mArg = matugen_m_argument(modeNorm);
  const bool light = want_light_json_branch(modeNorm);

  const auto t_spawn0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  std::string json;
  if (!spawn_matugen_stdout(path, mArg, scheme, json)) {
    if (bench) {
      const int64_t us_sp =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_spawn0).count();
      const int64_t us_tot =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
      std::cerr << "[settings-bench] matugen_palette spawn_failed spawn_us=" << us_sp << " total_us=" << us_tot
                << " scheme=\"" << scheme << "\" mode=\"" << modeNorm << "\"\n";
    }
    return;
  }
  const int64_t us_spawn =
      bench ? std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_spawn0).count()
            : 0;

  const auto t_parse0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

  float barR = 0.f, barG = 0.f, barB = 0.f;
  if (!extract_color_hex(json, "surface_container", light, barR, barG, barB)) {
    if (!extract_color_hex(json, "surface", light, barR, barG, barB)) {
      if (bench) {
        const int64_t us_tot =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        std::cerr << "[settings-bench] matugen_palette parse_fail after_spawn json_bytes=" << json.size()
                  << " spawn_us=" << us_spawn << " total_us=" << us_tot << "\n";
      }
      return;
    }
  }

  float varR = barR, varG = barG, varB = barB;
  (void)extract_color_hex(json, "surface_variant", light, varR, varG, varB);

  float ox = 0.55f, oy = 0.60f, oz = 0.62f;
  if (!extract_color_hex(json, "outline", light, ox, oy, oz)) {
    if (!extract_color_hex(json, "outline_variant", light, ox, oy, oz)) {
      ox = varR;
      oy = varG;
      oz = varB;
    }
  }

  float ar = 0.9f, ag = 0.9f, ab = 0.9f;
  (void)extract_color_hex(json, "primary", light, ar, ag, ab);

  float ncr = 0.18f, ncg = 0.06f, ncb = 0.06f;
  (void)extract_color_hex(json, "error_container", light, ncr, ncg, ncb);
  float nor = 0.70f, nog = 0.10f, nob = 0.10f;
  (void)extract_color_hex(json, "error", light, nor, nog, nob);

  g_cache.path = path;
  g_cache.mt = st.st_mtim;
  g_cache.scheme = scheme;
  g_cache.modeNorm = modeNorm;
  g_cache.theming = true;
  g_cache.ok = true;
  g_cache.dockR = barR;
  g_cache.dockG = barG;
  g_cache.dockB = barB;
  g_cache.panelR = barR;
  g_cache.panelG = barG;
  g_cache.panelB = barB;
  g_cache.drawerR = varR;
  g_cache.drawerG = varG;
  g_cache.drawerB = varB;
  g_cache.outlineR = ox;
  g_cache.outlineG = oy;
  g_cache.outlineB = oz;
  g_cache.accentR = ar;
  g_cache.accentG = ag;
  g_cache.accentB = ab;
  g_cache.notifCriticalBgR = ncr;
  g_cache.notifCriticalBgG = ncg;
  g_cache.notifCriticalBgB = ncb;
  g_cache.notifCriticalOutlineR = nor;
  g_cache.notifCriticalOutlineG = nog;
  g_cache.notifCriticalOutlineB = nob;

  copy_cache_to_appearance(appearance);
  save_matugen_disk_cache(path, st, scheme, modeNorm);

  if (bench) {
    const int64_t us_parse =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t_parse0).count();
    const int64_t us_total =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cerr << "[settings-bench] matugen_palette cache_miss_ok json_bytes=" << json.size() << " spawn_us=" << us_spawn
              << " parse_apply_us=" << us_parse << " total_us=" << us_total << " scheme=\"" << scheme << "\" mode=\""
              << modeNorm << "\" light_json=" << (light ? 1 : 0) << "\n";
  }
}

}
