#include "dialog/file_chooser_dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <filesystem>
#include <algorithm>
#include <unordered_map>

#include <cairo/cairo.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/shell_config.hpp"
#include "platform/common/icon_cache/icon_cache.hpp"

namespace fs = std::filesystem;

namespace eh::dialog {

// ── helpers ───────────────────────────────────────────────────────

static std::string home_dir() {
  if (auto* h = std::getenv("HOME")) return h;
  if (auto* pw = getpwuid(getuid())) return pw->pw_dir;
  return "/";
}

static bool is_dir(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string uri_from_path(const std::string& path) {
  return "file://" + path;
}

// ── icon theme helpers ────────────────────────────────────────────

static eh::icons::IconCache s_icon_cache;

static void ensure_icon_cache_initialized() {
  static bool done = false;
  if (done) return;
  done = true;
  const auto& sc = eh::config::shell_config_snapshot_skip_matugen();
  if (!sc.dock.iconTheme.empty()) {
    s_icon_cache.set_icon_theme(sc.dock.iconTheme);
  }
  s_icon_cache.prewarm_search_dirs();
}

enum class FileIcon { Folder, Image, Audio, Video, Text, Executable, File };

static FileIcon detect_file_icon(const std::string& name, bool is_dir) {
  if (is_dir) return FileIcon::Folder;
  auto dot = name.rfind('.');
  if (dot == std::string::npos || dot == name.size() - 1) return FileIcon::File;
  std::string ext = name.substr(dot + 1);
  for (auto& c : ext) c = std::tolower(c);
  if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" ||
      ext == "bmp" || ext == "webp" || ext == "svg" || ext == "avif")
    return FileIcon::Image;
  if (ext == "mp3" || ext == "wav" || ext == "flac" || ext == "ogg" ||
      ext == "m4a" || ext == "aac" || ext == "opus")
    return FileIcon::Audio;
  if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
      ext == "webm" || ext == "m4v")
    return FileIcon::Video;
  if (ext == "txt" || ext == "md" || ext == "conf" || ext == "cfg" ||
      ext == "ini" || ext == "json" || ext == "xml" || ext == "yaml")
    return FileIcon::Text;
  if (ext == "sh" || ext == "bin" || ext == "elf" || ext == "AppImage" ||
      ext == "desktop")
    return FileIcon::Executable;
  return FileIcon::File;
}

static const char* icon_name_for_type(FileIcon fi) {
  switch (fi) {
    case FileIcon::Folder:     return "folder";
    case FileIcon::Image:      return "image-x-generic";
    case FileIcon::Audio:      return "audio-x-generic";
    case FileIcon::Video:      return "video-x-generic";
    case FileIcon::Text:       return "text-x-generic";
    case FileIcon::Executable: return "application-x-executable";
    case FileIcon::File:       return "text-x-generic";
  }
  return "unknown";
}

cairo_surface_t* FileChooserDialog::get_thumbnail(const std::string& path, int size) {
  auto it = thumb_cache_.find(path);
  if (it != thumb_cache_.end()) return it->second;
  (void)size;
  return nullptr;
}

