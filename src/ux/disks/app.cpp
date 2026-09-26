#include "ux/disks/app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "configuration/shell_config.hpp"
#include "desktop_shell/common/log/debug_log.hpp"
#include "ux/disks/block.hpp"
#include "ux/disks/dialogs/benchmark_dialog.hpp"
#include "ux/disks/dialogs/create_partition_dialog.hpp"
#include "ux/disks/dialogs/format_disk_dialog.hpp"
#include "ux/disks/dialogs/format_volume_dialog.hpp"
#include "ux/disks/dialogs/image_dialog.hpp"
#include "ux/disks/dialogs/mount_options_dialog.hpp"
#include "ux/disks/dialogs/resize_dialog.hpp"
#include "ux/disks/dialogs/smart_dialog.hpp"
#include "ux/disks/dialogs/unlock_dialog.hpp"
#include "ux/disks/drive.hpp"
#include "ux/disks/jobs.hpp"
#include "ux/disks/manager.hpp"
#include "ux/disks/ui/layout.hpp"
#include "wl/buffer/shm_buffer.hpp"
#include "wl/surface/surface_extensions.hpp"
#include "xdg-shell-client-protocol.h"

// Forward views (ui/draw.cpp owns them).
namespace eh::disks {
void paint_main(AppState& app, cairo_t* cr, const layout::Layout& L,
                const std::vector<std::shared_ptr<Drive>>& drives,
                const std::vector<std::shared_ptr<Block>>& blocks);
void paint_menu(AppState& app, cairo_t* cr, const layout::Layout& L);
void paint_dialogs(AppState& app, cairo_t* cr);
}  // namespace eh::disks

namespace eh::disks {

AppState::AppState() {
  fmtDiskDlg = std::make_unique<FormatDiskDialog>();
  fmtDlg = std::make_unique<FormatVolumeDialog>();
  createPartDlg = std::make_unique<CreatePartitionDialog>();
  resizeDlg = std::make_unique<ResizeDialog>();
  unlockDlg = std::make_unique<UnlockDialog>();
  smartDlg = std::make_unique<SmartDialog>();
  benchDlg = std::make_unique<BenchmarkDialog>();
  mountOptsDlg = std::make_unique<MountOptionsDialog>();
  imageDlg = std::make_unique<ImageDialog>();
  attachDlg = std::make_unique<AttachDialog>();
  drvSettingsDlg = std::make_unique<DriveSettingsDialog>();
}
AppState::~AppState() = default;

bool AppState::any_dialog_open() const {
  return (fmtDiskDlg && fmtDiskDlg->open) || (fmtDlg && fmtDlg->open) ||
         (createPartDlg && createPartDlg->open) || (resizeDlg && resizeDlg->open) ||
         (unlockDlg && unlockDlg->open) || (smartDlg && smartDlg->open) ||
         (benchDlg && benchDlg->open) || (mountOptsDlg && mountOptsDlg->open) ||
         (imageDlg && imageDlg->open) || (attachDlg && attachDlg->open) ||
         (drvSettingsDlg && drvSettingsDlg->open);
}

// ---- formatting ----

std::string size_str(uint64_t bytes) {
  char buf[64];
  // Disks uses decimal SI.
  if (bytes >= 1000000000000ULL)
    std::snprintf(buf, sizeof(buf), "%.1f TB", bytes / 1e12);
  else if (bytes >= 1000000000ULL)
    std::snprintf(buf, sizeof(buf), "%.1f GB", bytes / 1e9);
  else if (bytes >= 1000000ULL)
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1e6);
  else if (bytes >= 1000ULL)
    std::snprintf(buf, sizeof(buf), "%.1f kB", bytes / 1e3);
  else
    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
  return buf;
}

