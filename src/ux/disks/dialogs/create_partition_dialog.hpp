#pragma once
// Create Partition — GParted-style: preceding/size/following + type + fs.

#include <cstdint>
#include <string>

#include "ux/disks/app_types.hpp"

namespace eh::disks {

inline constexpr int kCreatePartDlgW = 480;
inline constexpr int kCreatePartDlgH = 560;

struct CreatePartitionDialog {
  bool open = false;
  uint64_t free_offset = 0;
  uint64_t free_size = 0;
  uint64_t size = 0;  // chosen size
  int create_as = 0;  // 0=primary,1=extended,2=logical
  TextField name;
  int fs_idx = 0;
  TextField label;
  bool encrypt = false;
  int focus = 0;  // 0=size slider area,1=name,2=label
  double slider_grab = -1;  // -1 none, else 0..1 handle drag
  int hover_create_as = -1;
  int hover_fs = -1;
  bool hover_encrypt = false;
  bool hover_cancel = false;
  bool hover_create = false;
  bool pending = false;
  std::string error;
};

void draw_create_partition_dialog(struct AppState& app, cairo_t* cr);
bool create_partition_dialog_click(struct AppState& app, int x, int y);
void create_partition_dialog_move(struct AppState& app, int x, int y);
bool create_partition_dialog_key(struct AppState& app, uint32_t sym, const char* utf8,
                                 int len);

}  // namespace eh::disks
