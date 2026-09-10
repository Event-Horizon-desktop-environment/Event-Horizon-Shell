#include "desktop_shell/common/palette/horizon_colors_palette.hpp"

#include "color/horizon_colors.hpp"
#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "desktop_shell/common/palette/matugen_palette.hpp"
#include "ux/settings/common/trace/settings_trace.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sys/stat.h>

namespace eh::horizon_colors {
namespace {

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

bool want_light_mode(const std::string& modeNorm) { return modeNorm == "light"; }

[[nodiscard]] std::string disk_cache_path() {
  return eh::config::state_event_horizon_dir() + "/horizon-colors-palette.cache";
}

// Try the legacy external cache path as fallback for first-run migration.
[[nodiscard]] std::string legacy_disk_cache_path() {
  return eh::config::state_event_horizon_dir() + "/matugen-palette.cache";
}

[[nodiscard]] bool try_load_disk_cache(const std::string& path, const struct stat& st,
                                       const std::string& scheme, const std::string& modeNorm) {
  // Try new path first, then legacy path for migration.
  for (const auto& cache_file : {disk_cache_path(), legacy_disk_cache_path()}) {
    std::ifstream in(cache_file);
    if (!in) continue;
    std::string ver;
    if (!std::getline(in, ver) || (ver != "v2" && ver != "v3")) continue;
    std::string disk_path;
    in >> std::quoted(disk_path);
    if (!in || disk_path != path) continue;
    long long sec = 0;
    long long nsec = 0;
    in >> sec >> nsec;
    if (!in || sec != static_cast<long long>(st.st_mtim.tv_sec) ||
        nsec != static_cast<long long>(st.st_mtim.tv_nsec)) {
      continue;
    }
    std::string disk_scheme;
    std::string disk_mode;
    in >> std::quoted(disk_scheme) >> std::quoted(disk_mode);
    if (!in || disk_scheme != scheme || disk_mode != modeNorm) continue;
    int ok_flag = 0;
    in >> ok_flag;
    if (!in || ok_flag != 1) continue;

    float dr = 0, dg = 0, db = 0, pr = 0, pg = 0, pb = 0, wr = 0, wg = 0, wb = 0;
    float ox = 0, oy = 0, oz = 0, ar = 0, ag = 0, ab = 0;
    float ncr = 0, ncg = 0, ncb = 0, nor = 0, nog = 0, nob = 0;
    in >> dr >> dg >> db >> pr >> pg >> pb >> wr >> wg >> wb >> ox >> oy >> oz >> ar >> ag >> ab;
    if (in.fail()) continue;
    if (ver == "v3") {
      in >> ncr >> ncg >> ncb >> nor >> nog >> nob;
      if (in.fail()) continue;
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
  return false;
}

void save_disk_cache(const std::string& path, const struct stat& st, const std::string& scheme,
                     const std::string& modeNorm) {
  namespace fs = std::filesystem;
  const std::string dir = eh::config::state_event_horizon_dir();
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) return;
  const std::string final_path = disk_cache_path();
  const std::string tmp = final_path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << std::setprecision(10);
    out << "v3\n";
    out << std::quoted(path) << '\n';
    out << static_cast<long long>(st.st_mtim.tv_sec) << ' '
        << static_cast<long long>(st.st_mtim.tv_nsec) << '\n';
    out << std::quoted(scheme) << ' ' << std::quoted(modeNorm) << '\n';
    out << "1\n";
    out << g_cache.dockR << ' ' << g_cache.dockG << ' ' << g_cache.dockB << '\n';
    out << g_cache.panelR << ' ' << g_cache.panelG << ' ' << g_cache.panelB << '\n';
    out << g_cache.drawerR << ' ' << g_cache.drawerG << ' ' << g_cache.drawerB << '\n';
    out << g_cache.outlineR << ' ' << g_cache.outlineG << ' ' << g_cache.outlineB << '\n';
    out << g_cache.accentR << ' ' << g_cache.accentG << ' ' << g_cache.accentB << '\n';
    out << g_cache.notifCriticalBgR << ' ' << g_cache.notifCriticalBgG << ' '
        << g_cache.notifCriticalBgB << '\n';
    out << g_cache.notifCriticalOutlineR << ' ' << g_cache.notifCriticalOutlineG << ' '
        << g_cache.notifCriticalOutlineB << '\n';
    out.flush();
    if (!out) {
      fs::remove(tmp, ec);
      return;
    }
  }
  std::error_code ec2;
  fs::rename(tmp, final_path, ec2);
  debug_log("hc_palette", "save_disk_cache OK path=%s scheme=%s mode=%s accent=%02x%02x%02x",
            path.c_str(), scheme.c_str(), modeNorm.c_str(),
            static_cast<unsigned>(std::round(g_cache.accentR * 255.0)),
            static_cast<unsigned>(std::round(g_cache.accentG * 255.0)),
            static_cast<unsigned>(std::round(g_cache.accentB * 255.0)));
}

void copy_cache_to_appearance(eh::config::ShellAppearance& appearance) {
  appearance.hcDockFillR = g_cache.dockR;
  appearance.hcDockFillG = g_cache.dockG;
  appearance.hcDockFillB = g_cache.dockB;
  appearance.hcPanelFillR = g_cache.panelR;
  appearance.hcPanelFillG = g_cache.panelG;
  appearance.hcPanelFillB = g_cache.panelB;
  appearance.hcDrawerDimR = g_cache.drawerR;
  appearance.hcDrawerDimG = g_cache.drawerG;
  appearance.hcDrawerDimB = g_cache.drawerB;
  appearance.hcOutlineR = g_cache.outlineR;
  appearance.hcOutlineG = g_cache.outlineG;
  appearance.hcOutlineB = g_cache.outlineB;
  appearance.hcAccentR = g_cache.accentR;
  appearance.hcAccentG = g_cache.accentG;
  appearance.hcAccentB = g_cache.accentB;
  // Text is always pure white in dark, pure black in light — never from the color engine.
  const bool light = want_light_mode(g_cache.modeNorm);
  appearance.hcTextR = light ? 0.0f : 1.0f;
  appearance.hcTextG = light ? 0.0f : 1.0f;
  appearance.hcTextB = light ? 0.0f : 1.0f;
  appearance.hcNotifCriticalBgR = g_cache.notifCriticalBgR;
  appearance.hcNotifCriticalBgG = g_cache.notifCriticalBgG;
  appearance.hcNotifCriticalBgB = g_cache.notifCriticalBgB;
  appearance.hcNotifCriticalOutlineR = g_cache.notifCriticalOutlineR;
  appearance.hcNotifCriticalOutlineG = g_cache.notifCriticalOutlineG;
  appearance.hcNotifCriticalOutlineB = g_cache.notifCriticalOutlineB;
  appearance.horizonColorsPaletteOk = g_cache.ok;
}

eh::color::SchemeVariant resolve_variant(std::string_view scheme) {
  return eh::color::scheme_variant_from_name(scheme);
}

} // namespace

void refresh_wallpaper_derived_palette_native(eh::config::ShellAppearance& appearance,
                                              const std::string& path) {
  const bool bench = eh::settings::trace::bench();
  const auto t0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  debug_log("hc_palette", "refresh_native: enter path=%s native=%d scheme=%s mode=%s",
            path.c_str(), appearance.horizonColorsNative ? 1 : 0,
            appearance.matugenScheme.c_str(), appearance.matugenMode.c_str());

  appearance.horizonColorsPaletteOk = false;
  if (!appearance.horizonColorsNative) {
    g_cache.theming = false;
    g_cache.path.clear();
    if (bench) {
      std::cerr << "[settings-bench] horizon_colors_palette skip reason=theming_off path_len="
                << path.size() << "\n";
    }
    return;
  }
  if (path.empty() || ::access(path.c_str(), R_OK) != 0) {
    if (bench)
      std::cerr << "[settings-bench] horizon_colors_palette skip reason=no_path_or_unreadable path_len="
                << path.size() << "\n";
    return;
  }

  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    if (bench) std::cerr << "[settings-bench] horizon_colors_palette skip reason=stat_failed\n";
    return;
  }

