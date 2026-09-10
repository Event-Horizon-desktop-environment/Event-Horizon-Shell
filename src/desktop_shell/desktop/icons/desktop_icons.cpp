#include "desktop_shell/desktop/icons/desktop_icons.hpp"

#include "desktop_shell/desktop/icons/desktop_icon_menu.hpp"
#include "desktop_shell/desktop/core/desktop_layer.hpp"
#include "desktop_shell/desktop/icons/desktop_menu.hpp"
#include "desktop_shell/desktop/core/desktop_pointer.hpp"
#include "desktop_shell/desktop/core/desktop_preferences.hpp"
#include "desktop_shell/desktop/core/desktop_app.hpp"

#include "desktop_shell/desktop/entries/desktop_xdg_ops.hpp"
#include "desktop_shell/desktop/entries/desktop_entries.hpp"
#include "desktop_shell/widgets/start_menu/start_menu.hpp"
#include "desktop_shell/widgets/app_drawer/list/desktop_list.hpp"
#include "wallpaper/thumbnail/wallpaper_thumbnail.hpp"
#include "services/udisks2/udisks2_drive_service.hpp"

#include "configuration/shell_config.hpp"

#include <cairo/cairo.h>

#include "desktop_shell/shared/core/cairo_helpers.hpp"
#include <wayland-client.h>

#include <algorithm>
#include <cctype>
#include <vector>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace eh::shell::desktop {

using eh::shell::shared::path_rounded_rect;

// Resolve a themed icon through the desktop's own IconCache. Stands in for the
// dock's `eh_app_drawer_resolve_catalog_icon` now that the desktop owns its own
// cache (pre-split it borrowed the dock's).
static const eh::icons::IconEntry* desktop_resolve_catalog_icon(DesktopApp& app,
                                                                const std::string& desktop_path,
                                                                const std::string& icon_key) {
  // kHiResIconPx: see icon_cache.hpp — avoids capping the raster below what
  // scaled/HiDPI or large desktop-icon-size settings actually need on
  // screen (fixes upscaled/blurry icons).
  const std::string stem = eh_app_drawer_desktop_stem_from_path(desktop_path);
  if (!stem.empty()) {
    if (const eh::icons::IconEntry* ic = app.iconCache.app_icon(stem, eh::icons::kHiResIconPx)) {
      if (ic->surface) return ic;
    }
  }
  if (const eh::icons::IconEntry* ic = app.iconCache.tray_icon(icon_key)) {
    if (ic->surface) return ic;
  }
  if (!icon_key.empty() && icon_key != stem) {
    if (const eh::icons::IconEntry* ic = app.iconCache.app_icon(icon_key, eh::icons::kHiResIconPx)) {
      if (ic->surface) return ic;
    }
  }
  return nullptr;
}

namespace file_icon_cache {

constexpr std::size_t kMaxRasterThumbsPerDock = 48;

struct RasterSlot {
  cairo_surface_t* surf = nullptr;
  std::list<std::string>::iterator lru_it{};
};

struct DockRasterCache {
  std::unordered_map<std::string, RasterSlot> entries;
  std::list<std::string> lru;

  void evict_oldest() {
    if (lru.empty()) return;
    std::string key = std::move(lru.front());
    lru.pop_front();
    const auto it = entries.find(key);
    if (it == entries.end()) return;
    if (it->second.surf) cairo_surface_destroy(it->second.surf);
    entries.erase(it);
  }
};

std::unordered_map<const DesktopApp*, DockRasterCache> g_thumbs;

void purge(const DesktopApp* dock) {
  auto it = g_thumbs.find(dock);
  if (it == g_thumbs.end()) return;
  for (auto& kv : it->second.entries)
    if (kv.second.surf) cairo_surface_destroy(kv.second.surf);
  g_thumbs.erase(it);
}

}

namespace {

static std::string lower_ext(const fs::path& p) {
  std::string ext = p.extension().string();
  for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

static std::string icon_key_for_file_path(const fs::path& p) {
  const std::string ext = lower_ext(p);
  if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp" ||
      ext == ".tif" || ext == ".tiff" || ext == ".svg" || ext == ".ico" || ext == ".avif" || ext == ".heic" ||
      ext == ".jxl")
    return "image-x-generic";
  if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".webm" || ext == ".mov" || ext == ".wmv" ||
      ext == ".mpeg" || ext == ".mpg" || ext == ".m4v" || ext == ".flv" || ext == ".ogv" || ext == ".3gp" ||
      ext == ".ts")
    return "video-x-generic";
  if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg" || ext == ".opus" || ext == ".m4a" ||
      ext == ".aac" || ext == ".wma")
    return "audio-x-generic";
  if (ext == ".pdf") return "application-pdf";
  if (ext == ".zip" || ext == ".7z" || ext == ".tar" || ext == ".tgz" || ext == ".tbz2" || ext == ".tbz" ||
      ext == ".txz" || ext == ".tzst" || ext == ".gz" || ext == ".xz" || ext == ".bz2" || ext == ".rar" ||
      ext == ".zst" || ext == ".cab")
    return "package-x-generic";
  if (ext == ".txt" || ext == ".md" || ext == ".csv" || ext == ".json" || ext == ".toml" || ext == ".xml" ||
      ext == ".log")
    return "text-x-generic";
  return "inode-x-generic";
}

static bool file_wants_raster_thumbnail(const fs::path& p) {
  const std::string ext = lower_ext(p);
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp" || ext == ".gif";
}

static cairo_surface_t* cached_raster_thumb(DesktopApp& app, const std::string& abs_path, int max_px) {
  auto& dc = file_icon_cache::g_thumbs[&app];
  if (auto it = dc.entries.find(abs_path); it != dc.entries.end()) {
    dc.lru.erase(it->second.lru_it);
    dc.lru.push_back(abs_path);
    it->second.lru_it = std::prev(dc.lru.end());
    return it->second.surf;
  }
  cairo_surface_t* s = eh::wallpaper::load_thumbnail(abs_path, max_px);
  if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) return nullptr;

  while (dc.entries.size() >= file_icon_cache::kMaxRasterThumbsPerDock) dc.evict_oldest();

  dc.lru.push_back(abs_path);
  file_icon_cache::RasterSlot slot{s, std::prev(dc.lru.end())};
  dc.entries.emplace(abs_path, std::move(slot));
  return s;
}

static void paint_icon_surface_scaled(cairo_t* cr, double ix, double iy, double box, cairo_surface_t* surf) {
  cairo_save(cr);
  cairo_translate(cr, ix, iy);
  const double iw = static_cast<double>(cairo_image_surface_get_width(surf));
  const double ih = static_cast<double>(cairo_image_surface_get_height(surf));
  const double sc = box / std::max(1.0, std::max(iw, ih));
  cairo_scale(cr, sc, sc);
  cairo_set_source_surface(cr, surf, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

static bool env_disables_desktop_icons() {
  const char* e = std::getenv("EH_DISABLE_DESKTOP_ICONS");
  return e && e[0] == '1';
}

static bool icons_visible(DesktopApp& app) {
  if (env_disables_desktop_icons()) return false;
  return app.desktopIconsVisible;
}

static bool icons_visible_query(const DesktopApp& app) {
  if (env_disables_desktop_icons()) return false;
  return app.desktopIconsVisible;
}

std::string trim(std::string_view s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
  return std::string(s.substr(a, b - a));
}

std::string desktop_user_dir() {
  if (const char* d = std::getenv("XDG_DESKTOP_DIR")) {
    if (*d) return std::string(d);
  }
  if (const char* h = std::getenv("HOME")) {
    return std::string(h) + "/Desktop";
  }
  return {};
}

std::string strip_exec_field_codes(std::string exec) {
  std::string out;
  out.reserve(exec.size());
  for (size_t i = 0; i < exec.size(); i++) {
    if (exec[i] == '%' && i + 1 < exec.size()) {
      if (exec[i + 1] == '%') out.push_back('%');
      i++;
      continue;
    }
    out.push_back(exec[i]);
  }
  return trim(out);
}

struct ParsedDesktop {
  std::string name;
  std::string exec;
  std::string icon;
  bool no_display = false;
  bool hidden = false;
  bool terminal = false;
  std::string type;
};

bool parse_desktop_file(const fs::path& path, ParsedDesktop* out) {
  std::ifstream f(path);
  if (!f) return false;
  bool in_entry = false;
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line[0] == '[') {
      in_entry = (line == "[Desktop Entry]");
      continue;
    }
    if (!in_entry) continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq));
    std::string val = trim(line.substr(eq + 1));
    for (auto& c : key)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (key == "name") out->name = val;
    else if (key == "exec") out->exec = val;
    else if (key == "icon") out->icon = val;
    else if (key == "nodisplay") out->no_display = (val == "true" || val == "1");
    else if (key == "hidden") out->hidden = (val == "true" || val == "1");
    else if (key == "terminal") out->terminal = (val == "true" || val == "1");
    else if (key == "type") {
      for (auto& c : val) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      out->type = val;
    }
  }
  if (out->type.empty()) out->type = "application";
  return true;
}

