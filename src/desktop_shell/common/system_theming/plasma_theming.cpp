#include "desktop_shell/common/system_theming/system_theming.hpp"
#include "desktop_shell/common/system_theming/system_theming_utils.hpp"

#include "desktop_shell/common/fs/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace eh::theming {

using eh::shell::str::file_exists;
using eh::theming::detail::read_ini_kv;

namespace {

static std::string plasma_color_scheme_path(const std::string& themeDir) {
   
  // Check for bundled .colors files in the theme's colors/ dir
  const std::string colorsDir = themeDir + "/colors";
  DIR* d = opendir(colorsDir.c_str());
  if (d) {
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      const std::string name(ent->d_name);
      if (name.size() < 7) continue;
      if (name.compare(name.size() - 7, 7, ".colors") != 0) continue;
      closedir(d);
      return colorsDir + "/" + name;
    }
    closedir(d);
  }

  // Try to find a matching .colors file in standard color-scheme directories
  const std::string metaPath = themeDir + "/metadata.desktop";
  std::string themeName;
  if (file_exists(metaPath)) {
    themeName = read_ini_kv(metaPath, "[Desktop Entry]", "Name");
  }
  if (themeName.empty()) {
    // fallback: use directory name
    const size_t sl = themeDir.rfind('/');
    themeName = (sl != std::string::npos) ? themeDir.substr(sl + 1) : themeDir;
  }

  const char* home = std::getenv("HOME");
  std::vector<std::string> searchDirs;
  if (home) {
    searchDirs.push_back(std::string(home) + "/.local/share/color-schemes");
    searchDirs.push_back(std::string(home) + "/.local/share/flatpak/exports/share/color-schemes");
  }
  searchDirs.push_back("/usr/share/color-schemes");
  searchDirs.push_back("/usr/local/share/color-schemes");
  searchDirs.push_back("/var/lib/flatpak/exports/share/color-schemes");

