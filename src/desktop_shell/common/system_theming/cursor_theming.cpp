#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include "desktop_shell/common/fs/string_util.hpp"

#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <dlfcn.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace eh::theming {

using eh::shell::str::file_exists;
using eh::theming::detail::icon_base_dirs;
using eh::theming::detail::read_ini_kv;

namespace {

static cairo_surface_t* xcursor_file_to_surface(const std::string& cursorFilePath, int targetPx) {
   
  // Dynamically load libXcursor
  static void* xcursorLib = nullptr;
  static bool triedLoad = false;
  if (!triedLoad) {
    triedLoad = true;
    xcursorLib = dlopen("libXcursor.so.1", RTLD_LAZY | RTLD_LOCAL);
  }
  if (!xcursorLib) return nullptr;

  using FnLoadImages = void*(*)(const char*, int);
  using FnImagesDestroy = void(*)(void*);

  static FnLoadImages fnLoad = nullptr;
  static FnImagesDestroy fnDestroy = nullptr;
  static bool triedSym = false;
  if (!triedSym) {
    triedSym = true;
    fnLoad = reinterpret_cast<FnLoadImages>(dlsym(xcursorLib, "XcursorFilenameLoadImages"));
    fnDestroy = reinterpret_cast<FnImagesDestroy>(dlsym(xcursorLib, "XcursorImagesDestroy"));
  }
  if (!fnLoad || !fnDestroy) return nullptr;

  // Try sizes: prefer the best match for targetPx, then fall back to 32, 24
  int trySizes[] = {targetPx, 32, 24, 48, 64};
  void* images = nullptr;
  for (int sz : trySizes) {
    images = fnLoad(cursorFilePath.c_str(), sz);
    if (images) break;
  }
  if (!images) return nullptr;

  // Read XcursorImages - first field is nimage (int)
  int nimage = *reinterpret_cast<const int*>(images);
  void** imgPtrs = nullptr;
  {
    char* base = static_cast<char*>(images);
    imgPtrs = *reinterpret_cast<void***>(base + 8);
  }

  cairo_surface_t* result = nullptr;
  if (nimage > 0 && imgPtrs) {
    void* img = imgPtrs[0];
    if (img) {
      const char* ibase = static_cast<const char*>(img);
      int w = *reinterpret_cast<const int*>(ibase + 8);
      int h = *reinterpret_cast<const int*>(ibase + 12);
      const uint32_t* pixels = *reinterpret_cast<const uint32_t* const*>(ibase + 32);

      if (w > 0 && h > 0 && pixels && w < 512 && h < 512) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        if (surf && cairo_surface_status(surf) == CAIRO_STATUS_SUCCESS) {
          unsigned char* dst = cairo_image_surface_get_data(surf);
          int stride = cairo_image_surface_get_stride(surf);
          for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
              uint32_t p = pixels[y * w + x];
              unsigned char a = (p >> 24) & 0xff;
              unsigned char r = (p >> 16) & 0xff;
              unsigned char g = (p >> 8) & 0xff;
              unsigned char b = p & 0xff;
              if (a > 0 && a < 255) {
                r = static_cast<unsigned char>((static_cast<int>(r) * a) / 255);
                g = static_cast<unsigned char>((static_cast<int>(g) * a) / 255);
                b = static_cast<unsigned char>((static_cast<int>(b) * a) / 255);
              }
              dst[y * stride + x * 4 + 0] = b;
              dst[y * stride + x * 4 + 1] = g;
              dst[y * stride + x * 4 + 2] = r;
              dst[y * stride + x * 4 + 3] = a;
            }
          }
          cairo_surface_mark_dirty(surf);

          if (targetPx > 0 && (w > targetPx || h > targetPx || w < targetPx * 0.5 || h < targetPx * 0.5)) {
            int tw = std::max(1, targetPx);
            int th = std::max(1, targetPx * h / w);
            if (th < 1) th = 1;
            cairo_surface_t* scaled = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, th);
            if (scaled && cairo_surface_status(scaled) == CAIRO_STATUS_SUCCESS) {
              cairo_t* cr = cairo_create(scaled);
              cairo_scale(cr, static_cast<double>(tw) / w, static_cast<double>(th) / h);
              cairo_set_source_surface(cr, surf, 0, 0);
              cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
              cairo_paint(cr);
              cairo_destroy(cr);
              cairo_surface_destroy(surf);
              surf = scaled;
            } else {
              if (scaled) cairo_surface_destroy(scaled);
            }
          }
          result = surf;
        } else {
          if (surf) cairo_surface_destroy(surf);
        }
      }
    }
  }

  fnDestroy(images);
  return result;
}

} // namespace

std::vector<CursorThemeInfo> list_installed_cursor_themes() {
   
  const auto bases = icon_base_dirs();
  std::unordered_map<std::string, CursorThemeInfo> byId;

  for (const auto& base : bases) {
    DIR* d = opendir(base.c_str());
    if (!d) continue;
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      const std::string id(ent->d_name);
      if (id.empty() || id == "." || id == "..") continue;
      if (byId.contains(id)) continue;
      const std::string themeDir = base + "/" + id;
      if (!file_exists(themeDir + "/cursors")) continue;
      CursorThemeInfo info{};
      info.id = id;
      info.path = themeDir;
      const std::string idx = themeDir + "/index.theme";
      info.name = file_exists(idx) ? read_ini_kv(idx, "[Icon Theme]", "Name") : std::string{};
      if (info.name.empty()) info.name = id;
      byId[id] = info;
    }
    closedir(d);
  }

  std::vector<CursorThemeInfo> out;
  out.reserve(byId.size());
  for (auto& [_, v] : byId) out.push_back(v);
  std::sort(out.begin(), out.end(),
            [](const CursorThemeInfo& a, const CursorThemeInfo& b) { return a.name < b.name; });
  return out;
}

cairo_surface_t* load_cursor_shape_surface(const std::string& cursorThemePath,
                                           const std::string& cursorName, int targetPx) {
   
  if (cursorThemePath.empty()) return nullptr;
  std::vector<const char*> names;
  if (cursorName == "left_ptr")       names = {"left_ptr", "default", "arrow"};
  else if (cursorName == "hand2")     names = {"hand2", "hand1", "pointer", "hand", "openhand"};
  else if (cursorName == "xterm")     names = {"xterm", "text", "ibeam"};
  else if (cursorName == "crosshair") names = {"crosshair", "cross", "cross_reverse", "diamond_cross"};
  else if (cursorName == "watch")     names = {"watch", "wait", "left_ptr_watch", "half-busy"};
  else if (cursorName == "fleur")     names = {"fleur", "move", "all-scroll", "grabbing", "grab"};
  else if (cursorName == "no-drop")   names = {"no-drop", "not-allowed", "crossed_circle", "forbidden"};
  else if (cursorName == "help")      names = {"help", "question_arrow", "left_ptr_help", "whats_this"};
  else names = {cursorName.c_str()};

  for (const char* name : names) {
    std::string p = cursorThemePath + "/cursors/" + name;
    if (file_exists(p)) {
      cairo_surface_t* s = xcursor_file_to_surface(p, targetPx);
      if (s) return s;
    }
  }
  return nullptr;
}

} // namespace eh::theming