constexpr double kMargin = 20.0;
constexpr size_t kPrimaryLayerIndex = 0;
constexpr double kDragThresholdPx = 6.0;

IconLayoutMetrics metrics(const DesktopApp& app) { return desktop_icon_metrics_for(app.iconSizeKind); }

int grid_cols(const DesktopApp& app, int w) {
  const IconLayoutMetrics m = metrics(app);
  const int usable = std::max(0, w - static_cast<int>(kMargin * 2));
  return std::max(1, static_cast<int>(std::floor(static_cast<double>(usable) / m.cell_w)));
}

void merge_grid_from_prefs(DesktopApp& app) {
  for (auto& ic : app.icons) {
    const auto it = app.iconGridByPath.find(ic.desktop_path);
    if (it != app.iconGridByPath.end()) {
      ic.grid_col = it->second.col;
      ic.grid_row = it->second.row;
      ic.layer = it->second.layer;
    } else {
      ic.grid_col = -1;
      ic.grid_row = -1;
      ic.layer = 0;
    }
  }
}

void assign_free_grid_slots(DesktopApp& app, size_t layer_idx, int w, int h) {
  const auto& sc = eh::config::shell_config_snapshot();
  const auto& dockCfg = eh::config::shell_config_snapshot().dock;
  const int dockReserve = (dockCfg.dockShowDock && !dockCfg.dockAutoHide) ? dock_effective_bar_height_px(dockCfg) : 0;
  const int taskbarReserve = sc.taskbar.enabled ? sc.taskbar.height : 0;
  const IconLayoutMetrics m = metrics(app);
  const int availableH = std::max(1, h - static_cast<int>(kMargin) - dockReserve - taskbarReserve);
  const int numRows = std::max(1, availableH / static_cast<int>(m.cell_h));
  const int maxRow = numRows - 1;
  const int cols = grid_cols(app, w);
  std::set<std::pair<int, int>> occ;
  for (const auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    if (ic.grid_col >= 0 && ic.grid_row >= 0) occ.insert({ic.grid_col, ic.grid_row});
  }
  for (auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    if (ic.grid_col >= 0 && ic.grid_row >= 0) continue;
    bool placed = false;
    for (int row = 0; !placed && row <= maxRow; ++row) {
      for (int col = 0; col < cols; ++col) {
        const auto key = std::make_pair(col, row);
        if (!occ.count(key)) {
          ic.grid_col = col;
          ic.grid_row = row;
          occ.insert(key);
          placed = true;
          break;
        }
      }
    }
  }
}

void resolve_grid_collisions(DesktopApp& app, size_t layer_idx, int w, int h) {
  const auto& sc = eh::config::shell_config_snapshot();
  const auto& dockCfg = eh::config::shell_config_snapshot().dock;
  const int dockReserve = (dockCfg.dockShowDock && !dockCfg.dockAutoHide) ? dock_effective_bar_height_px(dockCfg) : 0;
  const int taskbarReserve = sc.taskbar.enabled ? sc.taskbar.height : 0;
  const IconLayoutMetrics m = metrics(app);
  const int availableH = std::max(1, h - static_cast<int>(kMargin) - dockReserve - taskbarReserve);
  const int numRows = std::max(1, availableH / static_cast<int>(m.cell_h));
  const int maxRow = numRows - 1;
  const int cols = grid_cols(app, w);
  std::set<std::pair<int, int>> seen;
  for (auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    ic.grid_col = std::clamp(ic.grid_col, 0, cols - 1);
    ic.grid_row = std::clamp(ic.grid_row, 0, maxRow);
    auto key = std::make_pair(ic.grid_col, ic.grid_row);
    while (seen.count(key)) {
      if (ic.grid_row >= maxRow) break;
      ic.grid_row++;
      key = {ic.grid_col, ic.grid_row};
    }
    seen.insert(key);
  }
}

void apply_grid_pixels(DesktopApp& app, size_t layer_idx, int, int) {
  const IconLayoutMetrics m = metrics(app);
  for (auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    ic.cell_x = kMargin + static_cast<double>(ic.grid_col) * m.cell_w;
    ic.cell_y = kMargin + static_cast<double>(ic.grid_row) * m.cell_h;
  }
}

void sort_icons(DesktopApp& app) {
  switch (app.iconSortMode) {
    case DesktopIconSort::Type:
      std::sort(app.icons.begin(), app.icons.end(),
                [](const DesktopIconItem& a, const DesktopIconItem& b) {
                  if (a.layer != b.layer) return a.layer < b.layer;
                  if (a.kind != b.kind) return a.kind < b.kind;
                  return a.name < b.name;
                });
      break;
    case DesktopIconSort::Name:
    default:
      std::sort(app.icons.begin(), app.icons.end(),
                [](const DesktopIconItem& a, const DesktopIconItem& b) {
                  if (a.layer != b.layer) return a.layer < b.layer;
                  return a.name < b.name;
                });
      break;
  }
}

void relayout_auto(DesktopApp& app, size_t layer_idx, int w, int h) {
  const IconLayoutMetrics m = metrics(app);
  const int cols = grid_cols(app, w);
  const auto& sc = eh::config::shell_config_snapshot();
  const auto& dockCfg = eh::config::shell_config_snapshot().dock;
  const int dockReserve = (dockCfg.dockShowDock && !dockCfg.dockAutoHide) ? dock_effective_bar_height_px(dockCfg) : 0;
  const int taskbarReserve = sc.taskbar.enabled ? sc.taskbar.height : 0;
  const int availableH = std::max(1, h - static_cast<int>(kMargin) - dockReserve - taskbarReserve);
  const int numRows = std::max(1, availableH / static_cast<int>(m.cell_h));
  const size_t maxIcons = static_cast<size_t>(std::max(1, cols * numRows));
  sort_icons(app);
  size_t pos = 0;
  for (auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    if (pos >= maxIcons) continue;
    const int col = static_cast<int>(pos % static_cast<size_t>(cols));
    const int row = static_cast<int>(pos / static_cast<size_t>(cols));
    ic.grid_col = col;
    ic.grid_row = row;
    ic.cell_x = kMargin + static_cast<double>(col) * m.cell_w;
    ic.cell_y = kMargin + static_cast<double>(row) * m.cell_h;
    pos++;
  }
  app.iconLayoutW[layer_idx] = w;
  app.iconLayoutH[layer_idx] = h;
}

void relayout_align(DesktopApp& app, size_t layer_idx, int w, int h) {
  sort_icons(app);
  assign_free_grid_slots(app, layer_idx, w, h);
  resolve_grid_collisions(app, layer_idx, w, h);
  apply_grid_pixels(app, layer_idx, w, h);
  app.iconLayoutW[layer_idx] = w;
  app.iconLayoutH[layer_idx] = h;
}