static void draw_file_icon(cairo_t* cr, int x, int y, int size, FileIcon fi,
                           bool selected, cairo_surface_t* thumb = nullptr) {
  if (fi == FileIcon::Image && thumb) {
    double iw = static_cast<double>(cairo_image_surface_get_width(thumb));
    double ih = static_cast<double>(cairo_image_surface_get_height(thumb));
    if (iw > 0 && ih > 0) {
      double scale = size / std::max(1.0, std::max(iw, ih));
      cairo_save(cr);
      double rad = 6;
      cairo_new_path(cr);
      cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3*M_PI/2);
      cairo_arc(cr, x + size - rad, y + rad, rad, 3*M_PI/2, 2*M_PI);
      cairo_arc(cr, x + size - rad, y + size - rad, rad, 0, M_PI/2);
      cairo_arc(cr, x + rad, y + size - rad, rad, M_PI/2, M_PI);
      cairo_close_path(cr);
      cairo_clip(cr);
      cairo_translate(cr, x, y);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, thumb,
                               (size/scale - iw) / 2, (size/scale - ih) / 2);
      cairo_paint(cr);
      cairo_restore(cr);
      return;
    }
  }

  const auto* entry = s_icon_cache.tray_icon(icon_name_for_type(fi));
  if (entry && entry->surface) {
    double iw = static_cast<double>(entry->width);
    double ih = static_cast<double>(entry->height);
    double scale = size / std::max(1.0, std::max(iw, ih));
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, entry->surface,
                             (size/scale - iw) / 2, (size/scale - ih) / 2);
    cairo_paint(cr);
    cairo_restore(cr);
    return;
  }

  double r, g, b;
  switch (fi) {
    case FileIcon::Folder:     r = 0.85; g = 0.65; b = 0.20; break;
    case FileIcon::Image:      r = 0.20; g = 0.75; b = 0.40; break;
    case FileIcon::Audio:      r = 0.30; g = 0.55; b = 0.90; break;
    case FileIcon::Video:      r = 0.70; g = 0.35; b = 0.80; break;
    case FileIcon::Text:       r = 0.50; g = 0.50; b = 0.55; break;
    case FileIcon::Executable: r = 0.60; g = 0.40; b = 0.25; break;
    default:                   r = 0.40; g = 0.40; b = 0.45; break;
  }
  if (selected) { r = r * 1.3; g = g * 1.3; b = b * 1.3; }

  cairo_save(cr);
  double rad = size * 0.2;
  cairo_new_path(cr);
  cairo_arc(cr, x + rad, y + rad, rad, M_PI, 3 * M_PI / 2);
  cairo_arc(cr, x + size - rad, y + rad, rad, 3 * M_PI / 2, 2 * M_PI);
  cairo_arc(cr, x + size - rad, y + size - rad, rad, 0, M_PI / 2);
  cairo_arc(cr, x + rad, y + size - rad, rad, M_PI / 2, M_PI);
  cairo_close_path(cr);
  cairo_set_source_rgba(cr, r, g, b, 0.85);
  cairo_fill(cr);

  const char* label = "?";
  switch (fi) {
    case FileIcon::Folder:     label = "F"; break;
    case FileIcon::Image:      label = "I"; break;
    case FileIcon::Audio:      label = "A"; break;
    case FileIcon::Video:      label = "V"; break;
    case FileIcon::Text:       label = "T"; break;
    case FileIcon::Executable: label = "X"; break;
    default:                   label = "?"; break;
  }
  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, size * 0.45);
  cairo_text_extents_t te;
  cairo_text_extents(cr, label, &te);
  cairo_move_to(cr, x + (size - te.width) / 2 - te.x_bearing,
                y + (size + te.height) / 2 - te.y_bearing);
  cairo_show_text(cr, label);
  cairo_restore(cr);
}

// ── constructor ───────────────────────────────────────────────────

FileChooserDialog::FileChooserDialog(Mode mode, const char* title,
                                      const std::string& accept_type,
                                      bool multiple)
  : DialogBase(800, 600, title && title[0] ? title :
                 (mode == Mode::Open ? "Open File" :
                  mode == Mode::Directory ? "Select Folder" : "Save File")),
    mode_(mode), accept_type_(accept_type), multiple_(multiple) {
  ensure_icon_cache_initialized();
  load_dir(home_dir());

  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    filename_focused_ = true;
  }
}

FileChooserDialog::~FileChooserDialog() {
  for (auto& [_, s] : thumb_cache_) {
    if (s) cairo_surface_destroy(s);
  }
  thumb_cache_.clear();
}

// ── directory listing ─────────────────────────────────────────────