  const std::string scheme = eh::matugen::normalize_matugen_scheme(appearance.matugenScheme);
  const std::string modeNorm = eh::matugen::normalize_matugen_mode(appearance.matugenMode);
  appearance.matugenScheme = scheme;
  appearance.matugenMode = modeNorm;

  // In-memory cache check
  const bool cache_hit = g_cache.ok && g_cache.theming && g_cache.path == path &&
                         g_cache.scheme == scheme && g_cache.modeNorm == modeNorm &&
                         g_cache.mt.tv_sec == st.st_mtim.tv_sec &&
                         g_cache.mt.tv_nsec == st.st_mtim.tv_nsec;
  if (cache_hit) {
    copy_cache_to_appearance(appearance);
    if (bench) {
      const int64_t us =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
              .count();
      std::cerr << "[settings-bench] horizon_colors_palette cache_hit total_us=" << us
                << " scheme=\"" << scheme << "\" mode=\"" << modeNorm
                << "\" path_len=" << path.size() << "\n";
    }
    return;
  }

  // Disk cache check
  if (try_load_disk_cache(path, st, scheme, modeNorm)) {
    copy_cache_to_appearance(appearance);
    debug_log("hc_palette", "refresh_native: DISK_CACHE_HIT scheme=%s mode=%s ok=%d",
              scheme.c_str(), modeNorm.c_str(), appearance.horizonColorsPaletteOk ? 1 : 0);
    if (bench) {
      const int64_t us =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
              .count();
      std::cerr << "[settings-bench] horizon_colors_palette cache_hit_disk total_us=" << us
                << " scheme=\"" << scheme << "\" mode=\"" << modeNorm
                << "\" path_len=" << path.size() << "\n";
    }
    return;
  }