void assign_free_grid_slots_stacked(DesktopApp& app, size_t layer_idx, int w, int col, int h) {
  const auto& sc = eh::config::shell_config_snapshot();
  const auto& dockCfg = eh::config::shell_config_snapshot().dock;
  const int dockReserve = (dockCfg.dockShowDock && !dockCfg.dockAutoHide) ? dock_effective_bar_height_px(dockCfg) : 0;
  const int taskbarReserve = sc.taskbar.enabled ? sc.taskbar.height : 0;
  const IconLayoutMetrics m = metrics(app);
  const int availableH = std::max(1, h - static_cast<int>(kMargin) - dockReserve - taskbarReserve);
  const int numRows = std::max(1, availableH / static_cast<int>(m.cell_h));
  const int maxRow = numRows - 1;
  const int cols = grid_cols(app, w);
  const int start_col = (col < 0) ? cols - 1 : 0;
  const int end_col = (col < 0) ? -1 : cols;
  const int step = (col < 0) ? -1 : 1;
  std::set<std::pair<int, int>> occ;
  for (const auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    if (ic.grid_col >= 0 && ic.grid_row >= 0) occ.insert({ic.grid_col, ic.grid_row});
  }
  for (auto& ic : app.icons) {
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    if (ic.grid_col >= 0 && ic.grid_row >= 0) continue;
    bool placed = false;
    for (int c = start_col; !placed && c != end_col; c += step) {
      for (int r = 0; r <= maxRow; ++r) {
        const auto key = std::make_pair(c, r);
        if (!occ.count(key)) {
          ic.grid_col = c;
          ic.grid_row = r;
          occ.insert(key);
          placed = true;
          break;
        }
      }
    }
  }
}

void relayout_stacked_left(DesktopApp& app, size_t layer_idx, int w, int h) {
  sort_icons(app);
  assign_free_grid_slots_stacked(app, layer_idx, w, 0, h);
  resolve_grid_collisions(app, layer_idx, w, h);
  apply_grid_pixels(app, layer_idx, w, h);
  app.iconLayoutW[layer_idx] = w;
  app.iconLayoutH[layer_idx] = h;
}

void relayout_stacked_right(DesktopApp& app, size_t layer_idx, int w, int h) {
  sort_icons(app);
  assign_free_grid_slots_stacked(app, layer_idx, w, -1, h);
  resolve_grid_collisions(app, layer_idx, w, h);
  apply_grid_pixels(app, layer_idx, w, h);
  app.iconLayoutW[layer_idx] = w;
  app.iconLayoutH[layer_idx] = h;
}

void relayout(DesktopApp& app, size_t layer_idx, int w, int h) {
  if (layer_idx >= app.iconLayoutW.size()) {
    app.iconLayoutW.resize(layer_idx + 1, -1);
    app.iconLayoutH.resize(layer_idx + 1, -1);
  }
  switch (app.iconArrangement) {
    case DesktopIconArrangement::AutoArrange:
      relayout_auto(app, layer_idx, w, h);
      break;
    case DesktopIconArrangement::Stacked:
      relayout_stacked_left(app, layer_idx, w, h);
      break;
    case DesktopIconArrangement::StackedRight:
      relayout_stacked_right(app, layer_idx, w, h);
      break;
    case DesktopIconArrangement::AlignToGrid:
    default:
      relayout_align(app, layer_idx, w, h);
      break;
    }
}

void truncate_label(cairo_t* cr, const std::string& text, double max_w, std::string* out) {
  *out = text;
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, out->c_str(), &ex);
  if (ex.x_advance <= max_w) return;
  const std::string ell = "…";
  while (out->size() > 1) {
    out->pop_back();
    std::string try_s = *out + ell;
    cairo_text_extents(cr, try_s.c_str(), &ex);
    if (ex.x_advance <= max_w) {
      *out = std::move(try_s);
      return;
    }
  }
  *out = ell;
}

static std::string decode_mount_escapes(const std::string& s) {
  std::string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 3 < s.size()) {
      char d1 = s[i + 1], d2 = s[i + 2], d3 = s[i + 3];
      if (d1 >= '0' && d1 <= '7' && d2 >= '0' && d2 <= '7' && d3 >= '0' && d3 <= '7') {
        int c = (d1 - '0') * 64 + (d2 - '0') * 8 + (d3 - '0');
        r.push_back(static_cast<char>(c));
        i += 3;
        continue;
      }
    }
    r.push_back(s[i]);
  }
  return r;
}

static std::string drive_icon_for_device() {
  return "drive-harddisk";
}

static void add_drive_icons(DesktopApp& app) {
  std::unordered_map<std::string, eh::drives::DriveInfo> udisks_by_dev;
  try {
    auto& svc = eh::drives::UDisks2DriveService::instance();
    svc.start();
    for (const auto& di : svc.query_drives()) {
      udisks_by_dev[di.device] = di;
    }
  } catch (...) {
  }

  std::unordered_set<std::string> seen_devices;
  auto add_drive = [&](std::string device, std::string name, std::string mount_point, bool mounted,
                         std::string object_path = {}, std::string uuid = {}, std::string fstype = {}) {
    if (!seen_devices.insert(device).second) return;
    if (name.empty() || name[0] == '.') return;
    if (name == "swap") return;

    std::string canon;
    std::error_code ec;
    if (!mount_point.empty()) {
      canon = fs::weakly_canonical(mount_point, ec).string();
      if (canon.empty()) canon = fs::absolute(mount_point, ec).string();
      if (canon.empty()) return;
    }

    std::string ik = drive_icon_for_device();
#ifndef NDEBUG
    std::cerr << "[desktop][drives] device=\"" << device << "\" mount=\"" << canon << "\" mounted=" << mounted << "\n";
#endif
    DesktopIconItem it{};
    it.kind = DesktopIconKind::Drive;
    it.desktop_path = canon.empty() ? device : std::move(canon);
    it.name = std::move(name);
    it.icon_key = std::move(ik);
    it.grid_col = -1;
    it.grid_row = -1;
    it.drive_mounted = mounted;
    it.drive_object_path = std::move(object_path);
    it.drive_uuid = std::move(uuid);
    it.drive_fstype = std::move(fstype);
    app.icons.push_back(std::move(it));
  };

  std::unordered_set<std::string> skip_fstypes = {
    "proc", "sysfs", "tmpfs", "devtmpfs", "devpts", "cgroup", "cgroup2",
    "efivarfs", "fusectl", "securityfs", "selinuxfs", "bpf", "pstore",
    "debugfs", "tracefs", "hugetlbfs", "mqueue", "configfs", "autofs",
    "overlay", "squashfs", "rpc_pipefs", "sunrpc"
  };

  std::ifstream mounts("/proc/mounts");
  std::string line;
  while (std::getline(mounts, line)) {
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string device, mount_point_raw, fstype;
    iss >> device >> mount_point_raw >> fstype;
    std::string mount_point = decode_mount_escapes(mount_point_raw);
    if (device.empty() || mount_point.empty() || fstype.empty()) continue;
    if (device.size() < 6 || device[0] != '/' || device[1] != 'd' || device[2] != 'e' || device[3] != 'v')
      continue;
    if (mount_point == "/") continue;
    if (skip_fstypes.count(fstype)) continue;
    if (mount_point.find("/media/") != 0 && mount_point.find("/run/media/") != 0 && mount_point.find("/mnt/") != 0)
      continue;
    std::error_code ec;
    if (fs::is_directory(mount_point, ec)) {
      std::string name = fs::path(mount_point).filename().string();

      auto uit = udisks_by_dev.find(device);
      if (uit != udisks_by_dev.end()) {
        add_drive(device, std::move(name), mount_point, true,
                  uit->second.object_path, uit->second.id_uuid, uit->second.id_type);
      } else {
        add_drive(std::move(device), std::move(name), mount_point, true);
      }
    }
  }

  for (const auto& [dev, di] : udisks_by_dev) {
    if (di.mounted) continue;
    if (seen_devices.count(dev)) continue;
    add_drive(di.device, di.label, {}, false, di.object_path, di.id_uuid, di.id_type);
  }
}