std::string size_str_full(uint64_t bytes) {
  // "500 GB (500,107,862,016 bytes)" style.
  std::string grouped;
  std::string digits = std::to_string(bytes);
  int n = 0;
  for (int i = int(digits.size()) - 1; i >= 0; --i) {
    grouped.push_back(digits[size_t(i)]);
    if (++n == 3 && i > 0) {
      grouped.push_back(',');
      n = 0;
    }
  }
  std::reverse(grouped.begin(), grouped.end());
  return size_str(bytes) + " (" + grouped + " bytes)";
}

void set_rgba(cairo_t* cr, double r, double g, double b, double a) {
  cairo_set_source_rgba(cr, r, g, b, a);
}
void set_rgb(cairo_t* cr, double r, double g, double b) {
  cairo_set_source_rgb(cr, r, g, b);
}
void draw_rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
  if (r < 0) r = 0;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
  cairo_close_path(cr);
}

void draw_icon_surface(cairo_t* cr, const eh::icons::IconEntry* icon, int x, int y,
                       int target_size) {
  if (!icon || !icon->surface) return;
  int iw = icon->width, ih = icon->height;
  if (iw <= 0 || ih <= 0) return;
  double scale = double(target_size) / double(std::max(iw, ih));
  int dw = int(std::lround(iw * scale));
  int dh = int(std::lround(ih * scale));
  int dx = x + (target_size - dw) / 2;
  int dy = y + (target_size - dh) / 2;
  cairo_save(cr);
  cairo_translate(cr, dx, dy);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, icon->surface, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);
}

const char* drive_icon_name(const std::shared_ptr<Drive>& d) {
  if (!d) return "drive-harddisk";
  if (d->is_loop()) return "drive-optical";
  if (d->is_removable()) return "drive-removable-media";
  return "drive-harddisk";
}

std::string drive_display_name(const std::shared_ptr<Drive>& d) {
  if (!d) return "Drive";
  std::string m = d->get_model();
  if (!m.empty()) return m;
  std::string v = d->get_vendor();
  if (!v.empty()) return v;
  return d->get_description();
}

std::string drive_subtitle(const std::shared_ptr<Drive>& d) {
  if (!d) return "";
  std::string s = size_str(d->get_size());
  std::string media = d->get_media();
  if (!media.empty() && media != "flash_cf" && media != "flash_ms") s += " · " + media;
  std::string ser = d->get_serial();
  if (!ser.empty()) {
    if (ser.size() > 18) ser = ser.substr(0, 18) + "…";
    s += " · " + ser;
  }
  return s;
}

std::string block_short_name(const std::shared_ptr<Block>& b) {
  if (!b) return "—";
  std::string dev = b->get_device();
  auto p = dev.find_last_of('/');
  if (p != std::string::npos) dev = dev.substr(p + 1);
  if (dev.empty()) {
    std::string lbl = b->get_label();
    if (!lbl.empty()) return lbl;
    return "Volume";
  }
  return dev;
}

std::string block_fs_label(const std::shared_ptr<Block>& b) {
  if (!b) return "—";
  std::string t = b->get_fstype();
  if (!t.empty()) return t;
  std::string u = b->get_fsusage();
  if (u == "crypto") return "LUKS";
  if (!u.empty()) return u;
  if (b->is_swap()) return "swap";
  if (b->has_partition() && !b->has_filesystem()) return "unformatted";
  return "—";
}

std::string block_mount_str(const std::shared_ptr<Block>& b) {
  if (!b) return "";
  auto mps = b->get_mount_points();
  if (!mps.empty()) return mps[0];
  if (b->is_swap()) return b->is_swap_active() ? "active" : "inactive";
  if (b->is_encrypted())
    return b->has_cleartext() ? "unlocked" : "locked";
  return "";
}

std::string partition_flags_str(const std::shared_ptr<Block>& b) {
  if (!b || !b->has_partition()) return "";
  uint64_t f = b->get_partition_flags();
  // libparted/dos flags are opaque here; surface GPT name instead when present.
  std::string n = b->get_partition_name();
  if (!n.empty()) return n;
  if (f == 0) return "";
  char buf[32];
  std::snprintf(buf, sizeof(buf), "flags %llu", (unsigned long long)f);
  return buf;
}

