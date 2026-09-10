#include "../app.hpp"

#include <cairo/cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <mntent.h>

#include "services/udisks2/udisks2_drive_service.hpp"

namespace fs = std::filesystem;

namespace eh::file_browser {

// ── helpers ──────────────────────────────────────────────────────

static std::string home_dir_path() {
  if (auto* h = std::getenv("HOME")) return h;
  return "/root";
}

static std::string xdg_user_dir_path(const char* env, const char* fallback) {
  if (auto* e = std::getenv(env)) return e;
  return home_dir_path() + "/" + fallback;
}

static std::string format_size_binary(uint64_t bytes) {
  char buf[32];
  if (bytes == 0) return "0 B";
  const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  int unit = 0;
  double val = static_cast<double>(bytes);
  while (val >= 1024.0 && unit < 5) { val /= 1024.0; ++unit; }
  if (unit == 0)
    std::snprintf(buf, sizeof(buf), "%.0f %s", val, units[unit]);
  else if (val < 10.0)
    std::snprintf(buf, sizeof(buf), "%.1f %s", val, units[unit]);
  else
    std::snprintf(buf, sizeof(buf), "%.0f %s", val, units[unit]);
  return buf;
}

// ── refresh_computer ─────────────────────────────────────────────

void refresh_computer(AppState& app) {
  app.computer_items.clear();

  // ── Splitter: My Computer ──
  {
    ComputerItem split;
    split.shape = ComputerItem::ShapeType::Splitter;
    split.group = ComputerItem::Group::UserDirs;
    split.label = "My Computer";
    app.computer_items.push_back(split);
  }

  // ── Section 1: User Directories (Small items) ──
  auto add_user_dir = [&](const std::string& label, const std::string& icon,
                           const std::string& dir_path) {
    ComputerItem item;
    item.shape = ComputerItem::ShapeType::Small;
    item.group = ComputerItem::Group::UserDirs;
    item.label = label;
    item.icon_name = icon;
    item.path = dir_path;
    item.is_mounted = true;
    // Check if dir exists
    struct stat st;
    item.is_mounted = (stat(dir_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    std::error_code ec;
    auto space = fs::space(dir_path, ec);
    if (!ec) {
      item.total_bytes = space.capacity;
      item.used_bytes = space.capacity - space.available;
    }
    app.computer_items.push_back(item);
  };

  add_user_dir("Desktop",    "user-desktop",     xdg_user_dir_path("XDG_DESKTOP_DIR", "Desktop"));
  add_user_dir("Documents",  "folder-documents", xdg_user_dir_path("XDG_DOCUMENTS_DIR", "Documents"));
  add_user_dir("Downloads",  "folder-download",  xdg_user_dir_path("XDG_DOWNLOAD_DIR", "Downloads"));
  add_user_dir("Music",      "folder-music",     xdg_user_dir_path("XDG_MUSIC_DIR", "Music"));
  add_user_dir("Pictures",   "folder-pictures",  xdg_user_dir_path("XDG_PICTURES_DIR", "Pictures"));
  add_user_dir("Videos",     "folder-videos",    xdg_user_dir_path("XDG_VIDEOS_DIR", "Videos"));

  // ── Splitter: Disks ──
  {
    ComputerItem split;
    split.shape = ComputerItem::ShapeType::Splitter;
    split.group = ComputerItem::Group::Disks;
    split.label = "Disks";
    app.computer_items.push_back(split);
  }

  // ── Section 2: Drives (Large items with progress) ──
  auto& drives = eh::drives::UDisks2DriveService::instance();
  std::vector<std::string> seen_mounts;

  auto unescape_name = [](std::string s) {
    for (auto p = s.find("\\x20"); p != std::string::npos;
         p = s.find("\\x20", p + 1))
      s.replace(p, 4, " ");
    return s;
  };

  // Parse /proc/mounts for mounted block devices
  FILE* mtab = setmntent("/proc/mounts", "r");
  if (mtab) {
    struct mntent mnt_buf;
    char mnt_str_buf[4096];
    while (getmntent_r(mtab, &mnt_buf, mnt_str_buf, sizeof(mnt_str_buf))) {
      std::string dev = mnt_buf.mnt_fsname;
      std::string mp = mnt_buf.mnt_dir;
      // Skip pseudo-fs, /dev, /proc, /sys, tmpfs, etc.
      if (dev.empty() || dev[0] != '/') continue;
      if (mp == "/boot" || mp == "/boot/efi" || mp == "/recovery") continue;
      // Skip non-block devices
      if (dev.find("/dev/") != 0) continue;
      // Skip loop, snap, zram
      if (dev.find("loop") != std::string::npos ||
          dev.find("zram") != std::string::npos ||
          dev.find("snap") != std::string::npos)
        continue;
      // Skip if already seen
      if (std::find(seen_mounts.begin(), seen_mounts.end(), dev) != seen_mounts.end())
        continue;
      seen_mounts.push_back(dev);

      ComputerItem item;
      item.shape = ComputerItem::ShapeType::Large;
      item.group = ComputerItem::Group::Disks;
      item.path = mp;
      item.drive_id = dev;
      item.is_mounted = true;
      item.filesystem = mnt_buf.mnt_type;

      // Get filesystem label from /dev/disk/by-label/
      std::string dev_base = dev.substr(5); // strip "/dev/"
      std::string label_path = "/dev/disk/by-label/";
      std::error_code ec;
      for (auto& entry : fs::directory_iterator("/dev/disk/by-label/", ec)) {
        std::error_code ec2;
        std::string target = fs::read_symlink(entry.path(), ec2);
        if (!ec2 && target.find(dev_base) != std::string::npos) {
          item.label = unescape_name(entry.path().filename().string());
          item.is_user_label = true;
          break;
        }
      }
      if (item.label.empty()) {
        if (mp == "/") item.label = "System Disk";
        else item.label = dev_base;
      }

      // Get space info
      struct statvfs vfs;
      if (statvfs(mp.c_str(), &vfs) == 0) {
        item.total_bytes = static_cast<uint64_t>(vfs.f_frsize) * vfs.f_blocks;
        item.used_bytes = static_cast<uint64_t>(vfs.f_frsize) * (vfs.f_blocks - vfs.f_bfree);
      }

      // Determine icon
      item.icon_name = "drive-harddisk";
      item.show_progress = true;
      app.computer_items.push_back(item);
    }
    endmntent(mtab);
  }

  // Scan /dev/disk/by-label/ for unmounted labeled drives
  {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator("/dev/disk/by-label/", ec)) {
      std::error_code ec2;
      std::string target = fs::read_symlink(entry.path(), ec2).string();
      if (ec2 || target.empty()) continue;
      std::string dev;
      if (target.size() > 6 && target.substr(0, 6) == "../../")
        dev = "/dev/" + target.substr(6);
      else if (target.size() > 3 && target.substr(0, 3) == "../")
        dev = "/dev/" + target.substr(3);
      else
        dev = target;
      if (dev.size() < 5 || dev.substr(0, 5) != "/dev/") continue;
      if (std::find(seen_mounts.begin(), seen_mounts.end(), dev) != seen_mounts.end())
        continue;
      seen_mounts.push_back(dev);

      ComputerItem item;
      item.shape = ComputerItem::ShapeType::Large;
      item.group = ComputerItem::Group::Disks;
      item.label = unescape_name(entry.path().filename().string());
      item.drive_id = dev;
      item.is_mounted = false;
      item.is_user_label = true;
      item.icon_name = "drive-harddisk";
      app.computer_items.push_back(item);
    }
  }

  // Scan /dev/disk/by-partlabel/ for unmounted GPT drives
  {
    std::error_code ec;
    for (auto& entry : fs::directory_iterator("/dev/disk/by-partlabel/", ec)) {
      std::error_code ec2;
      std::string target = fs::read_symlink(entry.path(), ec2).string();
      if (ec2 || target.empty()) continue;
      std::string dev;
      if (target.size() > 6 && target.substr(0, 6) == "../../")
        dev = "/dev/" + target.substr(6);
      else if (target.size() > 3 && target.substr(0, 3) == "../")
        dev = "/dev/" + target.substr(3);
      else
        dev = target;
      if (dev.size() < 5 || dev.substr(0, 5) != "/dev/") continue;
      std::string label = unescape_name(entry.path().filename().string());
      if (label == "EFI System Partition" ||
          label == "Microsoft reserved partition" ||
          label == "linux-boot" || label == "linux-efi" ||
          label == "linux-root")
        continue;
      if (std::find(seen_mounts.begin(), seen_mounts.end(), dev) != seen_mounts.end())
        continue;
      seen_mounts.push_back(dev);

      ComputerItem item;
      item.shape = ComputerItem::ShapeType::Large;
      item.group = ComputerItem::Group::Disks;
      item.label = label;
      item.drive_id = dev;
      item.is_mounted = false;
      item.icon_name = "drive-harddisk";
      app.computer_items.push_back(item);
    }
  }

  // Add unmounted drives from UDisks2
  auto dinfo = drives.query_drives();
  for (auto& d : dinfo) {
    if (d.mounted) continue;
    if (d.device.empty()) continue;
    // Check if already in list
    bool found = false;
    for (auto& existing : app.computer_items) {
      if (existing.drive_id == d.device) { found = true; break; }
    }
    if (found) continue;

    ComputerItem item;
    item.shape = ComputerItem::ShapeType::Large;
    item.group = ComputerItem::Group::Disks;
    item.label = d.label.empty() ? "Local Disk" : d.label;
    item.drive_id = d.device;
    item.filesystem = d.id_type;
    item.path = d.mount_point;
    item.is_mounted = false;
    item.total_bytes = d.size;
    item.show_progress = d.size > 0;
    item.icon_name = "drive-harddisk";
    app.computer_items.push_back(item);
  }

  // ── Splitter: Network (placeholder) ──
  // No network items yet; section omitted.
}

// ── draw_computer_view ───────────────────────────────────────────

void draw_computer_view(AppState& app, cairo_t* cr, int content_x,
                        int content_y, int content_w, int view_h) {
  if (app.computer_needs_refresh) {
    refresh_computer(app);
    app.computer_needs_refresh = false;
  }

  double zf = app.zoom_pct / 100.0;
  const int kItemGap = static_cast<int>(16 * zf);
  const int kSmallW = static_cast<int>(140 * zf);
  const int kSmallH = static_cast<int>(140 * zf);
  const int kLargeW = static_cast<int>(300 * zf);
  const int kLargeH = static_cast<int>(96 * zf);
  const int kSplitterH = static_cast<int>(44 * zf);
  const int kCardRadius = static_cast<int>(16 * zf);
  const int kProgressBarH = static_cast<int>(6 * zf);
  const int kDriveGap = static_cast<int>(8 * zf);

  int items = static_cast<int>(app.computer_items.size());
  int y = content_y - app.computer_scroll_px;

  int total_small = 0, total_large = 0;
  for (auto& ci : app.computer_items) {
    if (ci.shape == ComputerItem::ShapeType::Small) ++total_small;
    if (ci.shape == ComputerItem::ShapeType::Large) ++total_large;
  }

  int small_per_row = std::max(1, (content_w - kItemGap) / (kSmallW + kItemGap));
  small_per_row = std::min(small_per_row, total_small);
  int small_total_w = small_per_row * kSmallW;
  int small_flex_gap = (content_w - small_total_w) / (small_per_row + 1);
  small_flex_gap = std::max(small_flex_gap, kItemGap);
  int small_offset_x = small_flex_gap;
  int large_cols = std::max(1, (content_w - kItemGap) / (kLargeW + kItemGap));
  large_cols = std::min(large_cols, total_large);
  int actual_large_w_base = (content_w - (large_cols + 1) * kItemGap) / large_cols;
  actual_large_w_base = std::min(actual_large_w_base, kLargeW);
  actual_large_w_base = std::max(actual_large_w_base, static_cast<int>(240 * zf));
  int total_drive_w = large_cols * actual_large_w_base;
  int flex_gap = (content_w - total_drive_w) / (large_cols + 1);
  flex_gap = std::max(flex_gap, kItemGap);
  int large_offset_x = flex_gap;

  int small_drawn = 0;
  int large_drawn = 0;

  for (int i = 0; i < items; ++i) {
    const auto& citem = app.computer_items[i];

    if (citem.shape == ComputerItem::ShapeType::Splitter) {
      if (y + kSplitterH < content_y) { y += kSplitterH; continue; }
      if (y > content_y + view_h) break;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 18.0 * zf);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
      cairo_move_to(cr, content_x + static_cast<int>(20 * zf), y + kSplitterH - static_cast<int>(12 * zf));
      cairo_show_text(cr, citem.label.c_str());
      y += kSplitterH;
      continue;
    }

    if (citem.shape == ComputerItem::ShapeType::Small) {
      int col = small_drawn % small_per_row;
      int row = small_drawn / small_per_row;
      int cx = content_x + small_offset_x + col * (kSmallW + small_flex_gap);
      int cy = y + row * (kSmallH + kItemGap);

      if (cy + kSmallH < content_y) { ++small_drawn; continue; }
      if (cy > content_y + view_h) break;

      bool hovered = (i == app.computer_hover_idx);
      if (hovered) {
        cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.06);
        draw_rounded_rect(cr, cx, cy, kSmallW, kSmallH, kCardRadius);
        cairo_fill(cr);
      }

      // Icon area with colored circle background
      int icon_area_sz = static_cast<int>(84 * zf);
      int icon_area_x = cx + (kSmallW - icon_area_sz) / 2;
      int icon_area_y = cy + static_cast<int>(20 * zf);

      const auto* ic = app.icons.tray_icon(citem.icon_name.c_str());
      if (ic && ic->surface) {
        double iw = static_cast<double>(ic->width);
        double ih = static_cast<double>(ic->height);
        double scale = icon_area_sz / std::max(1.0, std::max(iw, ih));
        cairo_save(cr);
        cairo_translate(cr, icon_area_x, icon_area_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, ic->surface,
                                 (icon_area_sz / scale - iw) / 2,
                                 (icon_area_sz / scale - ih) / 2);
        cairo_paint(cr);
        cairo_restore(cr);
      } else {
        cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.8);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 28.0 * zf);
        cairo_text_extents_t te;
        char letter[2] = { citem.label.empty() ? '?' : citem.label[0], '\0' };
        cairo_text_extents(cr, letter, &te);
        cairo_move_to(cr, icon_area_x + (icon_area_sz - te.width) / 2,
                       icon_area_y + icon_area_sz / 2 + te.height / 2);
        cairo_show_text(cr, letter);
      }