void FileChooserDialog::load_dir(const std::string& path) {
  current_path_ = path;
  entries_.clear();
  visible_.clear();
  scroll_offset_ = 0;
  hover_idx_ = -1;
  selected_idx_ = -1;
  multi_selected_.clear();

  DIR* dir = opendir(path.c_str());
  if (!dir) return;

  struct dirent* de;
  while ((de = readdir(dir)) != nullptr) {
    std::string name = de->d_name;
    if (name == "." || name == "..") continue;
    FileEntry fe;
    fe.name = name;
    fe.is_dir = (de->d_type == DT_DIR);
    if (de->d_type == DT_UNKNOWN) {
      fe.is_dir = is_dir(path + "/" + name);
    }
    entries_.push_back(std::move(fe));
  }
  closedir(dir);

  std::sort(entries_.begin(), entries_.end(),
            [](const FileEntry& a, const FileEntry& b) {
              if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
              return a.name < b.name;
            });

  rebuild_visible();
}

void FileChooserDialog::go_up() {
  fs::path p(current_path_);
  auto parent = p.parent_path();
  if (parent != p) load_dir(parent.string());
}

void FileChooserDialog::open_selected() {
  if (selected_idx_ < 0 || selected_idx_ >= static_cast<int>(entries_.size())) return;
  const auto& entry = entries_[selected_idx_];
  if (entry.is_dir) {
    load_dir(current_path_ + "/" + entry.name);
  } else if (mode_ == Mode::Open) {
    selected_uris_.clear();
    selected_uris_.push_back(uri_from_path(current_path_ + "/" + entry.name));
    finish({1, selected_uris_});
  }
}

// ── visible list ──────────────────────────────────────────────────

void FileChooserDialog::rebuild_visible() {
  visible_.clear();
  for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
    if (show_hidden_ || !entry_hidden(i)) visible_.push_back(i);
  }
}

bool FileChooserDialog::entry_hidden(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(entries_.size())) return false;
  return !entries_[idx].name.empty() && entries_[idx].name[0] == '.';
}

void FileChooserDialog::toggle_view() {
  grid_ = !grid_;
}

void FileChooserDialog::toggle_hidden() {
  show_hidden_ = !show_hidden_;
  rebuild_visible();
}

// ── hit-testing ───────────────────────────────────────────────────

int FileChooserDialog::entry_at(int x, int y) const {
  if (grid_) {
    if (x < list_x_ || x >= list_x_ + list_w_ ||
        y < list_y_ || y >= list_y_ + list_h_) return -1;
    int col = (x - list_x_) / grid_cell_w_;
    int row = (y - list_y_) / grid_cell_h();
    int idx = row * grid_cols() + col;
    if (idx >= 0 && idx < static_cast<int>(visible_.size())) return visible_[idx];
    return -1;
  }
  if (x < list_x_ || x >= list_x_ + list_w_ ||
      y < list_y_ || y >= list_y_ + list_h_) return -1;
  int row = (y - list_y_) / entry_h_;
  if (row + scroll_offset_ >= static_cast<int>(visible_.size())) return -1;
  return visible_[row + scroll_offset_];
}

// ── grid helpers ──────────────────────────────────────────────────

