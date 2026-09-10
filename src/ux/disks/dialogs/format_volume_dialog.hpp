#pragma once

#include <string>
#include <vector>

namespace eh::disks {

inline constexpr int kFormatDialogW = 400;
inline constexpr int kFormatDialogH = 340;

struct FormatVolumeDialog {
  bool open = false;

  int selected_fs = 0;
  bool encrypt = false;

  int hover_item = -1;
  bool hover_cancel = false;
  bool hover_format = false;
  bool hover_encrypt = false;

  bool pending = false;
  bool result_ok = false;
  std::string result_msg;
};

}