      // Label centered below icon
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12.0 * zf);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
      cairo_text_extents_t te;
      cairo_text_extents(cr, citem.label.c_str(), &te);
      int max_label_w = kSmallW - static_cast<int>(12 * zf);
      std::string display = citem.label;
      if (te.width > max_label_w) {
        while (!display.empty()) {
          cairo_text_extents(cr, (display + "...").c_str(), &te);
          if (te.width <= max_label_w) break;
          display.pop_back();
        }
        display += "...";
      }
      cairo_move_to(cr, cx + (kSmallW - static_cast<int>(te.width)) / 2,
                     cy + kSmallH - static_cast<int>(18 * zf));
      cairo_show_text(cr, display.c_str());

      ++small_drawn;
      // Advance y past small rows once we finish the last item
      if (small_drawn >= total_small) {
        int last_row = (total_small - 1) / small_per_row;
        y = y + (last_row + 1) * (kSmallH + kItemGap);
      }
      continue;
    }

    if (citem.shape == ComputerItem::ShapeType::Large) {
      int col = large_drawn % large_cols;
      int actual_large_w = actual_large_w_base;
      int cx = content_x + large_offset_x + col * (actual_large_w + flex_gap);
      int cy = y;

      if (cy + kLargeH < content_y) { ++large_drawn; continue; }
      if (cy > content_y + view_h) break;

      bool hovered = (i == app.computer_hover_idx);
      cairo_set_source_rgba(cr, app.surface_r, app.surface_g, app.surface_b,
                            hovered ? 0.95 : 0.75);
      draw_rounded_rect(cr, cx, cy, actual_large_w, kLargeH, kCardRadius);
      cairo_fill(cr);

      // Device icon (left side, aligned with device name)
      int dev_icon_sz = static_cast<int>(36 * zf);
      int icon_x = cx + static_cast<int>(14 * zf);
      int icon_y = cy + static_cast<int>(10 * zf);
      const auto* ic = app.icons.tray_icon(citem.icon_name.c_str());
      if (ic && ic->surface) {
        double iw = static_cast<double>(ic->width);
        double ih = static_cast<double>(ic->height);
        double scale = dev_icon_sz / std::max(1.0, std::max(iw, ih));
        cairo_save(cr);
        cairo_translate(cr, icon_x, icon_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, ic->surface,
                                 (dev_icon_sz / scale - iw) / 2,
                                 (dev_icon_sz / scale - ih) / 2);
        cairo_paint(cr);
        cairo_restore(cr);
      } else {
        cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 0.8);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                                CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 22.0 * zf);
        cairo_text_extents_t te;
        char letter[2] = { citem.label.empty() ? '?' : citem.label[0], '\0' };
        cairo_text_extents(cr, letter, &te);
        cairo_move_to(cr, icon_x + (dev_icon_sz - te.width) / 2,
                       icon_y + dev_icon_sz / 2 + te.height / 2);
        cairo_show_text(cr, letter);
      }

      // Text area right of icon
      int text_x = cx + static_cast<int>(14 * zf) + dev_icon_sz + static_cast<int>(12 * zf);
      int text_w = actual_large_w - (text_x - cx) - static_cast<int>(14 * zf);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                              CAIRO_FONT_WEIGHT_NORMAL);

      // Device name
      double name_font_sz = citem.is_user_label ? 15.0 * zf : 13.0 * zf;
      cairo_set_font_size(cr, name_font_sz);
      cairo_set_source_rgba(cr, app.text_r, app.text_g, app.text_b, 1.0);
      cairo_text_extents_t te;
      std::string dev_label = citem.label;
      cairo_text_extents(cr, dev_label.c_str(), &te);
      if (te.width > text_w) {
        while (!dev_label.empty()) {
          cairo_text_extents(cr, (dev_label + "...").c_str(), &te);
          if (te.width <= text_w) break;
          dev_label.pop_back();
        }
        dev_label += "...";
      }
      cairo_move_to(cr, text_x, cy + static_cast<int>(24 * zf));
      cairo_show_text(cr, dev_label.c_str());

      // Second line: filesystem + device path
      cairo_set_font_size(cr, 10.0 * zf);
      cairo_set_source_rgba(cr, app.text_secondary_r, app.text_secondary_g,
                             app.text_secondary_b, 1.0);
      if (!citem.filesystem.empty() || !citem.drive_id.empty()) {
        std::string subtitle;
        if (!citem.filesystem.empty())
          subtitle = citem.filesystem + " \u2014 ";
        std::string dev_display = citem.drive_id;
        auto pos = dev_display.rfind('/');
        if (pos != std::string::npos && pos + 1 < dev_display.size())
          dev_display = dev_display.substr(pos + 1);
        subtitle += dev_display;
        cairo_move_to(cr, text_x, cy + static_cast<int>(38 * zf));
        cairo_show_text(cr, subtitle.c_str());
      }
      if (!citem.is_mounted) {
        cairo_set_source_rgba(cr, app.text_secondary_r, app.text_secondary_g,
                               app.text_secondary_b, 0.5);
        cairo_move_to(cr, text_x, cy + static_cast<int>(52 * zf));
        cairo_show_text(cr, "Click to mount");
      }

      // Progress bar + usage text (bottom of card)
      if (citem.show_progress && citem.total_bytes > 0) {
        int pb_y = cy + kLargeH - static_cast<int>(16 * zf);
        int pb_w = actual_large_w - static_cast<int>(28 * zf);

        cairo_set_font_size(cr, 10.0 * zf);
        cairo_set_source_rgba(cr, app.text_secondary_r, app.text_secondary_g,
                               app.text_secondary_b, 1.0);

        if (citem.is_mounted && citem.used_bytes > 0) {
          std::string usage = format_size_binary(citem.used_bytes) + " / " +
                              format_size_binary(citem.total_bytes);
          cairo_move_to(cr, cx + static_cast<int>(14 * zf), pb_y - static_cast<int>(5 * zf));
          cairo_show_text(cr, usage.c_str());

          double frac = std::min(1.0, static_cast<double>(citem.used_bytes) /
                                       static_cast<double>(citem.total_bytes));

          // Track
          cairo_set_source_rgba(cr, app.outline_r, app.outline_g, app.outline_b, 0.3);
          draw_rounded_rect(cr, cx + static_cast<int>(14 * zf), pb_y, pb_w, kProgressBarH, static_cast<int>(3 * zf));
          cairo_fill(cr);

          // Fill
          if (frac > 0.01) {
            cairo_set_source_rgba(cr, app.accent_r, app.accent_g, app.accent_b, 0.85);
            draw_rounded_rect(cr, cx + static_cast<int>(14 * zf), pb_y,
                              static_cast<double>(pb_w) * frac, kProgressBarH, static_cast<int>(3 * zf));
            cairo_fill(cr);
          }
        } else {
          std::string usage = format_size_binary(citem.total_bytes);
          cairo_move_to(cr, cx + static_cast<int>(14 * zf), pb_y - static_cast<int>(5 * zf));
          cairo_show_text(cr, usage.c_str());
        }
      }

      ++large_drawn;
      if (large_drawn % large_cols == 0 || large_drawn >= total_large) {
        y = cy + kLargeH + kDriveGap;
      }
      continue;
    }
  }

  int computed_h = y - content_y + app.computer_scroll_px;
  app.computer_content_h = std::max(computed_h, view_h);
}

