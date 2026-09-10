#define _GNU_SOURCE 1
#include "desktop_shell/common/log/mangowm_logger.hpp"
#include "desktop_shell/common/glyph/bundled_assets.hpp"

#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <array>
#include <cstdio>

#if EH_EMBEDDED_ASSETS
#include "embedded_bundled_data.hpp"
#endif

#if EH_EMBEDDED_ASSETS && defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace eh::shell {

bool bundled_try_register_material_symbols_fontconfig() {
   
#if !EH_EMBEDDED_ASSETS || !defined(__linux__)
  return false;
#else
  if (eh::embedded::kMaterialSymbolsRoundedTtf_size == 0) return false;
  static int s_memfd = -1;
  static bool s_ok = false;
  if (s_ok) return true;
  if (s_memfd >= 0) return false;

  int fd = memfd_create("eh-material-symbols.ttf", MFD_CLOEXEC);
  if (fd < 0) return false;
  const unsigned char* p = eh::embedded::kMaterialSymbolsRoundedTtf;
  std::size_t left = eh::embedded::kMaterialSymbolsRoundedTtf_size;
  while (left > 0) {
    const ssize_t chunk = static_cast<ssize_t>(std::min(left, static_cast<std::size_t>(SSIZE_MAX)));
    const ssize_t w = write(fd, p, chunk);
    if (w <= 0) {
      close(fd);
      return false;
    }
    p += static_cast<std::size_t>(w);
    left -= static_cast<std::size_t>(w);
  }

  std::array<char, 64> path;
  const int plen = std::snprintf(path.data(), path.size(), "/proc/self/fd/%d", fd);
  if (plen <= 0 || static_cast<std::size_t>(plen) >= path.size()) {
    close(fd);
    return false;
  }

  FcConfig* cfg = FcConfigGetCurrent();
  if (!cfg || FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.data())) != FcTrue) {
    close(fd);
    return false;
  }
  FcConfigBuildFonts(cfg);
  s_memfd = fd;
  s_ok = true;
  return true;
#endif
}

}