void FileChooserDialog::draw_grid(cairo_t* cr, int w, int h) {
  (void)w;
  (void)h;
  list_y_ = 50;
  list_h_ = h - list_y_ - 50;

  const int cols = grid_cols();
  if (cols < 1) return;

  const int total = static_cast<int>(visible_.size());
  const int cellW = grid_cell_w_;
  const int cellH = grid_cell_h();

  list_x_ = std::max(10, (w - cols * cellW) / 2);
  list_w_ = cols * cellW;

  int startRow = scroll_offset_;
  int endRow = std::min(startRow + (list_h_ / cellH), (total + cols - 1) / cols);

  for (int r = startRow; r < endRow; ++r) {
    for (int c = 0; c < cols; ++c) {
      int idx = r * cols + c;
      if (idx >= total) break;
      int entryIdx = visible_[idx];
      int cx = list_x_ + c * cellW;
      int cy = list_y_ + (r - startRow) * cellH;

      const auto& entry = entries_[entryIdx];
      bool sel = (entryIdx == selected_idx_);
      bool hov = (entryIdx == hover_idx_);

      if (sel || hov) {
        cairo_set_source_rgba(cr, 0.3, 0.5, 0.9, sel ? 0.25 : 0.12);
        cairo_rectangle(cr, cx + 2, cy + 2, cellW - 4, cellH - 4);
        cairo_fill(cr);
      }

      FileIcon fi = detect_file_icon(entry.name, entry.is_dir);
      const int iconSize = 48;
      const int iconX = cx + (cellW - iconSize) / 2;
      const int iconY = cy + 8;

      cairo_surface_t* thumb = nullptr;
      if (fi == FileIcon::Image) {
        thumb = get_thumbnail(current_path_ + "/" + entry.name, iconSize);
      }
      draw_file_icon(cr, iconX, iconY, iconSize, fi, false, thumb);

      const int labelY = iconY + iconSize + 4;
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 11);
      cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 0.95);

      std::string label = entry.name;
      cairo_text_extents_t te;
      cairo_text_extents(cr, label.c_str(), &te);
      while (te.width > cellW - 8 && label.size() > 3) {
        label = label.substr(0, label.size() - 4) + "...";
        cairo_text_extents(cr, label.c_str(), &te);
      }
      cairo_move_to(cr, cx + (cellW - te.width) / 2 - te.x_bearing, labelY + 12);
      cairo_show_text(cr, label.c_str());
    }
  }
}

int FileChooserDialog::grid_cols() const {
  if (list_w_ <= 0) return 4;
  return std::max(1, (list_w_ - 20) / grid_cell_w_);
}

int FileChooserDialog::grid_rows() const {
  return (static_cast<int>(visible_.size()) + grid_cols() - 1) / grid_cols();
}

int FileChooserDialog::grid_cell_h() const {
  return 100;
}

// ── draw ──────────────────────────────────────────────────────────

