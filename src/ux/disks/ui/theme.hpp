#pragma once
// Horizon Disks — design tokens.
// Reference: GNOME Disks 51 (GTK4/LibAdwaita port) + GParted column model.
// All metrics are in logical px, 8pt grid, 1px hairlines at +0.5 offsets.

namespace eh::disks::theme {

// Window
inline constexpr int kWinMinW = 1024;
inline constexpr int kWinMinH = 640;
inline constexpr int kWinDefaultW = 1180;
inline constexpr int kWinDefaultH = 720;

// Header (Adwaita headerbar height)
inline constexpr int kHeaderH = 52;
inline constexpr int kHeaderBtnH = 34;
inline constexpr int kHeaderIconBtn = 34;

// Sidebar (fixed width — pixel perfect, no dynamic resizing)
inline constexpr int kSidebarW = 288;
inline constexpr int kSidebarRowH = 64;
inline constexpr int kSidebarPad = 8;
inline constexpr int kSidebarSectionH = 28;

// Status / job footer
inline constexpr int kStatusH = 30;
inline constexpr int kJobBarH = 30;

// Content
inline constexpr int kContentPad = 16;
inline constexpr int kCardRadius = 12;
inline constexpr int kCardPad = 16;
inline constexpr int kRowRadius = 8;
inline constexpr int kBtnH = 32;
inline constexpr int kIconBtn = 32;

// Drive header card
inline constexpr int kDriveIconPx = 48;
inline constexpr int kDriveHeadH = 96;

// Volume bar (Disks 51 visual space-allocation bar)
inline constexpr int kVolBarH = 36;
inline constexpr int kVolBarRadius = 8;
inline constexpr int kVolBarGap = 2;
inline constexpr int kVolBarMinPx = 6;
inline constexpr int kVolLegendH = 22;

// Block table (GParted columns)
inline constexpr int kTableHeadH = 30;
inline constexpr int kBlockRowH = 56;
inline constexpr int kSwatchPx = 12;
inline constexpr int kUsageBarW = 84;
inline constexpr int kUsageBarH = 8;

// Menus / dialogs
inline constexpr int kMenuItemH = 34;
inline constexpr int kMenuPad = 6;
inline constexpr int kMenuRadius = 10;
inline constexpr int kDlgRadius = 14;
inline constexpr int kFieldH = 34;

// Typography (px)
inline constexpr int kFsTitle = 18;    // drive model
inline constexpr int kFsSection = 14;  // "Volumes"
inline constexpr int kFsBody = 13;
inline constexpr int kFsSecondary = 12;
inline constexpr int kFsCaption = 11;
inline constexpr int kFsTiny = 10;
inline constexpr int kFsTableHead = 11;

// Z-order / alpha
inline constexpr double kAlphaHover = 0.06;
inline constexpr double kAlphaPressed = 0.10;
inline constexpr double kAlphaSelected = 0.16;
inline constexpr double kAlphaDisabled = 0.38;
inline constexpr double kAlphaScrim = 0.55;

}  // namespace eh::disks::theme
