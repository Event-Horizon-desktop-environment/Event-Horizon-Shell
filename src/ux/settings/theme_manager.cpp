#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "ux/settings/theme_data.hpp"
#include "ux/settings/theme_manager.hpp"
#include "ux/settings/settings_app_types.hpp"

namespace fs = std::filesystem;

std::string theme_directory() {
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    if (xdg[0]) return std::string(xdg) + "/event-horizon/themes";
  }
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/share/event-horizon/themes";
  return "/tmp/event-horizon/themes";
}

static bool write_theme_toml(const toml::table& tbl, const std::string& path) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  const std::string tmp = path + ".__thtmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# Event Horizon color theme\n\n";
    out << tbl;
    if (!out.good()) { out.close(); (void)fs::remove(tmp, ec); return false; }
    out.flush();
    if (!out.good()) { out.close(); (void)fs::remove(tmp, ec); return false; }
    out.close();
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    (void)fs::remove(tmp, ec);
    return false;
  }
  return true;
}

std::vector<UserTheme> scan_user_themes() {
  std::vector<UserTheme> result;
  const std::string dir = theme_directory();
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return result;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    const auto& p = entry.path();
    if (p.extension() != ".toml") continue;
    UserTheme ut;
    if (load_theme_from_file(p.string(), ut)) {
      result.push_back(std::move(ut));
    }
  }
  return result;
}

bool load_theme_from_file(const std::string& path, UserTheme& out) {
  try {
    toml::table tbl = toml::parse_file(path);
    if (auto s = tbl["name"].value<std::string>()) out.name = *s;
    if (auto s = tbl["source"].value<std::string>()) out.source = *s;
    if (auto s = tbl["variant"].value<std::string>()) out.variant = *s;
    out.filePath = path;

    auto read_cf = [&](const char* key, float& field) {
      if (auto d = tbl[key].value<double>()) field = static_cast<float>(*d);
    };
    read_cf("primary_r", out.palette.primaryR);
    read_cf("primary_g", out.palette.primaryG);
    read_cf("primary_b", out.palette.primaryB);
    read_cf("on_primary_r", out.palette.onPrimaryR);
    read_cf("on_primary_g", out.palette.onPrimaryG);
    read_cf("on_primary_b", out.palette.onPrimaryB);
    read_cf("secondary_r", out.palette.secondaryR);
    read_cf("secondary_g", out.palette.secondaryG);
    read_cf("secondary_b", out.palette.secondaryB);
    read_cf("on_secondary_r", out.palette.onSecondaryR);
    read_cf("on_secondary_g", out.palette.onSecondaryG);
    read_cf("on_secondary_b", out.palette.onSecondaryB);
    read_cf("tertiary_r", out.palette.tertiaryR);
    read_cf("tertiary_g", out.palette.tertiaryG);
    read_cf("tertiary_b", out.palette.tertiaryB);
    read_cf("on_tertiary_r", out.palette.onTertiaryR);
    read_cf("on_tertiary_g", out.palette.onTertiaryG);
    read_cf("on_tertiary_b", out.palette.onTertiaryB);
    read_cf("error_r", out.palette.errorR);
    read_cf("error_g", out.palette.errorG);
    read_cf("error_b", out.palette.errorB);
    read_cf("on_error_r", out.palette.onErrorR);
    read_cf("on_error_g", out.palette.onErrorG);
    read_cf("on_error_b", out.palette.onErrorB);
    read_cf("surface_r", out.palette.surfaceR);
    read_cf("surface_g", out.palette.surfaceG);
    read_cf("surface_b", out.palette.surfaceB);
    read_cf("on_surface_r", out.palette.onSurfaceR);
    read_cf("on_surface_g", out.palette.onSurfaceG);
    read_cf("on_surface_b", out.palette.onSurfaceB);
    read_cf("surface_variant_r", out.palette.surfaceVariantR);
    read_cf("surface_variant_g", out.palette.surfaceVariantG);
    read_cf("surface_variant_b", out.palette.surfaceVariantB);
    read_cf("on_surface_variant_r", out.palette.onSurfaceVariantR);
    read_cf("on_surface_variant_g", out.palette.onSurfaceVariantG);
    read_cf("on_surface_variant_b", out.palette.onSurfaceVariantB);
    read_cf("outline_r", out.palette.outlineR);
    read_cf("outline_g", out.palette.outlineG);
    read_cf("outline_b", out.palette.outlineB);
    read_cf("shadow_r", out.palette.shadowR);
    read_cf("shadow_g", out.palette.shadowG);
    read_cf("shadow_b", out.palette.shadowB);
    read_cf("hover_r", out.palette.hoverR);
    read_cf("hover_g", out.palette.hoverG);
    read_cf("hover_b", out.palette.hoverB);
    read_cf("on_hover_r", out.palette.onHoverR);
    read_cf("on_hover_g", out.palette.onHoverG);
    read_cf("on_hover_b", out.palette.onHoverB);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[theme_manager] failed to parse " << path << ": " << e.what() << "\n";
    return false;
  }
}

