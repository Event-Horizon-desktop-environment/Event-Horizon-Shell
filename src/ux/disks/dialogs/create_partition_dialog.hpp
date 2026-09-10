#pragma once

#include <string>

namespace eh::disks {

inline constexpr int kCreatePartDlgW = 420;
inline constexpr int kCreatePartDlgH = 380;

struct CreatePartitionDialog {
  bool open = false;

  int page = 0; // 0=size, 1=fs, 2=confirm

  // Page 0: size
  int size_pct = 100; // percent of free space

  // Page 1: filesystem
  int selected_fs = 0;

  // Page 2: confirm
  std::string name;
  bool encrypt = false;

  // Hover tracking
  int hover_item = -1;
  bool hover_cancel = false;
  bool hover_next = false;
  bool hover_back = false;
  bool hover_create = false;
  bool hover_encrypt = false;
  int hover_pct = -1;

  bool pending = false;
  bool result_ok = false;
};

}

