#include "desktop_shell/shared/popup/geometry/margins.hpp"

int popup_extra_margin(int popupKind) {
  if (popupKind == kPopupKindAppMenu ||
      popupKind == kPopupKindControlCenter ||
      popupKind == kPopupKindCalendar ||
      popupKind == kPopupKindWeather ||
      popupKind == kPopupKindMediaPlayer) {
    return 2;
  }
  return 0;
}