bool save_theme_to_file(const UserTheme& theme) {
  const std::string dir = theme_directory();
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string filename = theme.name;
  for (auto& c : filename) {
    if (c == '/' || c == '\\') c = '_';
  }
  if (filename.empty()) filename = "unnamed";
  const std::string path = dir + "/" + filename + ".toml";

  toml::table tbl;
  tbl.insert_or_assign("name", theme.name);
  tbl.insert_or_assign("source", theme.source.empty() ? std::string("custom") : theme.source);
  tbl.insert_or_assign("variant", theme.variant);

  auto w = [](float v) -> double { return static_cast<double>(v); };
  const auto& p = theme.palette;
  tbl.insert_or_assign("primary_r", w(p.primaryR));
  tbl.insert_or_assign("primary_g", w(p.primaryG));
  tbl.insert_or_assign("primary_b", w(p.primaryB));
  tbl.insert_or_assign("on_primary_r", w(p.onPrimaryR));
  tbl.insert_or_assign("on_primary_g", w(p.onPrimaryG));
  tbl.insert_or_assign("on_primary_b", w(p.onPrimaryB));
  tbl.insert_or_assign("secondary_r", w(p.secondaryR));
  tbl.insert_or_assign("secondary_g", w(p.secondaryG));
  tbl.insert_or_assign("secondary_b", w(p.secondaryB));
  tbl.insert_or_assign("on_secondary_r", w(p.onSecondaryR));
  tbl.insert_or_assign("on_secondary_g", w(p.onSecondaryG));
  tbl.insert_or_assign("on_secondary_b", w(p.onSecondaryB));
  tbl.insert_or_assign("tertiary_r", w(p.tertiaryR));
  tbl.insert_or_assign("tertiary_g", w(p.tertiaryG));
  tbl.insert_or_assign("tertiary_b", w(p.tertiaryB));
  tbl.insert_or_assign("on_tertiary_r", w(p.onTertiaryR));
  tbl.insert_or_assign("on_tertiary_g", w(p.onTertiaryG));
  tbl.insert_or_assign("on_tertiary_b", w(p.onTertiaryB));
  tbl.insert_or_assign("error_r", w(p.errorR));
  tbl.insert_or_assign("error_g", w(p.errorG));
  tbl.insert_or_assign("error_b", w(p.errorB));
  tbl.insert_or_assign("on_error_r", w(p.onErrorR));
  tbl.insert_or_assign("on_error_g", w(p.onErrorG));
  tbl.insert_or_assign("on_error_b", w(p.onErrorB));
  tbl.insert_or_assign("surface_r", w(p.surfaceR));
  tbl.insert_or_assign("surface_g", w(p.surfaceG));
  tbl.insert_or_assign("surface_b", w(p.surfaceB));
  tbl.insert_or_assign("on_surface_r", w(p.onSurfaceR));
  tbl.insert_or_assign("on_surface_g", w(p.onSurfaceG));
  tbl.insert_or_assign("on_surface_b", w(p.onSurfaceB));
  tbl.insert_or_assign("surface_variant_r", w(p.surfaceVariantR));
  tbl.insert_or_assign("surface_variant_g", w(p.surfaceVariantG));
  tbl.insert_or_assign("surface_variant_b", w(p.surfaceVariantB));
  tbl.insert_or_assign("on_surface_variant_r", w(p.onSurfaceVariantR));
  tbl.insert_or_assign("on_surface_variant_g", w(p.onSurfaceVariantG));
  tbl.insert_or_assign("on_surface_variant_b", w(p.onSurfaceVariantB));
  tbl.insert_or_assign("outline_r", w(p.outlineR));
  tbl.insert_or_assign("outline_g", w(p.outlineG));
  tbl.insert_or_assign("outline_b", w(p.outlineB));
  tbl.insert_or_assign("shadow_r", w(p.shadowR));
  tbl.insert_or_assign("shadow_g", w(p.shadowG));
  tbl.insert_or_assign("shadow_b", w(p.shadowB));
  tbl.insert_or_assign("hover_r", w(p.hoverR));
  tbl.insert_or_assign("hover_g", w(p.hoverG));
  tbl.insert_or_assign("hover_b", w(p.hoverB));
  tbl.insert_or_assign("on_hover_r", w(p.onHoverR));
  tbl.insert_or_assign("on_hover_g", w(p.onHoverG));
  tbl.insert_or_assign("on_hover_b", w(p.onHoverB));

  return write_theme_toml(tbl, path);
}

