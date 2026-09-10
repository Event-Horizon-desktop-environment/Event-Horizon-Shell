#pragma once

#include <cstdint>

namespace eh::shell::desktop {

enum class DesktopMenuPanel : std::uint8_t { Main, View, SortBy, AddNew };

struct IconCtxMenuRow {
  enum class Kind : uint8_t {
    Open, OpenWith, DesktopAction, Separator, OpenLocation, RunElevated,
    PinStart, PinTaskbar, Separator2, Cut, Copy, CreateShortcut,
    Delete, Rename, Properties, ExtractArchive,
    CompressSubmenu, CompressZip, CompressTarGz, CompressTar, Compress7z,
    Mount, Unmount, EmptyTrash,
    FstabDismiss, FstabYes, FstabNo,
  };
  Kind kind = Kind::Open;
  uint16_t action_index = 0;
};

}

