#pragma once

#include "dialog/dialog_base.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace eh::dialog {

/// A native file picker with Cairo-rendered UI.
class FileChooserDialog : public DialogBase {
public:
  enum class Mode { Open, Save, SaveFiles, Directory };

  FileChooserDialog(Mode mode, const char* title,
                    const std::string& accept_type,
                    bool multiple);
  ~FileChooserDialog() override;

  std::vector<std::string> selected_uris() const { return selected_uris_; }

  void set_suggested_filename(const std::string& name) {
    if (mode_ == Mode::Save || mode_ == Mode::SaveFiles) {
      filename_text_ = name;
      filename_cursor_ = static_cast<int>(filename_text_.size());
    }
  }

protected:
  void draw(cairo_t* cr, int w, int h) override;
  void on_click(int x, int y, int button) override;
  void on_pointer_move(int x, int y) override;
  void on_scroll(int x, int y, double dx, double dy) override;
  void on_key(xkb_keysym_t sym, uint32_t state,
              const char* utf8, int utf8_len) override;

private:
  struct FileEntry {
    std::string name;
    bool is_dir = false;
  };

  void load_dir(const std::string& path);
  void go_up();
  void open_selected();
  void rebuild_visible();

  // Grid <-> list helpers
  void toggle_view();
  void toggle_hidden();
  bool entry_hidden(int idx) const;
  int  entry_at(int x, int y) const;
  void draw_grid(cairo_t* cr, int w, int h);
  int  grid_cols() const;
  int  grid_rows() const;
  int  grid_cell_h() const;

  Mode mode_;
  std::string accept_type_;
  bool multiple_;
  static inline bool grid_ = false;
  static inline bool show_hidden_ = false;

  std::string current_path_;
  std::vector<FileEntry> entries_;
  std::vector<int> visible_; // indices into entries_ that pass the hidden filter
  int scroll_offset_ = 0;
  int hover_idx_ = -1;
  int selected_idx_ = -1;
  std::vector<int> multi_selected_;

  std::vector<std::string> selected_uris_;

  // filename entry (Save mode)
  std::string filename_text_;
  bool filename_focused_ = false;
  int filename_cursor_ = 0;

  cairo_surface_t* get_thumbnail(const std::string& path, int size);

  std::unordered_map<std::string, cairo_surface_t*> thumb_cache_;

  // UI layout tracking
  int list_x_ = 10, list_y_ = 50, list_w_ = 0, list_h_ = 0;
  int entry_h_ = 32;
  int grid_cell_w_ = 120;
};

/// Convert a "file://" URI to a local filesystem path.
[[nodiscard]] std::string file_uri_to_local_path(const std::string& uri);

/// Show a native folder picker dialog, blocks until done.
/// Returns true and sets \a out_path if a folder was chosen.
[[nodiscard]] bool show_native_folder_picker(std::string* out_path);

} // namespace eh::dialog