  // Native palette generation.
  const bool is_dark = !want_light_mode(modeNorm);
  const eh::color::SchemeVariant variant = resolve_variant(scheme);
  const float contrast = 0.0f;

  const auto t_gen0 = bench ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
  eh::color::PaletteResult result =
      eh::color::generate_palette_from_image(path, variant, is_dark, contrast);
  const int64_t us_gen =
      bench
          ? std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t_gen0)
                .count()
          : 0;

  if (!result.ok) {
    debug_log("hc_palette", "refresh_native: GENERATION_FAILED scheme=%s mode=%s path=%s",
              scheme.c_str(), modeNorm.c_str(), path.c_str());
    if (bench) {
      const int64_t us_tot =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
              .count();
      std::cerr << "[settings-bench] horizon_colors_palette native_failed gen_us=" << us_gen
                << " total_us=" << us_tot << " scheme=\"" << scheme << "\" mode=\"" << modeNorm
                << "\"\n";
    }
    return;
  }

  // Populate cache from native result.
  g_cache.path = path;
  g_cache.mt = st.st_mtim;
  g_cache.scheme = scheme;
  g_cache.modeNorm = modeNorm;
  g_cache.theming = true;
  g_cache.ok = true;
  g_cache.dockR = result.dockFillR;
  g_cache.dockG = result.dockFillG;
  g_cache.dockB = result.dockFillB;
  g_cache.panelR = result.panelFillR;
  g_cache.panelG = result.panelFillG;
  g_cache.panelB = result.panelFillB;
  g_cache.drawerR = result.drawerDimR;
  g_cache.drawerG = result.drawerDimG;
  g_cache.drawerB = result.drawerDimB;
  g_cache.outlineR = result.outlineR;
  g_cache.outlineG = result.outlineG;
  g_cache.outlineB = result.outlineB;
  g_cache.accentR = result.accentR;
  g_cache.accentG = result.accentG;
  g_cache.accentB = result.accentB;
  g_cache.textR = result.textR;
  g_cache.textG = result.textG;
  g_cache.textB = result.textB;
  g_cache.notifCriticalBgR = result.notifCriticalBgR;
  g_cache.notifCriticalBgG = result.notifCriticalBgG;
  g_cache.notifCriticalBgB = result.notifCriticalBgB;
  g_cache.notifCriticalOutlineR = result.notifCriticalOutlineR;
  g_cache.notifCriticalOutlineG = result.notifCriticalOutlineG;
  g_cache.notifCriticalOutlineB = result.notifCriticalOutlineB;

  copy_cache_to_appearance(appearance);
  save_disk_cache(path, st, scheme, modeNorm);
  debug_log("hc_palette", "refresh_native: GENERATION_OK scheme=%s mode=%s ok=%d gen_us=%lld",
            scheme.c_str(), modeNorm.c_str(), appearance.horizonColorsPaletteOk ? 1 : 0,
            static_cast<long long>(us_gen));

  if (bench) {
    const int64_t us_total =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    std::cerr << "[settings-bench] horizon_colors_palette native_ok gen_us=" << us_gen
              << " total_us=" << us_total << " scheme=\"" << scheme << "\" mode=\"" << modeNorm
              << "\" variant=" << eh::color::scheme_variant_name(variant)
              << " dark=" << (is_dark ? 1 : 0) << "\n";
  }
}

} // namespace eh::horizon_colors