void FileChooserDialog::draw(cairo_t* cr, int w, int h) {
  // Background
  cairo_set_source_rgba(cr, 0.08, 0.10, 0.12, 0.98);
  cairo_rectangle(cr, 0, 0, w, h);
  cairo_fill(cr);

  // Title bar
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 16);
  cairo_set_source_rgba(cr, 0.92, 0.94, 0.96, 0.98);
  cairo_move_to(cr, 16, 32);
  std::string title;
  switch (mode_) {
    case Mode::Open:      title = "Open File"; break;
    case Mode::Save:      title = "Save File"; break;
    case Mode::SaveFiles: title = "Save Files"; break;
    case Mode::Directory: title = "Select Folder"; break;
  }
  cairo_show_text(cr, title.c_str());

  // Path bar
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 11);
  cairo_set_source_rgba(cr, 0.65, 0.67, 0.70, 0.85);
  cairo_move_to(cr, 16, 48);
  cairo_show_text(cr, current_path_.c_str());

  list_y_ = 56;
  list_h_ = h - list_y_ - 60;
  list_x_ = 10;
  list_w_ = w - 20;

  if (grid_) {
    draw_grid(cr, w, h);
  } else {
    const int total = static_cast<int>(visible_.size());
    const int visibleRows = list_h_ / entry_h_;
    const int start = scroll_offset_;
    const int end = std::min(start + visibleRows, total);

    for (int i = start; i < end; ++i) {
      int entryIdx = visible_[i];
      const auto& entry = entries_[entryIdx];
      int rowY = list_y_ + (i - start) * entry_h_;

      bool sel = (entryIdx == selected_idx_);
      bool hov = (entryIdx == hover_idx_);

      if (sel) {
        cairo_set_source_rgba(cr, 0.3, 0.5, 0.9, 0.25);
        cairo_rectangle(cr, list_x_, rowY, list_w_, entry_h_);
        cairo_fill(cr);
      } else if (hov) {
        cairo_set_source_rgba(cr, 0.3, 0.5, 0.9, 0.10);
        cairo_rectangle(cr, list_x_, rowY, list_w_, entry_h_);
        cairo_fill(cr);
      }

      FileIcon fi = detect_file_icon(entry.name, entry.is_dir);
      const int iconSize = 24;
      const int iconX = list_x_ + 6;
      const int iconY = rowY + (entry_h_ - iconSize) / 2;

      cairo_surface_t* thumb = nullptr;
      if (fi == FileIcon::Image) {
        thumb = get_thumbnail(current_path_ + "/" + entry.name, iconSize);
      }
      draw_file_icon(cr, iconX, iconY, iconSize, fi, false, thumb);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size(cr, 13);
      cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 0.95);
      cairo_move_to(cr, iconX + iconSize + 8, rowY + entry_h_ / 2 + 5);
      cairo_show_text(cr, entry.name.c_str());
    }
  }

  // Bottom bar
  const int bottomY = h - 44;
  cairo_set_source_rgba(cr, 0.12, 0.14, 0.16, 0.95);
  cairo_rectangle(cr, 0, bottomY, w, 44);
  cairo_fill(cr);

  // Cancel button
  cairo_set_source_rgba(cr, 0.22, 0.24, 0.27, 0.85);
  cairo_rectangle(cr, w - 180, bottomY + 6, 80, 32);
  cairo_fill(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13);
  cairo_set_source_rgba(cr, 0.90, 0.92, 0.94, 0.95);
  cairo_move_to(cr, w - 160, bottomY + 28);
  cairo_show_text(cr, "Cancel");

  // Open/Save button
  cairo_set_source_rgba(cr, 0.3, 0.5, 0.9, 0.6);
  cairo_rectangle(cr, w - 90, bottomY + 6, 80, 32);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 0.98);
  cairo_move_to(cr, w - 65, bottomY + 28);
  cairo_show_text(cr, mode_ == Mode::Open ? "Open" : "Save");

  // Filename entry in Save mode
  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    cairo_rectangle(cr, 16, h - 110, w - 32, 32);
    cairo_set_source_rgba(cr, 0.15, 0.17, 0.20, 0.9);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.85, 0.87, 0.90, 0.95);
    cairo_move_to(cr, 22, h - 88);
    cairo_show_text(cr, filename_text_.c_str());
  }
}

// ── input handlers ────────────────────────────────────────────────

void FileChooserDialog::on_click(int x, int y, int button) {
  if (button != 1) return;
  int w = 800, h = 600;

  // Cancel button
  if (x >= w - 180 && x < w - 100 && y >= h - 44 + 6 && y < h - 44 + 38) {
    finish({});
    return;
  }

  // Open/Save button
  if (x >= w - 90 && x < w - 10 && y >= h - 44 + 6 && y < h - 44 + 38) {
    if (selected_idx_ >= 0) open_selected();
    else if (mode_ == Mode::Save && !filename_text_.empty()) {
      selected_uris_.push_back(uri_from_path(current_path_ + "/" + filename_text_));
      finish({1, selected_uris_});
    }
    return;
  }

  int idx = entry_at(x, y);
  if (idx >= 0) {
    if (multiple_) {
      auto it = std::find(multi_selected_.begin(), multi_selected_.end(), idx);
      if (it != multi_selected_.end())
        multi_selected_.erase(it);
      else
        multi_selected_.push_back(idx);
    } else {
      selected_idx_ = idx;
    }
    const auto& entry = entries_[idx];
    if (entry.is_dir) {
      if (mode_ != Mode::Directory) {
        load_dir(current_path_ + "/" + entry.name);
      }
    } else if (mode_ == Mode::Open) {
      selected_uris_.clear();
      selected_uris_.push_back(uri_from_path(current_path_ + "/" + entry.name));
      finish({1, selected_uris_});
    }
  }
}

void FileChooserDialog::on_pointer_move(int x, int y) {
  hover_idx_ = entry_at(x, y);
}

