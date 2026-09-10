#include "desktop_shell/common/glyph/bundled_fonts.hpp"
#include "desktop_shell/common/fs/shell_file_util.hpp"

#include "desktop_shell/common/glyph/material_glyph.hpp"

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

namespace eh::shell {

void pango_fc_clear() {
   
  PangoFontMap* fm = pango_cairo_font_map_get_default();
  if (fm && PANGO_IS_FC_FONT_MAP(fm)) pango_fc_font_map_cache_clear(PANGO_FC_FONT_MAP(fm));
}

std::string find_font_path(const char* relative_path, const char* share_suffix) {
   
  static constexpr const char* kCandidates[] = {
      "assets/fonts/",
      "src/assets/fonts/",
  };
  std::vector<std::string> candidates;
  if (const char* d = std::getenv("EH_ASSETS_DIR"); d && *d)
    candidates.push_back(std::string(d) + "/fonts/" + relative_path);
  for (const char* c : kCandidates)
    candidates.emplace_back(std::string(c) + relative_path);
  {
    const std::string exe = exe_directory();
    candidates.push_back(exe + "/../" + kCandidates[0] + relative_path);
    candidates.push_back(exe + "/../" + kCandidates[1] + relative_path);
  }
  if (const char* h = std::getenv("HOME"); h && *h)
    candidates.push_back(std::string(h) + "/.local/share" + share_suffix);
  {
    const char* xdg = std::getenv("XDG_DATA_DIRS");
    const std::string dirs = (xdg && *xdg) ? std::string(xdg) : std::string("/usr/local/share:/usr/share");
    std::size_t a = 0;
    while (a < dirs.size()) {
      std::size_t b = dirs.find(':', a);
      if (b == std::string::npos) b = dirs.size();
      std::string part = dirs.substr(a, b - a);
      while (!part.empty() && part.back() == '/') part.pop_back();
      if (!part.empty()) candidates.push_back(part + share_suffix);
      a = (b < dirs.size()) ? b + 1 : dirs.size();
    }
  }
  candidates.emplace_back(std::string("/usr/local/share") + share_suffix);
  candidates.emplace_back(std::string("/usr/share") + share_suffix);
  for (const auto& p : candidates) {
    if (!p.empty() && file_exists_str(p)) return p;
  }
  return {};
}

bool register_font_with_fontconfig(const std::string& path) {
   
  if (path.empty()) return false;
  FcConfig* cfg = FcConfigGetCurrent();
  if (!cfg) return false;
  if (FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.c_str())) != FcTrue) return false;
  return true;
}

void register_font_family(const std::vector<const char*>& filenames, const char* /*subdir*/, const char* share_suffix) {
   
  bool any = false;
  for (const char* fn : filenames) {
    std::string path = find_font_path(fn, share_suffix);
    if (register_font_with_fontconfig(path)) any = true;
  }
  if (any) {
    FcConfigBuildFonts(FcConfigGetCurrent());
    pango_fc_clear();
  }
}

void register_bundled_fonts_once() {
   
  static bool did = false;
  if (did) return;
  did = true;

  ensure_material_symbols_font_registered();

  register_font_family(
      {"inter/docs/font-files/InterVariable.ttf"},
      "inter/",
      "/event-horizon/assets/fonts/inter/InterVariable.ttf");
}

}
