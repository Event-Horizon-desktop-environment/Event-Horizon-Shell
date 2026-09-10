#include "platform/common/glyph/bundled_assets.hpp"

#include <fontconfig/fontconfig.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace eh::shell {

static std::string find_material_symbols_ttf() {
  const char* env = std::getenv("EH_ASSETS_DIR");
  if (env) {
    std::string p = std::string(env) + "/fonts/MF/MaterialSymbolsRounded.ttf";
    FILE* f = std::fopen(p.c_str(), "r");
    if (f) { std::fclose(f); return p; }
  }

  const char* dirs[] = {
    "assets/fonts/MF",
    "assets/fonts/MF",
    "/usr/local/share/event-horizon/assets/fonts/MF",
    "/usr/share/event-horizon/assets/fonts/MF",
  };
  for (auto* d : dirs) {
    std::string p = std::string(d) + "/MaterialSymbolsRounded.ttf";
    FILE* f = std::fopen(p.c_str(), "r");
    if (f) { std::fclose(f); return p; }
  }

  return {};
}

bool bundled_try_register_material_symbols_fontconfig() {
  static bool tried = false;
  static bool ok = false;
  if (tried) return ok;
  tried = true;

  std::string path = find_material_symbols_ttf();
  if (path.empty()) return false;

  FcConfig* cfg = FcConfigGetCurrent();
  if (!cfg) return false;
  if (FcConfigAppFontAddFile(cfg, (const FcChar8*)path.c_str()) != FcTrue)
    return false;
  FcConfigBuildFonts(cfg);
  ok = true;
  return true;
}

}