void FileChooserDialog::on_scroll(int x, int y, double dx, double dy) {
  (void)x;
  (void)y;
  (void)dx;
  if (grid_) {
    scroll_offset_ = std::max(0, std::min(scroll_offset_ + (dy > 0 ? 1 : -1),
                              std::max(0, grid_rows() - (list_h_ / grid_cell_h()))));
    return;
  }
  const int total = static_cast<int>(visible_.size());
  const int visibleRows = list_h_ / entry_h_;
  if (total <= visibleRows) return;
  int delta = static_cast<int>(dy / entry_h_);
  if (delta == 0) delta = (dy > 0) ? 1 : -1;
  scroll_offset_ = std::clamp(scroll_offset_ + delta, 0, total - visibleRows);
}

void FileChooserDialog::on_key(xkb_keysym_t sym, uint32_t state,
                                const char* utf8, int utf8_len) {
  (void)state;
  (void)utf8_len;
  if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
    if (utf8 && utf8[0] >= 32) {
      filename_text_.insert(filename_cursor_++, utf8);
      return;
    }
    if (sym == 65288 && filename_cursor_ > 0) { // BackSpace
      filename_text_.erase(--filename_cursor_, 1);
      return;
    }
  }

  if (sym == 65293) { // Return/Enter
    if (selected_idx_ >= 0) open_selected();
    else if (mode_ == Mode::Save && !filename_text_.empty()) {
      selected_uris_.push_back(uri_from_path(current_path_ + "/" + filename_text_));
      finish({1, selected_uris_});
    }
    return;
  }
  if (sym == 65307) { // Escape
    finish({});
    return;
  }
  if (sym == 65289) { // Tab
    toggle_view();
    return;
  }
  if (sym == 65288) { // BackSpace (navigate up)
    go_up();
    return;
  }
  if (sym == 105) { // 'i' - toggle hidden
    toggle_hidden();
    return;
  }

  const int total = static_cast<int>(visible_.size());
  if (total == 0) return;
  if (selected_idx_ < 0) { selected_idx_ = visible_[0]; return; }

  if (grid_) {
    auto it = std::find(visible_.begin(), visible_.end(), selected_idx_);
    int pos = static_cast<int>(std::distance(visible_.begin(), it));
    int cols = grid_cols();
    if (sym == 65362 && pos >= cols) { selected_idx_ = visible_[pos - cols]; }
    if (sym == 65364 && pos + cols < total) { selected_idx_ = visible_[pos + cols]; }
    if (sym == 65361 && pos > 0) { selected_idx_ = visible_[pos - 1]; }
    if (sym == 65363 && pos + 1 < total) { selected_idx_ = visible_[pos + 1]; }
  } else {
    auto it = std::find(visible_.begin(), visible_.end(), selected_idx_);
    int pos = static_cast<int>(std::distance(visible_.begin(), it));
    if (sym == 65362 && pos > 0) { selected_idx_ = visible_[pos - 1]; }
    if (sym == 65364 && pos + 1 < total) { selected_idx_ = visible_[pos + 1]; }
  }

  if (!grid_) {
    const int visibleRows = list_h_ / entry_h_;
    auto it = std::find(visible_.begin(), visible_.end(), selected_idx_);
    int pos = static_cast<int>(std::distance(visible_.begin(), it));
    if (pos < scroll_offset_) scroll_offset_ = pos;
    if (pos >= scroll_offset_ + visibleRows) scroll_offset_ = pos - visibleRows + 1;
  }
}

std::string file_uri_to_local_path(const std::string& uri) {
  if (uri.size() > 7 && uri.substr(0, 7) == "file://") return uri.substr(7);
  return uri;
}

bool show_native_folder_picker(std::string* out_path) {
  std::string cmd = "zenity --file-selection --directory 2>/dev/null";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return false;
  char buf[4096];
  if (fgets(buf, sizeof(buf), f)) {
    std::string path(buf);
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    pclose(f);
    if (!path.empty()) {
      *out_path = path;
      return true;
    }
  }
  pclose(f);
  return false;
}

} // namespace eh::dialog