bool delete_theme_file(const std::string& path) {
  std::error_code ec;
  return fs::remove(path, ec);
}

void apply_theme_to_app(App& app, const eh::settings::theme::ThemePalette& p) {
  app.settings.themePrimaryR = p.primaryR;
  app.settings.themePrimaryG = p.primaryG;
  app.settings.themePrimaryB = p.primaryB;
  app.settings.themeOnPrimaryR = p.onPrimaryR;
  app.settings.themeOnPrimaryG = p.onPrimaryG;
  app.settings.themeOnPrimaryB = p.onPrimaryB;
  app.settings.themeSecondaryR = p.secondaryR;
  app.settings.themeSecondaryG = p.secondaryG;
  app.settings.themeSecondaryB = p.secondaryB;
  app.settings.themeOnSecondaryR = p.onSecondaryR;
  app.settings.themeOnSecondaryG = p.onSecondaryG;
  app.settings.themeOnSecondaryB = p.onSecondaryB;
  app.settings.themeTertiaryR = p.tertiaryR;
  app.settings.themeTertiaryG = p.tertiaryG;
  app.settings.themeTertiaryB = p.tertiaryB;
  app.settings.themeOnTertiaryR = p.onTertiaryR;
  app.settings.themeOnTertiaryG = p.onTertiaryG;
  app.settings.themeOnTertiaryB = p.onTertiaryB;
  app.settings.themeErrorR = p.errorR;
  app.settings.themeErrorG = p.errorG;
  app.settings.themeErrorB = p.errorB;
  app.settings.themeOnErrorR = p.onErrorR;
  app.settings.themeOnErrorG = p.onErrorG;
  app.settings.themeOnErrorB = p.onErrorB;
  app.settings.themeSurfaceR = p.surfaceR;
  app.settings.themeSurfaceG = p.surfaceG;
  app.settings.themeSurfaceB = p.surfaceB;
  app.settings.themeOnSurfaceR = p.onSurfaceR;
  app.settings.themeOnSurfaceG = p.onSurfaceG;
  app.settings.themeOnSurfaceB = p.onSurfaceB;
  app.settings.themeSurfaceVariantR = p.surfaceVariantR;
  app.settings.themeSurfaceVariantG = p.surfaceVariantG;
  app.settings.themeSurfaceVariantB = p.surfaceVariantB;
  app.settings.themeOnSurfaceVariantR = p.onSurfaceVariantR;
  app.settings.themeOnSurfaceVariantG = p.onSurfaceVariantG;
  app.settings.themeOnSurfaceVariantB = p.onSurfaceVariantB;
  app.settings.themeOutlineR = p.outlineR;
  app.settings.themeOutlineG = p.outlineG;
  app.settings.themeOutlineB = p.outlineB;
  app.settings.themeShadowR = p.shadowR;
  app.settings.themeShadowG = p.shadowG;
  app.settings.themeShadowB = p.shadowB;
  app.settings.themeHoverR = p.hoverR;
  app.settings.themeHoverG = p.hoverG;
  app.settings.themeHoverB = p.hoverB;
  app.settings.themeOnHoverR = p.onHoverR;
  app.settings.themeOnHoverG = p.onHoverG;
  app.settings.themeOnHoverB = p.onHoverB;
}