// ── hit_test_computer ────────────────────────────────────────────

int hit_test_computer(AppState& app, int x, int y) {
  if (app.computer_items.empty()) return -1;

  double zf = app.zoom_pct / 100.0;
  int content_x = app.sidebar_expanded ? app.sidebar_width : 0;
  int content_y = app.top_bar_height + app.tab_bar_height;
  int content_w = app.width - content_x;
  const int kItemGap = static_cast<int>(16 * zf);
  const int kSmallW = static_cast<int>(140 * zf);
  const int kSmallH = static_cast<int>(140 * zf);
  const int kLargeW = static_cast<int>(300 * zf);
  const int kLargeH = static_cast<int>(96 * zf);
  const int kSplitterH = static_cast<int>(44 * zf);
  const int kDriveGap = static_cast<int>(8 * zf);

  int total_small = 0, total_large = 0;
  for (auto& ci : app.computer_items) {
    if (ci.shape == ComputerItem::ShapeType::Small) ++total_small;
    if (ci.shape == ComputerItem::ShapeType::Large) ++total_large;
  }

  int small_per_row = std::max(1, (content_w - kItemGap) / (kSmallW + kItemGap));
  small_per_row = std::min(small_per_row, total_small);
  int small_total_w = small_per_row * kSmallW;
  int small_flex_gap = (content_w - small_total_w) / (small_per_row + 1);
  small_flex_gap = std::max(small_flex_gap, kItemGap);
  int small_offset_x = small_flex_gap;
  int large_cols = std::max(1, (content_w - kItemGap) / (kLargeW + kItemGap));
  large_cols = std::min(large_cols, total_large);
  int actual_large_w_base = (content_w - (large_cols + 1) * kItemGap) / large_cols;
  actual_large_w_base = std::min(actual_large_w_base, kLargeW);
  actual_large_w_base = std::max(actual_large_w_base, static_cast<int>(240 * zf));
  int total_drive_w = large_cols * actual_large_w_base;
  int flex_gap = (content_w - total_drive_w) / (large_cols + 1);
  flex_gap = std::max(flex_gap, kItemGap);
  int large_offset_x = flex_gap;

  int current_y = content_y - app.computer_scroll_px;
  int small_drawn = 0;
  int large_drawn = 0;

  for (int i = 0; i < static_cast<int>(app.computer_items.size()); ++i) {
    const auto& citem = app.computer_items[i];

    if (citem.shape == ComputerItem::ShapeType::Splitter) {
      if (x >= content_x && x < content_x + content_w &&
          y >= current_y && y < current_y + kSplitterH)
        return i;
      current_y += kSplitterH;
    } else if (citem.shape == ComputerItem::ShapeType::Small) {
      int col = small_drawn % small_per_row;
      int row = small_drawn / small_per_row;
      int cx = content_x + small_offset_x + col * (kSmallW + small_flex_gap);
      int cy = current_y + row * (kSmallH + kItemGap);
      if (x >= cx && x < cx + kSmallW && y >= cy && y < cy + kSmallH)
        return i;
      ++small_drawn;
      if (small_drawn >= total_small) {
        int last_row = (total_small - 1) / small_per_row;
        current_y = current_y + (last_row + 1) * (kSmallH + kItemGap);
      }
    } else if (citem.shape == ComputerItem::ShapeType::Large) {
      int col = large_drawn % large_cols;
      int actual_large_w = actual_large_w_base;
      int cx = content_x + large_offset_x + col * (actual_large_w + flex_gap);
      int cy = current_y;
      if (x >= cx && x < cx + actual_large_w && y >= cy && y < cy + kLargeH)
        return i;
      ++large_drawn;
      if (large_drawn % large_cols == 0 || large_drawn >= total_large) {
        current_y = cy + kLargeH + kDriveGap;
      }
    }
  }

  return -1;
}

} // namespace eh::file_browser
