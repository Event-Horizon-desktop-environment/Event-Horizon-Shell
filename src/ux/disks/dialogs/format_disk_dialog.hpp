#pragma once

#include <string>

namespace eh::disks {

inline constexpr int kFormatDiskDialogW = 380;
inline constexpr int kFormatDiskDialogH = 280;

struct FormatDiskDialog {
  bool open = false;

  int selected_scheme = 0;
  int hover_item = -1;
  bool hover_cancel = false;
  bool hover_format = false;

  bool pending = false;
  bool result_ok = false;
};

}