static bool trash_has_files() {
  const char* home = std::getenv("HOME");
  if (!home) return false;
  fs::path trash_files = fs::path(home) / ".local/share/Trash/files";
  std::error_code ec;
  fs::directory_iterator it(trash_files, ec);
  if (ec) return false;
  for (; it != fs::directory_iterator(); ++it) {
    if (!ec && it->exists()) return true;
  }
  return false;
}

static void add_special_icons(DesktopApp& app) {
  const bool has_files = trash_has_files();

#ifndef NDEBUG
  std::cerr << "[desktop][special] trash_has_files=" << has_files << "\n";
#endif

  DesktopIconItem trash{};
  trash.kind = DesktopIconKind::Trash;
  trash.desktop_path = "@trash";
  trash.name = "Trash";
  trash.icon_key = has_files ? "user-trash-full" : "user-trash";
  trash.exec.clear();
  trash.grid_col = -1;
  trash.grid_row = -1;
  app.icons.push_back(std::move(trash));

  DesktopIconItem computer{};
  computer.kind = DesktopIconKind::MyComputer;
  computer.desktop_path = "@mycomputer";
  computer.name = "My Computer";
  computer.icon_key = "computer";
  computer.exec.clear();
  computer.grid_col = -1;
  computer.grid_row = -1;
  app.icons.push_back(std::move(computer));
}

void rescan(DesktopApp& app) {
  file_icon_cache::purge(&app);
  app.icons.clear();
  app.iconLayoutW.clear();
  app.iconLayoutH.clear();
  if (env_disables_desktop_icons()) return;

  const std::string dir = desktop_user_dir();
  if (dir.empty()) return;
  std::error_code ec;
  fs::path dir_path(dir);
  dir_path = fs::weakly_canonical(dir_path, ec);
  if (dir_path.empty() || !fs::is_directory(dir_path, ec)) return;

  enum class ScanKind : uint8_t { DesktopApp, File, Folder };
  struct ScanEntry {
    fs::path p;
    ScanKind kind;
  };
  std::vector<ScanEntry> entries;
  for (const auto& ent : fs::directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec)) {
    const fs::path p = ent.path();
    const std::string fname = p.filename().string();
    if (!fname.empty() && fname[0] == '.') continue;

    std::error_code tec;
    if (fs::is_directory(p, tec)) {
      entries.push_back({p, ScanKind::Folder});
      continue;
    }
    if (!fs::is_regular_file(p, tec)) continue;
    const std::string skip_ext = lower_ext(p);

    if (skip_ext == ".lnk" || skip_ext == ".ink") continue;
    if (p.extension() == ".desktop")
      entries.push_back({p, ScanKind::DesktopApp});
    else
      entries.push_back({p, ScanKind::File});
  }
  std::sort(entries.begin(), entries.end(),
            [](const ScanEntry& a, const ScanEntry& b) { return a.p.filename() < b.p.filename(); });

  for (const ScanEntry& se : entries) {
    const fs::path& p = se.p;
    if (se.kind == ScanKind::Folder) {
      DesktopIconItem it{};
      it.kind = DesktopIconKind::Folder;
      std::error_code ec2;
      it.desktop_path = fs::weakly_canonical(p, ec2).string();
      if (it.desktop_path.empty()) it.desktop_path = fs::absolute(p, ec2).string();
      it.name = p.filename().string();
      it.icon_key = "inode-directory";
      it.exec.clear();
      app.icons.push_back(std::move(it));
      continue;
    }
    if (se.kind == ScanKind::DesktopApp) {
      ParsedDesktop pd{};
      if (!parse_desktop_file(p, &pd)) continue;
      if (pd.type != "application") continue;
      if (pd.no_display || pd.hidden) continue;
      DesktopIconItem it{};
      it.kind = DesktopIconKind::Application;
      std::error_code ec2;
      it.desktop_path = fs::weakly_canonical(p, ec2).string();
      if (it.desktop_path.empty()) it.desktop_path = fs::absolute(p, ec2).string();
      it.name = pd.name.empty() ? p.stem().string() : pd.name;
      it.icon_key = pd.icon.empty() ? eh_app_drawer_desktop_stem_from_path(it.desktop_path) : pd.icon;
      std::string exec = strip_exec_field_codes(pd.exec);
      if (exec.empty()) continue;
      if (pd.terminal) exec = std::string("x-terminal-emulator -e ") + exec;
      it.exec = std::move(exec);
      app.icons.push_back(std::move(it));
    } else {
      DesktopIconItem it{};
      it.kind = DesktopIconKind::File;
      std::error_code ec2;
      it.desktop_path = fs::weakly_canonical(p, ec2).string();
      if (it.desktop_path.empty()) it.desktop_path = fs::absolute(p, ec2).string();
      it.name = p.filename().string();
      it.icon_key = icon_key_for_file_path(p);
      it.exec.clear();
      app.icons.push_back(std::move(it));
    }
  }
  add_drive_icons(app);
  add_special_icons(app);
  merge_grid_from_prefs(app);
}

static void ensure_monitoring_started(DesktopApp& app) {
  static bool monitoring_started = false;
  if (!monitoring_started) {
    monitoring_started = true;
    auto& svc = eh::drives::UDisks2DriveService::instance();
    svc.set_change_callback([&app]() {
      if ((app.wl ? &app : nullptr))
        desktop_icons_defer_rescan_after_background_io(app);
    });
    svc.start();
  }
}

void ensure_scanned(DesktopApp& app) {
  desktop_prefs_ensure_loaded(app);
  if (app.iconScanDone) return;
  app.iconScanDone = true;
  rescan(app);
  ensure_monitoring_started(app);
}