static std::string lower_str(std::string s) {
  for (auto& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

std::vector<std::shared_ptr<Drive>> filtered_drives(
    const std::vector<std::shared_ptr<Drive>>& all, const std::string& query) {
  if (query.empty()) return all;
  std::string q = lower_str(query);
  std::vector<std::shared_ptr<Drive>> out;
  for (auto& d : all) {
    std::string hay = lower_str(drive_display_name(d) + " " + d->get_serial() +
                                " " + d->get_model() + " " + d->get_vendor());
    // Also match any child block.
    for (auto& b : d->blocks())
      hay += " " + lower_str(b->get_device() + " " + b->get_label() + " " +
                             b->get_fstype());
    if (hay.find(q) != std::string::npos) out.push_back(d);
  }
  return out;
}

std::vector<std::shared_ptr<Block>> filtered_blocks(
    const std::shared_ptr<Drive>& drive, const std::string& query) {
  std::vector<std::shared_ptr<Block>> out;
  if (!drive) return out;
  // Disks 51 shows extended containers properly; GParted shows free rows
  // separately — our bar synthesizes free, the table lists real blocks only.
  for (auto& b : drive->blocks()) {
    if (b->is_hint_ignore()) continue;
    out.push_back(b);
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) {
    // Containers first (disk device), then by offset.
    bool ap = !a->has_partition(), bp = !b->has_partition();
    if (ap != bp) return ap;
    return a->get_partition_offset() < b->get_partition_offset();
  });
  // Drop the raw disk-device row when partitions exist (it duplicates the drive).
  bool has_part = false;
  for (auto& b : out)
    if (b->has_partition()) has_part = true;
  if (has_part) {
    std::vector<std::shared_ptr<Block>> f;
    for (auto& b : out)
      if (b->has_partition() || b->has_filesystem() || b->is_encrypted() ||
          b->is_swap())
        f.push_back(b);
    // Keep at least one row so empty-but-formatted disks still show.
    if (!f.empty()) out = f;
  }
  if (!query.empty()) {
    std::string q = lower_str(query);
    std::vector<std::shared_ptr<Block>> f;
    for (auto& b : out) {
      std::string hay = lower_str(b->get_device() + " " + b->get_label() + " " +
                                  b->get_fstype() + " " + block_mount_str(b));
      if (hay.find(q) != std::string::npos) f.push_back(b);
    }
    out = f;
  }
  return out;
}

void drive_groups(const std::vector<std::shared_ptr<Drive>>& drives,
                  std::vector<std::shared_ptr<Drive>>* internal,
                  std::vector<std::shared_ptr<Drive>>* removable,
                  std::vector<std::shared_ptr<Drive>>* loop) {
  for (auto& d : drives) {
    if (d->is_loop()) {
      if (loop) loop->push_back(d);
    } else if (d->is_removable() || d->is_ejectable()) {
      if (removable) removable->push_back(d);
    } else {
      if (internal) internal->push_back(d);
    }
  }
}

void disk_log(const char* fmt, ...) {
  static FILE* logf = [] {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/disks.log", eh_log::dir());
    FILE* f = fopen(path, "w");
    return f ? f : stderr;
  }();
  va_list ap;
  va_start(ap, fmt);
  vfprintf(logf, fmt, ap);
  va_end(ap);
  fprintf(logf, "\n");
  fflush(logf);
}

void toast(AppState& app, const std::string& text, bool error) {
  Toast t;
  t.text = text;
  t.error = error;
  t.mono = mono_seconds();
  app.toasts.push_back(t);
  if (app.toasts.size() > 3) app.toasts.erase(app.toasts.begin());
}

// ---- menus ----

std::vector<MenuItem> build_main_menu(AppState&) {
  return {
      {"attach", "Attach Disk Image…", "◎", true, "", false, false},
      {"create-image", "Create Disk Image…", "⤓", true, "", false, false},
      {"restore-image", "Restore Disk Image…", "⤒", true, "", false, true},
      {"settings", "Drive Settings", "⚙", true, "", false, false},
      {"shortcuts", "Keyboard Shortcuts", "⌨", true, "", false, true},
      {"about", "About Disks", "ⓘ", true, "", false, false},
  };
}

std::vector<MenuItem> build_drive_menu(AppState&, const std::shared_ptr<Drive>& d) {
  std::vector<MenuItem> items;
  if (!d) return items;
  auto f = d->get_features();
  bool can_format = (f & FEATURE_FORMAT) || (f & FEATURE_CREATE_PARTITION);
  items.push_back({"format-disk", "Format Disk…", "✕", can_format,
                   can_format ? "" : "Read-only device", true, false});
  items.push_back({"create-image", "Create Disk Image…", "⤓",
                   bool(f & FEATURE_CREATE_IMAGE), "", false, false});
  items.push_back({"restore-image", "Restore Disk Image…", "⤒",
                   bool(f & FEATURE_RESTORE_IMAGE),
                   (f & FEATURE_RESTORE_IMAGE) ? "" : "Read-only device", false, true});
  items.push_back({"smart", "SMART Data & Self-Tests…", "♥", bool(f & FEATURE_SMART),
                   (f & FEATURE_SMART) ? "" : "No SMART support", false, false});
  items.push_back({"benchmark", "Benchmark Disk…", "▦", bool(f & FEATURE_BENCHMARK), "",
                   false, false});
  items.push_back({"drive-settings", "Drive Settings…", "⚙", true, "", false, true});
  bool can_power = (f & FEATURE_POWEROFF);
  bool can_eject = (f & FEATURE_EJECT);
  bool can_detach = (f & FEATURE_DETACH);
  items.push_back({"standby", "Standby Now", "☾", can_power || d->is_loop(),
                   "", false, false});
  if (can_eject)
    items.push_back({"eject", "Eject", "⏏", true, "", false, false});
  if (can_detach)
    items.push_back({"detach", "Detach Loop Device", "⎋", true, "", false, false});
  if (can_power)
    items.push_back({"poweroff", "Power Off…", "⏻", true, "", true, false});
  return items;
}

std::vector<MenuItem> build_block_menu(AppState&, const std::shared_ptr<Block>& b) {
  std::vector<MenuItem> items;
  if (!b) return items;
  auto f = b->get_features();
  auto has = [&](Feature bit) { return (f & bit) != 0; };
  items.push_back({"mount-toggle", has(FEATURE_CAN_UNMOUNT) ? "Unmount" : "Mount",
                   has(FEATURE_CAN_UNMOUNT) ? "⏏" : "▶",
                   has(FEATURE_CAN_MOUNT) || has(FEATURE_CAN_UNMOUNT), "", false,
                   false});
  if (has(FEATURE_CAN_UNLOCK) || has(FEATURE_CAN_LOCK))
    items.push_back({"lock-toggle", has(FEATURE_CAN_LOCK) ? "Lock" : "Unlock…",
                     has(FEATURE_CAN_LOCK) ? "🔒" : "🔓", true, "", false, false});
  items.push_back({"add-part", "Add Partition…", "＋", has(FEATURE_CREATE_PARTITION),
                   has(FEATURE_CREATE_PARTITION) ? "" : "No free space handling here",
                   false, true});
  items.push_back({"format", "Format Partition…", "✎", has(FEATURE_FORMAT), "", false,
                   false});
  items.push_back({"edit-part", "Edit Partition…", "⚙", has(FEATURE_EDIT_PARTITION),
                   "", false, false});
  items.push_back({"edit-fs", "Edit Filesystem…", "🏷", has(FEATURE_EDIT_LABEL),
                   has(FEATURE_EDIT_LABEL) ? "" : "Filesystem is read-only", false,
                   false});
  items.push_back({"resize", "Resize…", "⇔", has(FEATURE_RESIZE_PARTITION),
                   has(FEATURE_RESIZE_PARTITION) ? ""
                                                 : "Resize needs ext4/btrfs/xfs/ntfs",
                   false, false});
  items.push_back({"check", "Check Filesystem", "✓", has(FEATURE_CHECK_FILESYSTEM),
                   has(FEATURE_CHECK_FILESYSTEM) ? "" : "No fsck helper installed",
                   false, false});
  items.push_back({"repair", "Repair Filesystem…", "🛠", has(FEATURE_REPAIR_FILESYSTEM),
                   has(FEATURE_REPAIR_FILESYSTEM) ? "" : "No repair helper installed",
                   false, true});
  items.push_back({"take-ownership", "Take Ownership…", "👤",
                   has(FEATURE_TAKE_OWNERSHIP), "", false, false});
  items.push_back({"mount-opts", "Edit Mount Options…", "📌",
                   has(FEATURE_CONFIGURE_FSTAB) || has(FEATURE_CAN_MOUNT) ||
                       has(FEATURE_CAN_UNMOUNT),
                   "", false, false});
  if (has(FEATURE_CAN_SWAPON) || has(FEATURE_CAN_SWAPOFF))
    items.push_back({"swap-toggle", has(FEATURE_CAN_SWAPOFF) ? "Stop Swap" : "Start Swap",
                     "⇄", true, "", false, false});
  items.push_back({"delete", "Delete Partition…", "🗑", has(FEATURE_DELETE_PARTITION),
                   "", true, false});
  return items;
}

[[maybe_unused]] static std::shared_ptr<Block> selected_block_ptr(AppState& app) {
  auto drives = Manager::instance().get_drives();
  auto fdrv = filtered_drives(drives, app.search.text);
  if (app.selected_drive < 0 || app.selected_drive >= int(fdrv.size())) return nullptr;
  auto blocks = filtered_blocks(fdrv[size_t(app.selected_drive)], app.search.text);
  if (app.selected_block < 0 || app.selected_block >= int(blocks.size()))
    return nullptr;
  return blocks[size_t(app.selected_block)];
}

[[maybe_unused]] static std::shared_ptr<Drive> selected_drive_ptr(AppState& app) {
  auto drives = Manager::instance().get_drives();
  auto fdrv = filtered_drives(drives, app.search.text);
  if (app.selected_drive < 0 || app.selected_drive >= int(fdrv.size())) return nullptr;
  return fdrv[size_t(app.selected_drive)];
}

void run_menu_action(AppState& app, const std::string& id, int drive_idx, int block_idx) {
  auto drives = Manager::instance().get_drives();
  auto fdrv = filtered_drives(drives, app.search.text);
  std::shared_ptr<Drive> drive;
  if (drive_idx >= 0 && drive_idx < int(fdrv.size())) drive = fdrv[size_t(drive_idx)];
  std::shared_ptr<Block> block;
  if (drive && block_idx >= 0) {
    auto blocks = filtered_blocks(drive, app.search.text);
    if (block_idx < int(blocks.size())) block = blocks[size_t(block_idx)];
  }
  if (id == "attach") {
    app.attachDlg->open = true;
    app.attachDlg->error.clear();
    app.attachDlg->pending = false;
  } else if (id == "create-image" || id == "restore-image") {
    if (!drive) {
      toast(app, "Select a drive first", true);
      return;
    }
    app.imageDlg->open = true;
    app.imageDlg->restore = (id == "restore-image");
    app.imageDlg->error.clear();
    app.imageDlg->pending = false;
    app.imageDlg->progress = -1;
  } else if (id == "format-disk") {
    if (!drive) return;
    app.fmtDiskDlg->open = true;
    app.fmtDiskDlg->pending = false;
  } else if (id == "smart") {
    app.smartDlg->open = true;
    app.smartDlg->drive_idx = drive_idx;
    app.smartDlg->scroll = 0;
  } else if (id == "benchmark") {
    app.benchDlg->open = true;
    app.benchDlg->drive_idx = drive_idx;
    app.benchDlg->running = false;
    app.benchDlg->has_result = false;
    app.benchDlg->reads.clear();
  } else if (id == "drive-settings" || id == "settings") {
    app.drvSettingsDlg->open = true;
    app.drvSettingsDlg->drive_idx = drive_idx;
  } else if (id == "shortcuts") {
    toast(app, "↑↓ drives · ←→ volumes · Enter mount · / search · Esc close");
  } else if (id == "about") {
    toast(app, "Horizon Disks — Disks 51 rebuild · UDisks2 + GParted model");
  } else if (id == "standby" || id == "poweroff" || id == "eject" || id == "detach") {
    if (!drive) return;
    std::string label = id == "poweroff" ? "Powering off " : id == "eject" ? "Ejecting "
                                    : id == "detach"       ? "Detaching "
                                                           : "Standing by ";
    auto job = JobTracker::instance().start(label + drive_display_name(drive) + "…", "",
                                            false);
    app.opActive = true;
    if (id == "poweroff")
      drive->power_off_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Powered off" : "Failed");
      });
    else if (id == "eject")
      drive->eject_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Ejected" : "Failed");
      });
    else if (id == "detach")
      drive->detach_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Detached" : "Failed");
      });
    else
      drive->standby_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Standby" : "Failed");
      });
  } else if (id == "mount-toggle" || id == "swap-toggle") {
    if (!block) return;
    auto f = block->get_features();
    auto job = JobTracker::instance().start("", block->get_object_path(), false);
    if (f & FEATURE_CAN_MOUNT) {
      job->label = "Mounting " + block_short_name(block) + "…";
      block->mount_async("", [job](bool ok, std::string) {
        JobTracker::instance().finish(job->id, ok, ok ? "Mounted" : "Mount failed");
      });
    } else if (f & FEATURE_CAN_UNMOUNT) {
      job->label = "Unmounting " + block_short_name(block) + "…";
      block->unmount_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Unmounted" : "Failed");
      });
    } else if (f & FEATURE_CAN_SWAPON) {
      job->label = "Starting swap…";
      block->swapon_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Swap on" : "Failed");
      });
    } else if (f & FEATURE_CAN_SWAPOFF) {
      job->label = "Stopping swap…";
      block->swapoff_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Swap off" : "Failed");
      });
    }
  } else if (id == "lock-toggle") {
    if (!block) return;
    auto f = block->get_features();
    if (f & FEATURE_CAN_LOCK) {
      auto job = JobTracker::instance().start("Locking " + block_short_name(block) + "…",
                                              block->get_object_path(), false);
      block->lock_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Locked" : "Lock failed");
      });
    } else {
      app.unlockDlg->open = true;
      app.unlockDlg->block_idx = app.selected_block;
      app.unlockDlg->error.clear();
      app.unlockDlg->pending = false;
    }
  } else if (id == "add-part") {
    if (!drive) return;
    // Compute largest free gap for the dialog default.
    auto blocks = filtered_blocks(drive, "");
    uint64_t total = drive->get_size();
    uint64_t pos = 0, best_off = 0, best_sz = 0;
    std::vector<std::pair<uint64_t, uint64_t>> spans;
    for (auto& b : blocks) {
      if (!b->has_partition()) continue;
      uint64_t o = b->get_partition_offset();
      if (o > pos) spans.push_back({pos, o - pos});
      pos = std::max(pos, o + b->get_size());
    }
    if (pos < total) spans.push_back({pos, total - pos});
    for (auto& s : spans)
      if (s.second > best_sz) {
        best_sz = s.second;
        best_off = s.first;
      }
    app.createPartDlg->open = true;
    app.createPartDlg->free_offset = best_off;
    app.createPartDlg->free_size = best_sz;
    app.createPartDlg->size = best_sz;
    app.createPartDlg->error.clear();
    app.createPartDlg->pending = false;
  } else if (id == "format") {
    app.fmtDlg->open = true;
    app.fmtDlg->block_idx = app.selected_block;
    app.fmtDlg->error.clear();
    app.fmtDlg->pending = false;
    app.fmtDlg->pass.password = true;
    app.fmtDlg->pass_confirm.password = true;
    app.fmtDlg->focus = 0;
  } else if (id == "resize") {
    if (!block) return;
    app.resizeDlg->open = true;
    app.resizeDlg->block_idx = app.selected_block;
    app.resizeDlg->cur_size = block->get_size();
    app.resizeDlg->max_size = block->get_size();
    // Heuristic min: used bytes when mounted, else 1/4.
    uint64_t used = block->get_fs_used_bytes();
    app.resizeDlg->min_size = used > 0 ? used + 256 * 1024 * 1024 : block->get_size() / 4;
    if (app.resizeDlg->min_size >= app.resizeDlg->max_size)
      app.resizeDlg->min_size = app.resizeDlg->max_size / 2;
    app.resizeDlg->new_size = app.resizeDlg->max_size;
    app.resizeDlg->missing_tools = !(block->get_features() & FEATURE_RESIZE_PARTITION);
    if (app.resizeDlg->missing_tools)
      app.resizeDlg->missing_msg =
          "Resize utilities for this filesystem are not installed.";
    app.resizeDlg->error.clear();
    app.resizeDlg->pending = false;
  } else if (id == "check" || id == "repair") {
    if (!block) return;
    bool repair = (id == "repair");
    auto job = JobTracker::instance().start(
        (repair ? "Repairing " : "Checking ") + block_short_name(block) + "…",
        block->get_object_path(), false);
    if (repair)
      block->repair_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Repaired" : "Repair failed");
      });
    else
      block->check_async([job](bool ok) {
        JobTracker::instance().finish(job->id, ok, ok ? "Check clean" : "Check failed");
      });
  } else if (id == "take-ownership") {
    if (!block) return;
    auto job = JobTracker::instance().start(
        "Taking ownership of " + block_short_name(block) + "…",
        block->get_object_path(), false);
    block->take_ownership_async([job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Ownership taken" : "Failed");
    });
  } else if (id == "mount-opts") {
    app.mountOptsDlg->open = true;
    app.mountOptsDlg->block_idx = app.selected_block;
  } else if (id == "edit-part" || id == "edit-fs") {
    // Route to format-volume name path as the closest safe editor; full
    // label/type editors land here next.
    app.fmtDlg->open = true;
    app.fmtDlg->block_idx = app.selected_block;
    app.fmtDlg->error.clear();
    app.fmtDlg->pass.password = true;
    app.fmtDlg->pass_confirm.password = true;
    app.fmtDlg->focus = 0;
  } else if (id == "delete") {
    if (!block) return;
    auto job = JobTracker::instance().start(
        "Deleting " + block_short_name(block) + "…", block->get_object_path(), false);
    block->delete_partition_async([job](bool ok) {
      JobTracker::instance().finish(job->id, ok, ok ? "Deleted" : "Delete failed");
    });
  }
  schedule_frame(app);
}