  // Try exact name match first, then normalize
  auto try_match = [&](const std::string& dir) -> std::string {
    DIR* d2 = opendir(dir.c_str());
    if (!d2) return {};
    std::string found;
    for (dirent* ent2 = readdir(d2); ent2; ent2 = readdir(d2)) {
      const std::string fn(ent2->d_name);
      if (fn.size() < 7 || fn.compare(fn.size() - 7, 7, ".colors") != 0) continue;
      const std::string base = fn.substr(0, fn.size() - 7);
      if (base == themeName) {
        found = dir + "/" + fn;
        break;
      }
      // Also match without spaces/hyphens
      std::string normal;
      for (char c : themeName) {
        if (c != ' ' && c != '-') normal += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      std::string fnNormal;
      for (char c : base) {
        if (c != ' ' && c != '-') fnNormal += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (normal == fnNormal) {
        found = dir + "/" + fn;
        break;
      }
    }
    closedir(d2);
    return found;
  };

  for (const auto& dir : searchDirs) {
    std::string found = try_match(dir);
    if (!found.empty()) return found;
  }

  return {};
}

static std::string find_plasma_theme_colors_file(const std::string& themePath) {
  return plasma_color_scheme_path(themePath);
}

static bool parse_kde_colors_rgb(const std::string& val, double& r, double& g, double& b) {
   
  // Expects "R,G,B" where each is 0-255
  int ri = 0, gi = 0, bi = 0;
  if (std::sscanf(val.c_str(), "%d,%d,%d", &ri, &gi, &bi) != 3) return false;
  r = std::clamp(ri / 255.0, 0.0, 1.0);
  g = std::clamp(gi / 255.0, 0.0, 1.0);
  b = std::clamp(bi / 255.0, 0.0, 1.0);
  return true;
}

} // namespace

std::vector<PlasmaThemeInfo> list_installed_plasma_themes() {
   
  std::unordered_map<std::string, PlasmaThemeInfo> byId;
  const char* home = std::getenv("HOME");

  std::vector<std::string> searchDirs;
  if (home) {
    searchDirs.push_back(std::string(home) + "/.local/share/plasma/desktoptheme");
  }
  searchDirs.push_back("/usr/share/plasma/desktoptheme");

  for (const auto& base : searchDirs) {
    DIR* d = opendir(base.c_str());
    if (!d) continue;
    for (dirent* ent = readdir(d); ent; ent = readdir(d)) {
      const std::string id(ent->d_name);
      if (id.empty() || id == "." || id == "..") continue;
      if (byId.contains(id)) continue;
      const std::string themeDir = base + "/" + id;
      const std::string metaPath = themeDir + "/metadata.desktop";
      if (!file_exists(metaPath)) continue;
      PlasmaThemeInfo info{};
      info.id = id;
      info.path = themeDir;
      info.name = read_ini_kv(metaPath, "[Desktop Entry]", "Name");
      if (info.name.empty()) info.name = id;
      byId[id] = info;
    }
    closedir(d);
  }

  std::vector<PlasmaThemeInfo> out;
  out.reserve(byId.size());
  for (auto& [id, info] : byId) out.push_back(std::move(info));
  return out;
}

bool apply_plasma_theme(const std::string& themeId) {
   
  if (themeId.empty()) return false;
  const char* home = std::getenv("HOME");
  if (!home) return false;

  // Find the theme directory
  std::vector<std::string> searchDirs;
  if (home) {
    searchDirs.push_back(std::string(home) + "/.local/share/plasma/desktoptheme");
  }
  searchDirs.push_back("/usr/share/plasma/desktoptheme");

  std::string themePath;
  for (const auto& base : searchDirs) {
    const std::string tp = base + "/" + themeId;
    if (file_exists(tp + "/metadata.desktop")) {
      themePath = tp;
      break;
    }
  }
  if (themePath.empty()) return false;

  // Step 1: write the platform theme name to plasmarc.
  {
    const std::string prPath = std::string(home) + "/.config/plasmarc";
    std::string existing;
    {
      FILE* ef = fopen(prPath.c_str(), "rb");
      if (ef) {
        fseek(ef, 0, SEEK_END);
        long sz = ftell(ef);
        if (sz > 0) {
          fseek(ef, 0, SEEK_SET);
          existing.resize(static_cast<size_t>(sz));
          (void)fread(&existing[0], 1, static_cast<size_t>(sz), ef);
        }
        fclose(ef);
      }
    }
    FILE* f = fopen(prPath.c_str(), "wb");
    if (f) {
      fprintf(f, "[Theme]\nname=%s\n", themeId.c_str());
      // Copy non-[Theme] sections from existing
      if (!existing.empty()) {
        bool inTheme = false;
        std::istringstream iss(existing);
        std::string line;
        while (std::getline(iss, line)) {
          if (line.rfind('[', 0) == 0) {
            inTheme = (line.find("[Theme]") != std::string::npos);
            if (!inTheme) fprintf(f, "%s\n", line.c_str());
          } else if (!inTheme) {
            fprintf(f, "%s\n", line.c_str());
          }
        }
      }
      fclose(f);
    }
  }

  // Step 2: find and apply the color scheme.
  std::string colorsPath = find_plasma_theme_colors_file(themePath);
  if (!colorsPath.empty()) {
    const size_t sl = colorsPath.rfind('/');
    const std::string fn = (sl != std::string::npos) ? colorsPath.substr(sl + 1) : colorsPath;
    const std::string schemeName = (fn.size() >= 7) ? fn.substr(0, fn.size() - 7) : fn;
    const std::string kgPath = std::string(home) + "/.config/kdeglobals";

    // Read existing kdeglobals to preserve non-color sections
    std::string existing;
    {
      FILE* ef = fopen(kgPath.c_str(), "rb");
      if (ef) {
        fseek(ef, 0, SEEK_END);
        long sz = ftell(ef);
        if (sz > 0) {
          fseek(ef, 0, SEEK_SET);
          existing.resize(static_cast<size_t>(sz));
          (void)fread(&existing[0], 1, static_cast<size_t>(sz), ef);
        }
        fclose(ef);
      }
    }

    // Read the .colors file to get all [Colors:*] sections
    std::string colorsContent;
    {
      FILE* cf = fopen(colorsPath.c_str(), "rb");
      if (cf) {
        fseek(cf, 0, SEEK_END);
        long sz = ftell(cf);
        if (sz > 0) {
          fseek(cf, 0, SEEK_SET);
          colorsContent.resize(static_cast<size_t>(sz));
          (void)fread(&colorsContent[0], 1, static_cast<size_t>(sz), cf);
        }
        fclose(cf);
      }
    }

    // Write kdeglobals.
    // The platform applies: (1) colors first, then (2) scheme name
    // This order matters so apps watching for the scheme change see the new colors
    FILE* f = fopen(kgPath.c_str(), "wb");
    if (!f) return false;

    // Write [General] with ColorScheme
    fprintf(f, "[General]\n");
    // Copy [General] entries from colors file (like Name=, Contrast=, etc.)
    // but skip the combined rgb entries and just write ColorScheme
    {
      std::istringstream cis(colorsContent);
      std::string cl;
      bool inGeneralColors = false;
      while (std::getline(cis, cl)) {
        while (!cl.empty() && (cl.back() == '\n' || cl.back() == '\r')) cl.pop_back();
        if (cl.rfind("[General]", 0) == 0) { inGeneralColors = true; continue; }
        if (cl.rfind('[', 0) == 0) { inGeneralColors = false; continue; }
        if (inGeneralColors) {
          if (cl.rfind("Name=", 0) != 0 && cl.rfind("ColorScheme=", 0) != 0) {
            fprintf(f, "%s\n", cl.c_str());
          }
        }
      }
    }
    fprintf(f, "ColorScheme=%s\n\n", schemeName.c_str());

    // Copy all [Colors:*] sections from the .colors file verbatim
    {
      std::istringstream cis(colorsContent);
      std::string cl;
      bool inColorsSection = false;
      while (std::getline(cis, cl)) {
        while (!cl.empty() && (cl.back() == '\n' || cl.back() == '\r')) cl.pop_back();
        if (cl.rfind("[Colors:", 0) == 0) {
          inColorsSection = true;
          fprintf(f, "%s\n", cl.c_str());
        } else if (inColorsSection) {
          if (cl.rfind('[', 0) == 0) {
            inColorsSection = false;
          } else {
            fprintf(f, "%s\n", cl.c_str());
          }
        }
      }
    }

    // Copy non-colors sections from the existing globals config (e.g. [KDE], [Icons])
    if (!existing.empty()) {
      std::istringstream eis(existing);
      std::string el;
      while (std::getline(eis, el)) {
        while (!el.empty() && (el.back() == '\n' || el.back() == '\r')) el.pop_back();
        if (el.rfind('[', 0) == 0) {
          // Skip [General] and [Colors:*] sections (we wrote those fresh)
          if (el.find("[General]") != std::string::npos) {
            // skip it
          } else if (el.find("[Colors:") != std::string::npos) {
            // skip this whole section
            while (std::getline(eis, el)) {
              while (!el.empty() && (el.back() == '\n' || el.back() == '\r')) el.pop_back();
              if (el.rfind('[', 0) == 0) break;
            }
          } else {
            fprintf(f, "%s\n", el.c_str());
            // Copy entries in this section
            while (std::getline(eis, el)) {
              while (!el.empty() && (el.back() == '\n' || el.back() == '\r')) el.pop_back();
              if (el.rfind('[', 0) == 0) { fprintf(f, "%s\n", el.c_str()); break; }
              fprintf(f, "%s\n", el.c_str());
            }
          }
        }
      }
    }
    fclose(f);
  }

  // Step 3: set the Qt style to the platform's standard theme.
  apply_qt_style("Breeze", true);

  return true;
}

GtkThemeColors extract_plasma_theme_colors(const std::string& themeDir) {
   
  GtkThemeColors out;
  if (themeDir.empty()) return out;

  std::string colorsPath = find_plasma_theme_colors_file(themeDir);
  if (colorsPath.empty()) return out;

  FILE* f = fopen(colorsPath.c_str(), "rb");
  if (!f) return out;

  // Parse the .colors file
  std::string currentSection;
  char lineBuf[512];
  double winBgR = 1, winBgG = 1, winBgB = 1;
  double winFgR = 0, winFgG = 0, winFgB = 0;
  double selBgR = 0.3, selBgG = 0.5, selBgB = 0.85;
  double viewBgR = 1, viewBgG = 1, viewBgB = 1;
  double btnBgR = 0.85, btnBgG = 0.85, btnBgB = 0.85;
  bool gotWindow = false, gotSelection = false, gotView = false, gotButton = false;

  while (std::fgets(lineBuf, sizeof(lineBuf), f)) {
    std::string line(lineBuf);
    // Trim trailing whitespace
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty()) continue;

    if (line[0] == '[') {
      currentSection = line;
      continue;
    }

    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    double r, g, b;
    if (!parse_kde_colors_rgb(val, r, g, b)) continue;

    if (currentSection == "[Colors:Window]") {
      if (key == "BackgroundNormal") { winBgR = r; winBgG = g; winBgB = b; gotWindow = true; }
      else if (key == "ForegroundNormal") { winFgR = r; winFgG = g; winFgB = b; }
    } else if (currentSection == "[Colors:Selection]") {
      if (key == "BackgroundNormal") { selBgR = r; selBgG = g; selBgB = b; gotSelection = true; }
    } else if (currentSection == "[Colors:View]") {
      if (key == "BackgroundNormal") { viewBgR = r; viewBgG = g; viewBgB = b; gotView = true; }
    } else if (currentSection == "[Colors:Button]") {
      if (key == "BackgroundNormal") { btnBgR = r; btnBgG = g; btnBgB = b; gotButton = true; }
    }
  }
  fclose(f);

  if (!gotWindow && !gotSelection && !gotView && !gotButton) return out;

  out.bgR = winBgR; out.bgG = winBgG; out.bgB = winBgB; out.bgA = 1.0;
  out.fgR = winFgR; out.fgG = winFgG; out.fgB = winFgB; out.fgA = 1.0;
  out.accentR = selBgR; out.accentG = selBgG; out.accentB = selBgB; out.accentA = 1.0;
  out.baseR = viewBgR; out.baseG = viewBgG; out.baseB = viewBgB; out.baseA = 1.0;
  out.textR = winFgR; out.textG = winFgG; out.textB = winFgB; out.textA = 1.0;
  out.headerBgR = btnBgR; out.headerBgG = btnBgG; out.headerBgB = btnBgB; out.headerBgA = 1.0;
  out.headerFgR = winFgR; out.headerFgG = winFgG; out.headerFgB = winFgB; out.headerFgA = 1.0;
  out.valid = true;

  return out;
}

} // namespace eh::theming