void paint_icon_item(DesktopApp& app, cairo_t* cr, const DesktopIconItem& ic, bool marquee_selected) {
  const IconLayoutMetrics m = metrics(app);
  const bool dim = ic.kind == DesktopIconKind::Drive && !ic.drive_mounted;

  const int icon_idx = static_cast<int>(&ic - app.icons.data());
  const bool hovered = app.iconHoverIdx == icon_idx && app.iconPressIdx < 0 && !app.iconDragging
                       && !app.desktopMenuOpen && !app.iconCtxMenuOpen;
  const bool pressed = app.iconPressIdx == icon_idx;

  if (hovered) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.25, 0.45, 0.85, 0.15);
    path_rounded_rect(cr, ic.cell_x + 1, ic.cell_y + 1, m.cell_w - 2, m.cell_h - 2, 6.0);
    cairo_fill(cr);
    cairo_restore(cr);
  }
  if (pressed || marquee_selected) {
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.22, 0.42, 0.82, 0.2);
    path_rounded_rect(cr, ic.cell_x + 1, ic.cell_y + 1, m.cell_w - 2, m.cell_h - 2, 6.0);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.4, 0.62, 0.98, 0.65);
    cairo_set_line_width(cr, 1.0);
    path_rounded_rect(cr, ic.cell_x + 1.5, ic.cell_y + 1.5, m.cell_w - 3, m.cell_h - 3, 5.5);
    cairo_stroke(cr);
    cairo_restore(cr);
  }
  const double ix = ic.cell_x + (m.cell_w - m.icon_sz) * 0.5;
  const double iy = ic.cell_y + 4.0;

  if (dim) {
    cairo_save(cr);
    const double dim_pad = m.icon_sz * 0.25 + 4;
    cairo_rectangle(cr, ix - dim_pad, iy - dim_pad, m.icon_sz + dim_pad * 2, m.icon_sz + dim_pad * 2);
    cairo_clip(cr);
    cairo_push_group(cr);
  }

  if (!app.iconDragging && !app.marqueeDragging && !app.marqueeVisible && !app.desktopMenuOpen && !app.iconCtxMenuOpen) {
    cairo_save(cr);
    const double scx = ix + m.icon_sz * 0.5;
    const double scy = iy + m.icon_sz * 0.55;
    const double sr = m.icon_sz * 0.7;
    cairo_rectangle(cr, scx - sr, scy - sr, sr * 2, sr * 2);
    cairo_clip(cr);
    cairo_pattern_t* pat = cairo_pattern_create_radial(scx, scy, 0, scx, scy, sr);
    cairo_pattern_add_color_stop_rgba(pat, 0, 0, 0, 0, 0.40);
    cairo_pattern_add_color_stop_rgba(pat, 0.5, 0, 0, 0, 0.20);
    cairo_pattern_add_color_stop_rgba(pat, 1, 0, 0, 0, 0);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
  }

  bool drew = false;
  if (ic.kind == DesktopIconKind::File) {
    const fs::path fp(ic.desktop_path);
    if (file_wants_raster_thumbnail(fp)) {
      const int thumbPx = std::clamp(static_cast<int>(std::lround(m.icon_sz * 1.25)), 32, 160);
      if (cairo_surface_t* thumb = cached_raster_thumb(app, ic.desktop_path, thumbPx)) {
        paint_icon_surface_scaled(cr, ix, iy, m.icon_sz, thumb);
        drew = true;
      }
    }
  }
  if (!drew) {

    const bool icon_by_key = ic.kind == DesktopIconKind::Folder || ic.kind == DesktopIconKind::Drive
                             || ic.kind == DesktopIconKind::Trash || ic.kind == DesktopIconKind::MyComputer;
    const eh::icons::IconEntry* ent = icon_by_key
                                          ? desktop_resolve_catalog_icon(app, std::string(), ic.icon_key)
                                          : desktop_resolve_catalog_icon(app, ic.desktop_path, ic.icon_key);
    if (ic.kind == DesktopIconKind::Drive || ic.kind == DesktopIconKind::Trash)
#ifndef NDEBUG
      std::cerr << "[desktop][icons] paint kind=" << static_cast<int>(ic.kind) << " icon_key=\"" << ic.icon_key << "\" found=" << (ent && ent->surface) << "\n";
#endif
    if ((!ent || !ent->surface) && icon_by_key)
      ent = desktop_resolve_catalog_icon(app, std::string(), std::string("drive-harddisk"));
    if (ent && ent->surface) {
      cairo_save(cr);
      cairo_translate(cr, ix, iy);
      const double iw = static_cast<double>(ent->width);
      const double ih = static_cast<double>(ent->height);
      const double sc = m.icon_sz / std::max(1.0, std::max(iw, ih));
      cairo_scale(cr, sc, sc);
      cairo_set_source_surface(cr, ent->surface, 0, 0);
      cairo_paint(cr);
      cairo_restore(cr);
    } else {
      cairo_save(cr);
      cairo_rectangle(cr, ix, iy, m.icon_sz, m.icon_sz);
      cairo_set_source_rgba(cr, 0.15, 0.18, 0.22, 0.55);
      cairo_fill(cr);
      cairo_set_font_size(cr, 20.0);
      cairo_set_source_rgba(cr, 0.9, 0.92, 0.95, 1.0);
      std::string init = ic.name.empty() ? "?" : std::string(1, ic.name[0]);
      cairo_move_to(cr, ix + 14.0, iy + 34.0);
      cairo_show_text(cr, init.c_str());
      cairo_restore(cr);
    }
  }

  if (dim) {
    cairo_pop_group_to_source(cr);
    cairo_paint_with_alpha(cr, 0.6);
    cairo_restore(cr);
  }

  cairo_set_font_size(cr, 11.0);
  const double maxLabelW = m.cell_w - 6.0;
  std::string shown;
  truncate_label(cr, ic.name, maxLabelW, &shown);
  cairo_text_extents_t ex{};
  cairo_text_extents(cr, shown.c_str(), &ex);
  const double lx = ic.cell_x + (m.cell_w - ex.x_advance) * 0.5;
  const double ly = ic.cell_y + m.cell_h - 10.0;
  if (!app.marqueeDragging && !app.marqueeVisible && !app.desktopMenuOpen && !app.iconCtxMenuOpen) {
    cairo_save(cr);
    cairo_move_to(cr, lx + 1.0, ly + 1.0);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.35);
    cairo_show_text(cr, shown.c_str());
    cairo_restore(cr);
  }
  cairo_move_to(cr, lx, ly);
  cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 1.0);
  cairo_show_text(cr, shown.c_str());
}

void convert_coords_to_layer(DesktopApp& app, size_t from_layer, size_t to_layer, double* x, double* y) {
  if (from_layer == to_layer || !x || !y) return;
  if (from_layer >= app.layers.size() || !app.layers[from_layer] ||
      to_layer >= app.layers.size() || !app.layers[to_layer]) return;
  if (!app.wl) return;
  DesktopLayer& src = *app.layers[from_layer];
  DesktopLayer& dst = *app.layers[to_layer];
  if (!src.wlOut || !dst.wlOut) return;
  const auto& bounds = app.wl->logical_output_bounds();
  const eh::wayland::LogicalOutputBounds* srcB = nullptr;
  const eh::wayland::LogicalOutputBounds* dstB = nullptr;
  for (const auto& b : bounds) {
    if (b.output == src.wlOut) srcB = &b;
    if (b.output == dst.wlOut) dstB = &b;
  }
  if (!srcB || !dstB) return;
  const double gx = static_cast<double>(srcB->global_x) + *x;
  const double gy = static_cast<double>(srcB->global_y) + *y;
  *x = gx - static_cast<double>(dstB->global_x);
  *y = gy - static_cast<double>(dstB->global_y);
}

void finish_icon_drag(DesktopApp& app, int w, int h, size_t target_layer_idx) {
  const int idx = app.iconGrabIdx;
  auto clear_group = [&app] {
    app.iconDragGroup.clear();
    app.iconDragGroupOffX.clear();
    app.iconDragGroupOffY.clear();
  };
  if (idx < 0 || static_cast<size_t>(idx) >= app.icons.size()) {
    app.iconDragging = false;
    app.iconGrabIdx = -1;
    app.iconPressIdx = -1;
    clear_group();
    return;
  }

  const size_t src_layer_idx = static_cast<size_t>(app.icons[idx].layer);

  if (src_layer_idx != target_layer_idx) {
    auto convert_one = [&](DesktopIconItem& ic) {
      convert_coords_to_layer(app, src_layer_idx, target_layer_idx, &ic.cell_x, &ic.cell_y);
      ic.layer = static_cast<int>(target_layer_idx);
    };
    convert_one(app.icons[idx]);
    if (!app.iconDragGroup.empty()) {
      for (int id : app.iconDragGroup) {
        if (id < 0 || static_cast<size_t>(id) >= app.icons.size()) continue;
        convert_one(app.icons[static_cast<size_t>(id)]);
      }
    }
  }

  if (!app.iconDragGroup.empty()) {
    const IconLayoutMetrics m = metrics(app);
    const int cols = grid_cols(app, w);
    for (int id : app.iconDragGroup) {
      if (id < 0 || static_cast<size_t>(id) >= app.icons.size()) continue;
      DesktopIconItem& ic = app.icons[static_cast<size_t>(id)];
      int ncol = static_cast<int>(std::lround((ic.cell_x - kMargin) / m.cell_w));
      int nrow = static_cast<int>(std::lround((ic.cell_y - kMargin) / m.cell_h));
      ncol = std::clamp(ncol, 0, cols - 1);
      nrow = std::max(0, nrow);
      ic.grid_col = ncol;
      ic.grid_row = nrow;
    }
    resolve_grid_collisions(app, target_layer_idx, w, h);
    apply_grid_pixels(app, target_layer_idx, w, h);
    app.iconDragging = false;
    app.iconGrabIdx = -1;
    app.iconPressIdx = -1;
    clear_group();
    desktop_prefs_save(app);
    return;
  }

  const IconLayoutMetrics m = metrics(app);
  const int cols = grid_cols(app, w);
  DesktopIconItem& ic = app.icons[static_cast<size_t>(idx)];

  int ncol = static_cast<int>(std::lround((ic.cell_x - kMargin) / m.cell_w));
  int nrow = static_cast<int>(std::lround((ic.cell_y - kMargin) / m.cell_h));
  ncol = std::clamp(ncol, 0, cols - 1);
  nrow = std::max(0, nrow);

  const int old_c = ic.grid_col;
  const int old_r = ic.grid_row;
  for (size_t j = 0; j < app.icons.size(); ++j) {
    if (static_cast<int>(j) == idx) continue;
    if (app.icons[j].grid_col == ncol && app.icons[j].grid_row == nrow) {
      app.icons[j].grid_col = old_c;
      app.icons[j].grid_row = old_r;
      break;
    }
  }
  ic.grid_col = ncol;
  ic.grid_row = nrow;
  resolve_grid_collisions(app, target_layer_idx, w, h);
  apply_grid_pixels(app, target_layer_idx, w, h);

  app.iconDragging = false;
  app.iconGrabIdx = -1;
  app.iconPressIdx = -1;
  clear_group();
  desktop_prefs_save(app);
}

