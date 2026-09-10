#pragma once

#include <cstdint>

// Popup-kind constants that affect margins.  These mirror (a subset of)
// DockApp::PopupKind values so the shared popup module can decide margins
// without depending on the dock type.
constexpr int kPopupKindAppMenu = 3;
constexpr int kPopupKindControlCenter = 5;
constexpr int kPopupKindCalendar = 7;
constexpr int kPopupKindWeather = 8;
constexpr int kPopupKindMediaPlayer = 13;

// Returns an extra margin (in px) that certain popup kinds need for visual
// polish (rounded corners, shadow, etc.).  `popupKind` is the integral value
// of the caller's PopupKind enum.
int popup_extra_margin(int popupKind);