// ---- main draw ----

void draw(AppState& app) {
  if (!app.surface) return;
  int w = app.width, h = app.height;
  if (w <= 0 || h <= 0) return;

  auto* buf = (!app.buf[0].busy()) ? &app.buf[0] : &app.buf[1];
  if (buf->busy()) return;
  if (app.shm && (buf->width() != w || buf->height() != h)) {
    const char* tag = (buf == &app.buf[0]) ? "eh-disks-a" : "eh-disks-b";
    buf->ensure(app.shm, tag, w, h);
  }
  cairo_t* cr = buf->cairo();
  if (!cr) return;

  app.last_paint_w = w;
  app.last_paint_h = h;
  double now = mono_seconds();
  if (app.last_frame_mono == 0) app.last_frame_mono = now;
  double dt = now - app.last_frame_mono;
  app.last_frame_mono = now;
  if (JobTracker::instance().has_active())
    app.spinner_phase += dt * 6.0;
  JobTracker::instance().prune();

  cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  set_rgba(cr, app.bgR, app.bgG, app.bgB, 1.0);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  auto drives_all = Manager::instance().get_drives();
  auto drives = filtered_drives(drives_all, app.search.text);
  if (app.selected_drive < 0 && !drives.empty()) app.selected_drive = 0;
  if (app.selected_drive >= int(drives.size()))
    app.selected_drive = drives.empty() ? -1 : 0;
  std::shared_ptr<Drive> drive;
  if (app.selected_drive >= 0 && app.selected_drive < int(drives.size()))
    drive = drives[size_t(app.selected_drive)];
  uint64_t dsize = drive ? drive->get_size() : 0;
  auto blocks = drive ? filtered_blocks(drive, app.search.text)
                      : std::vector<std::shared_ptr<Block>>{};
  if (app.selected_block >= int(blocks.size())) app.selected_block = -1;

  layout::Layout L = layout::compute(app, drives, blocks, dsize);
  app.contentH = L.content_total_h;

  paint_main(app, cr, L, drives, blocks);
  paint_menu(app, cr, L);
  paint_dialogs(app, cr);

  // Toasts
  {
    int ty = h - 30 - (JobTracker::instance().has_active() ? 30 : 0) - 16;
    for (int i = int(app.toasts.size()) - 1; i >= 0; --i) {
      auto& t = app.toasts[i];
      if (now - t.mono > 4.0) continue;
      cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
                             CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 12);
      cairo_text_extents_t te;
      cairo_text_extents(cr, t.text.c_str(), &te);
      int tw = int(te.width) + 28;
      int tx = (w - tw) / 2;
      ty -= 40;
      set_rgba(cr, 0.08, 0.08, 0.10, 0.95);
      draw_rounded_rect(cr, tx, ty, tw, 32, 10);
      cairo_fill(cr);
      set_rgb(cr, t.error ? 0.95 : app.outlineR, t.error ? 0.45 : app.outlineG,
              t.error ? 0.45 : app.outlineB);
      cairo_set_line_width(cr, 1);
      draw_rounded_rect(cr, tx, ty, tw, 32, 10);
      cairo_stroke(cr);
      set_rgb(cr, app.textR, app.textG, app.textB);
      cairo_move_to(cr, tx + 14, ty + 21);
      cairo_show_text(cr, t.text.c_str());
    }
    // Expire
    app.toasts.erase(
        std::remove_if(app.toasts.begin(), app.toasts.end(),
                       [now](const Toast& t) { return now - t.mono > 4.0; }),
        app.toasts.end());
  }

  wl_surface* surf = app.surface;
  wl_surface_set_buffer_scale(surf, 1);
  wl_surface_damage_buffer(surf, 0, 0, w, h);
  wl_surface_attach(surf, buf->wl(), 0, 0);
  buf->mark_busy();

  if (app.frame_cb) wl_callback_destroy(app.frame_cb);
  app.frame_cb = wl_surface_frame(surf);
  static constexpr wl_callback_listener kFrameListener{
      .done = [](void* data, wl_callback*, uint32_t) {
        auto& a = *static_cast<AppState*>(data);
        a.frame_cb = nullptr;
        if (a.pendingRedraw) {
          a.pendingRedraw = false;
          draw(a);
        } else if (JobTracker::instance().has_active()) {
          draw(a);
        }
      }};
  wl_callback_add_listener(app.frame_cb, &kFrameListener, &app);

  wl_surface_commit(surf);
  wl_display_flush(app.wl.display());
}

void schedule_frame(AppState& app) { app.pendingRedraw = true; }

}  // namespace eh::disks