void select_icons_in_rect_impl(DesktopApp& app, size_t layer_idx, double x0, double y0, double x1, double y1, int layer_w, int layer_h) {
  if (env_disables_desktop_icons()) return;
  ensure_scanned(app);
  const int lay_w = layer_idx < app.iconLayoutW.size() ? app.iconLayoutW[layer_idx] : -1;
  const int lay_h = layer_idx < app.iconLayoutH.size() ? app.iconLayoutH[layer_idx] : -1;
  if (layer_w > 0 && layer_h > 0 && (lay_w != layer_w || lay_h != layer_h)) relayout(app, layer_idx, layer_w, layer_h);
  const double rx0 = std::min(x0, x1);
  const double ry0 = std::min(y0, y1);
  const double rx1 = std::max(x0, x1);
  const double ry1 = std::max(y0, y1);
  const IconLayoutMetrics m = metrics(app);
  app.iconMarqueeSelection.clear();
  app.iconMarqueeSelection.reserve(app.icons.size());
  for (size_t i = 0; i < app.icons.size(); ++i) {
    const auto& ic = app.icons[i];
    if (ic.layer != static_cast<int>(layer_idx)) continue;
    const double ix1 = ic.cell_x + m.cell_w;
    const double iy1 = ic.cell_y + m.cell_h;
    if (ix1 < rx0 || ic.cell_x > rx1 || iy1 < ry0 || ic.cell_y > ry1) continue;
    app.iconMarqueeSelection.push_back(static_cast<int>(i));
  }
}

}

void desktop_icons_clear_marquee_selection(DesktopApp& app) {
   
  app.iconMarqueeSelection.clear();
}

void desktop_icons_select_in_rect(DesktopApp& app, size_t layer_idx, double x0, double y0, double x1, double y1, int layer_w, int layer_h) {
   
  select_icons_in_rect_impl(app, layer_idx, x0, y0, x1, y1, layer_w, layer_h);
}

void desktop_icons_copy_marquee_selection_to_clipboard(DesktopApp& app) {
   
  if (env_disables_desktop_icons()) return;
  std::vector<std::string> paths;
  for (int idx : app.iconMarqueeSelection) {
    if (idx < 0 || static_cast<size_t>(idx) >= app.icons.size()) continue;
    paths.push_back(app.icons[static_cast<size_t>(idx)].desktop_path);
  }
  if (paths.empty()) return;
  xdg::clipboard_files_cut_copy_multi(false, paths);
}

std::string desktop_icons_user_desktop_dir() {
   
  if (const char* e = std::getenv("XDG_DESKTOP_DIR"))
    if (*e) return std::string(e);
  if (const char* h = std::getenv("HOME")) return std::string(h) + "/Desktop";
  return {};
}

void desktop_icons_delete_marquee_selection(DesktopApp& app) {
   
  if (env_disables_desktop_icons()) return;
  if (app.iconMarqueeSelection.empty()) return;
  for (int idx : app.iconMarqueeSelection) {
    if (idx < 0 || static_cast<size_t>(idx) >= app.icons.size()) continue;
    (void)xdg::trash_file(app.icons[static_cast<size_t>(idx)].desktop_path);
  }
  app.iconMarqueeSelection.clear();
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_paste_from_clipboard(DesktopApp& app) {
   
  if (env_disables_desktop_icons()) return;
  const std::string dir = desktop_icons_user_desktop_dir();
  if (dir.empty()) return;
  std::vector<std::string> created;
  bool was_cut = false;
  if (!xdg::clipboard_paste_into_directory(dir, &created, &was_cut)) return;
  if (!was_cut && !created.empty())
    app.desktopUndoPasteDestPaths = std::move(created);
  else
    app.desktopUndoPasteDestPaths.clear();
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_undo_last_paste(DesktopApp& app) {
   
  if (env_disables_desktop_icons()) return;
  if (app.desktopUndoPasteDestPaths.empty()) return;
  std::vector<std::string> paths = std::move(app.desktopUndoPasteDestPaths);
  std::sort(paths.begin(), paths.end(),
            [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
  for (const std::string& p : paths) {
    std::error_code ec;
    fs::remove_all(p, ec);
  }
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_refresh_workspace(DesktopApp& app) {
   
  if (env_disables_desktop_icons()) return;
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_create_new_text_file(DesktopApp& app) {
  if (env_disables_desktop_icons()) return;
  const std::string dir = desktop_icons_user_desktop_dir();
  if (dir.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::path(dir), ec);
  const std::string base = "New Text Document.txt";
  fs::path path = fs::path(dir) / base;
  int n = 2;
  while (fs::exists(path, ec)) {
    path = fs::path(dir) / ("New Text Document (" + std::to_string(n) + ").txt");
    ++n;
  }
  {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
  }
  app.desktopUndoPasteDestPaths.clear();
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_create_new_folder(DesktopApp& app) {
  if (env_disables_desktop_icons()) return;
  const std::string dir = desktop_icons_user_desktop_dir();
  if (dir.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::path(dir), ec);
  const std::string base = "New Folder";
  fs::path path = fs::path(dir) / base;
  int n = 2;
  while (fs::exists(path, ec)) {
    path = fs::path(dir) / ("New Folder (" + std::to_string(n) + ")");
    ++n;
  }
  if (!fs::create_directory(path, ec) || ec) return;
  app.desktopUndoPasteDestPaths.clear();
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_clear(DesktopApp& app) {
  desktop_icon_menu_close(app);
  desktop_menu_close(app);
  file_icon_cache::purge(&app);
  app.icons.clear();
  app.iconLayoutW.clear();
  app.iconLayoutH.clear();
  app.iconScanDone = false;
  app.iconGrabIdx = -1;
  app.iconDragGroup.clear();
  app.iconDragGroupOffX.clear();
  app.iconDragGroupOffY.clear();
  app.iconDragging = false;
  app.iconPressIdx = -1;
  app.iconMarqueeSelection.clear();
  app.desktopDeferredRescanUntil.reset();
  app.desktopDeferredRescanLastTick.reset();
}

void desktop_icons_paint_layer(DesktopApp& app, size_t layer_index, int width, int height, cairo_t* cr) {
  if (!icons_visible(app)) return;
  if (width <= 0 || height <= 0) return;
  ensure_monitoring_started(app);
  if (!app.iconScanDone) return;
  if (app.icons.empty()) return;

  const int lay_w = layer_index < app.iconLayoutW.size() ? app.iconLayoutW[layer_index] : -1;
  const int lay_h = layer_index < app.iconLayoutH.size() ? app.iconLayoutH[layer_index] : -1;
  if (width != lay_w || height != lay_h) relayout(app, layer_index, width, height);

  cairo_select_font_face(cr, "Inter", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

  auto is_marquee_sel = [&app](int idx) {
    return std::find(app.iconMarqueeSelection.begin(), app.iconMarqueeSelection.end(), idx) != app.iconMarqueeSelection.end();
  };
  auto in_drag_group = [&app](int i) {
    if (app.iconDragGroup.empty()) return false;
    return std::find(app.iconDragGroup.begin(), app.iconDragGroup.end(), i) != app.iconDragGroup.end();
  };

  for (size_t i = 0; i < app.icons.size(); ++i) {
    if (app.icons[i].layer != static_cast<int>(layer_index)) continue;
    if (app.iconDragging && in_drag_group(static_cast<int>(i))) continue;
    paint_icon_item(app, cr, app.icons[i], is_marquee_sel(static_cast<int>(i)));
  }
  if (app.iconDragging && !app.iconDragGroup.empty()) {
    for (int id : app.iconDragGroup) {
      if (id >= 0 && static_cast<size_t>(id) < app.icons.size() &&
          app.icons[static_cast<size_t>(id)].layer == static_cast<int>(layer_index))
        paint_icon_item(app, cr, app.icons[static_cast<size_t>(id)], is_marquee_sel(id));
    }
  } else if (app.iconDragging && app.iconGrabIdx >= 0 && static_cast<size_t>(app.iconGrabIdx) < app.icons.size())
    paint_icon_item(app, cr, app.icons[static_cast<size_t>(app.iconGrabIdx)], is_marquee_sel(app.iconGrabIdx));
}

void desktop_icons_launch_by_index(DesktopApp& app, int idx) {
  if (!icons_visible(app)) return;
  ensure_scanned(app);
  if (idx < 0 || static_cast<size_t>(idx) >= app.icons.size()) return;
  const DesktopIconItem& ic = app.icons[static_cast<size_t>(idx)];
  std::cerr << "[launch] idx=" << idx << " kind=" << static_cast<int>(ic.kind) << " path=\"" << ic.desktop_path << "\" exec=\"" << ic.exec << "\"\n";
  if (ic.kind == DesktopIconKind::Trash) {
    const char* home = std::getenv("HOME");
    if (home) {
      fs::path trash_path = fs::path(home) / ".local/share/Trash";
      xdg::open_path_in_default_application(trash_path.string());
    }
    return;
  }
  if (ic.kind == DesktopIconKind::MyComputer) {
    xdg::open_uri("computer:///");
    return;
  }
  if (ic.kind == DesktopIconKind::File || ic.kind == DesktopIconKind::Folder || ic.kind == DesktopIconKind::Drive) {
    xdg::open_path_in_default_application(ic.desktop_path);
    return;
  }
  const auto info = read_desktop_entry_info(ic.desktop_path);
  xdg::open_desktop_default(ic.desktop_path, info, ic.exec);
}

void desktop_icons_reload_from_disk(DesktopApp& app) {
  app.iconScanDone = false;
  ensure_scanned(app);
}

void desktop_icons_defer_rescan_after_background_io(DesktopApp& app) {
  if (env_disables_desktop_icons()) return;
  using clock = std::chrono::steady_clock;
  app.desktopDeferredRescanUntil = clock::now() + std::chrono::milliseconds(4000);
  app.desktopDeferredRescanLastTick.reset();
}

void desktop_icons_poll_deferred_rescan(DesktopApp& app) {
  if (!app.desktopDeferredRescanUntil) return;
  if (env_disables_desktop_icons()) {
    app.desktopDeferredRescanUntil.reset();
    app.desktopDeferredRescanLastTick.reset();
    return;
  }
  using clock = std::chrono::steady_clock;
  const auto now = clock::now();
  if (now >= *app.desktopDeferredRescanUntil) {
    app.desktopDeferredRescanUntil.reset();
    app.desktopDeferredRescanLastTick.reset();
    return;
  }
  constexpr auto kMinGap = std::chrono::milliseconds(100);
  if (app.desktopDeferredRescanLastTick && now - *app.desktopDeferredRescanLastTick < kMinGap) return;
  app.desktopDeferredRescanLastTick = now;
  desktop_icons_reload_from_disk(app);
  paint_all_layers(app);
  if (app.display) wl_display_flush(app.display);
}

void desktop_icons_kick_initial_scan(DesktopApp& app) {
  if (env_disables_desktop_icons()) return;
  if (app.iconScanDone) return;
  desktop_prefs_ensure_loaded(app);
  ensure_monitoring_started(app);
  using clock = std::chrono::steady_clock;
  app.desktopDeferredRescanUntil = clock::now() + std::chrono::milliseconds(100);
  app.desktopDeferredRescanLastTick.reset();
}

bool desktop_icons_try_launch_at(DesktopApp& app, size_t layer_index, double x, double y) {
  if (!icons_visible(app)) return false;
  ensure_scanned(app);
  if (app.icons.empty()) return false;
  const IconLayoutMetrics m = metrics(app);

  for (size_t i = 0; i < app.icons.size(); ++i) {
    const auto& ic = app.icons[i];
    if (ic.layer != static_cast<int>(layer_index)) continue;
    if (x >= ic.cell_x && x < ic.cell_x + m.cell_w && y >= ic.cell_y && y < ic.cell_y + m.cell_h) {
#ifndef NDEBUG
      std::cerr << "[desktop][icons] launch name=\"" << ic.name << "\" path=\"" << ic.desktop_path << "\"\n";
#endif
      desktop_icons_launch_by_index(app, static_cast<int>(i));
      return true;
    }
  }
  return false;
}

bool desktop_icons_hit_index(const DesktopApp& app, size_t layer_index, double x, double y, int* out_idx) {
  if (!icons_visible_query(app) || !out_idx) return false;
  const IconLayoutMetrics m = metrics(app);
  for (size_t i = 0; i < app.icons.size(); ++i) {
    const auto& ic = app.icons[i];
    if (ic.layer != static_cast<int>(layer_index)) continue;
    if (x >= ic.cell_x && x < ic.cell_x + m.cell_w && y >= ic.cell_y && y < ic.cell_y + m.cell_h) {
      *out_idx = static_cast<int>(i);
      return true;
    }
  }
  return false;
}

bool desktop_icons_pointer_motion(DesktopApp& app) {
  if (!icons_visible(app)) return false;
  if (app.pointerLayerIdx >= app.layers.size() || !app.layers[app.pointerLayerIdx]) return false;

  if (app.iconDragging && app.iconGrabIdx >= 0 && static_cast<size_t>(app.iconGrabIdx) < app.icons.size()) {
    double lx = 0, ly = 0;
    if (!desktop_pointer_local_xy(app, app.iconGrabLayerIdx, &lx, &ly)) return false;
    DesktopIconItem& anc = app.icons[static_cast<size_t>(app.iconGrabIdx)];
    anc.cell_x = lx - app.iconGrabDx;
    anc.cell_y = ly - app.iconGrabDy;
    if (!app.iconDragGroup.empty()) {
      for (size_t k = 0; k < app.iconDragGroup.size(); ++k) {
        const int id = app.iconDragGroup[k];
        if (id == app.iconGrabIdx) continue;
        if (id < 0 || static_cast<size_t>(id) >= app.icons.size()) continue;
        if (k >= app.iconDragGroupOffX.size() || k >= app.iconDragGroupOffY.size()) continue;
        app.icons[static_cast<size_t>(id)].cell_x = anc.cell_x + app.iconDragGroupOffX[k];
        app.icons[static_cast<size_t>(id)].cell_y = anc.cell_y + app.iconDragGroupOffY[k];
      }
    }
    if (app.iconGrabLayerIdx < app.layers.size() && app.layers[app.iconGrabLayerIdx]) {
      paint_layer(app, *app.layers[app.iconGrabLayerIdx]);
    }
    if (app.pointerLayerIdx != app.iconGrabLayerIdx &&
        app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
      paint_layer(app, *app.layers[app.pointerLayerIdx]);
    }
    if (app.display) wl_display_flush(app.display);
    return true;
  }

  {
    int hit = -1;
    double hx = 0, hy = 0;
    if (desktop_pointer_local_xy(app, app.pointerLayerIdx, &hx, &hy))
      (void)desktop_icons_hit_index(app, app.pointerLayerIdx, hx, hy, &hit);
    if (hit != app.iconHoverIdx && app.iconPressIdx < 0 && !app.iconDragging && !app.desktopMenuOpen && !app.iconCtxMenuOpen) {
      app.iconHoverIdx = hit;
      if (app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
        paint_layer(app, *app.layers[app.pointerLayerIdx]);
        if (app.display) wl_display_flush(app.display);
      }
    }
  }
  if (app.iconArrangement != DesktopIconArrangement::AlignToGrid && app.iconArrangement != DesktopIconArrangement::Stacked && app.iconArrangement != DesktopIconArrangement::StackedRight) return false;
  if (app.iconPressIdx < 0) return false;
  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.iconGrabLayerIdx, &lx, &ly)) return false;
  const double dx = lx - app.iconPressX;
  const double dy = ly - app.iconPressY;
  if (dx * dx + dy * dy >= kDragThresholdPx * kDragThresholdPx) {
    app.iconDragging = true;
    app.iconGrabIdx = app.iconPressIdx;
    DesktopIconItem& ic = app.icons[static_cast<size_t>(app.iconGrabIdx)];
    app.iconGrabDx = lx - ic.cell_x;
    app.iconGrabDy = ly - ic.cell_y;
    app.iconDragGroup.clear();
    app.iconDragGroupOffX.clear();
    app.iconDragGroupOffY.clear();
    if (!app.iconMarqueeSelection.empty()) {
      app.iconDragGroup.reserve(app.iconMarqueeSelection.size());
      app.iconDragGroupOffX.reserve(app.iconMarqueeSelection.size());
      app.iconDragGroupOffY.reserve(app.iconMarqueeSelection.size());
      const bool anchor_in_sel =
          std::find(app.iconMarqueeSelection.begin(), app.iconMarqueeSelection.end(), app.iconGrabIdx) !=
          app.iconMarqueeSelection.end();
      if (anchor_in_sel && app.iconMarqueeSelection.size() > 1) {
        const double ax = ic.cell_x;
        const double ay = ic.cell_y;
        for (int gid : app.iconMarqueeSelection) {
          if (gid < 0 || static_cast<size_t>(gid) >= app.icons.size()) continue;
          app.iconDragGroup.push_back(gid);
          const DesktopIconItem& o = app.icons[static_cast<size_t>(gid)];
          app.iconDragGroupOffX.push_back(o.cell_x - ax);
          app.iconDragGroupOffY.push_back(o.cell_y - ay);
        }
      }
    }
    if (app.marqueeDragging) {
      app.marqueeDragging = false;
      app.marqueeVisible = false;
    }
    if (app.iconGrabLayerIdx < app.layers.size() && app.layers[app.iconGrabLayerIdx]) {
      paint_layer(app, *app.layers[app.iconGrabLayerIdx]);
      if (app.display) wl_display_flush(app.display);
    }
    return true;
  }
  return false;
}

bool desktop_icons_left_release(DesktopApp& app) {
  if (!icons_visible(app)) return false;
  if (app.iconDragging && app.iconGrabIdx >= 0 && app.pointerLayerIdx < app.layers.size() && app.layers[app.pointerLayerIdx]) {
    DesktopLayer& targetL = *app.layers[app.pointerLayerIdx];
    finish_icon_drag(app, targetL.configuredWidth, targetL.configuredHeight, app.pointerLayerIdx);
    paint_layer(app, targetL);
    if (app.iconGrabLayerIdx != app.pointerLayerIdx &&
        app.iconGrabLayerIdx < app.layers.size() && app.layers[app.iconGrabLayerIdx]) {
      paint_layer(app, *app.layers[app.iconGrabLayerIdx]);
    }
    if (app.display) wl_display_flush(app.display);
    return true;
  }

  if (app.iconPressIdx >= 0) {
    double lx = 0, ly = 0;
    bool have = desktop_pointer_local_xy(app, app.iconGrabLayerIdx, &lx, &ly);
    const double dx = have ? (lx - app.iconPressX) : 0;
    const double dy = have ? (ly - app.iconPressY) : 0;
    const bool moved = (dx * dx + dy * dy >= kDragThresholdPx * kDragThresholdPx);
    app.iconPressIdx = -1;
    if (!moved && have) {
      int hit_idx = -1;
      (void)desktop_icons_hit_index(app, app.iconGrabLayerIdx, lx, ly, &hit_idx);
      using clock = std::chrono::steady_clock;
      const auto now = clock::now();
      const bool dbl = app.iconLastClickTime.has_value() &&
                       app.iconLastClickIdx >= 0 && app.iconLastClickIdx == hit_idx &&
                       now - *app.iconLastClickTime < std::chrono::milliseconds(400);
      std::cerr << "[dc] first=" << (app.iconLastClickTime.has_value() ? "y" : "n")
                << " last=" << app.iconLastClickIdx << " hit=" << hit_idx
                << " dbl=" << dbl << "\n";
      app.iconLastClickTime = now;
      app.iconLastClickIdx = hit_idx;
      if (dbl && desktop_icons_try_launch_at(app, app.iconGrabLayerIdx, lx, ly)) return true;
    }
    return true;
  }
  return false;
}

bool desktop_icons_left_press(DesktopApp& app) {
  if (!icons_visible(app)) return false;
  ensure_scanned(app);
  if (app.icons.empty()) return false;

  double lx = 0, ly = 0;
  if (!desktop_pointer_local_xy(app, app.pointerLayerIdx, &lx, &ly)) return false;
  int idx = -1;
  if (!desktop_icons_hit_index(app, app.pointerLayerIdx, lx, ly, &idx)) {
    app.iconLastClickTime.reset();
    app.iconLastClickIdx = -1;
    return false;
  }

  if (!app.iconMarqueeSelection.empty()) {
    if (std::find(app.iconMarqueeSelection.begin(), app.iconMarqueeSelection.end(), idx) == app.iconMarqueeSelection.end())
      app.iconMarqueeSelection.clear();
  }

  app.iconHoverIdx = -1;
  if (app.iconLastClickIdx != idx) {
    app.iconLastClickTime.reset();
    app.iconLastClickIdx = -1;
  }
  app.iconPressIdx = idx;
  app.iconPressX = lx;
  app.iconPressY = ly;
  app.iconGrabLayerIdx = app.pointerLayerIdx;
  return true;
}

void desktop_workspace_right_press(DesktopApp& app) {
  desktop_menu_close(app);
  desktop_icon_menu_close(app);
  eh_desktop_log("workspace_right_press: iconCtxMenuOpen=%d openWith.open=%d layers=%zu",
                 app.iconCtxMenuOpen, app.openWith.open, app.layers.size());
  {
    double lx = 0, ly = 0;
    if (desktop_pointer_local_xy(app, app.pointerLayerIdx, &lx, &ly)) {
      int idx = -1;
      if (desktop_icons_hit_index(app, app.pointerLayerIdx, lx, ly, &idx)) {
        if (app.iconMarqueeSelection.empty() ||
            std::find(app.iconMarqueeSelection.begin(), app.iconMarqueeSelection.end(), idx) == app.iconMarqueeSelection.end()) {
          app.iconMarqueeSelection.clear();
          app.iconMarqueeSelection.push_back(idx);
        }
        eh_desktop_log("workspace_right_press: hit icon idx=%d -> icon_menu_open", idx);
        desktop_icon_menu_open(app, idx);
        return;
      }
    }
  }
  eh_desktop_log("workspace_right_press: no icon hit -> workspace menu");
  desktop_menu_open_for_workspace(app);
}

void desktop_icons_purge_raster_cache_for(DesktopApp& app) { file_icon_cache::purge(&app); }

}
