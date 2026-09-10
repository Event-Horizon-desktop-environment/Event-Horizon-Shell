#include "desktop_shell/widgets/popup/calendar/calendar_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"

namespace eh::shell::dock::popup::calendar {

void dock_calendar_popup_handle_click(DockApp& app, double, double, uint32_t) {
   
  popup_close(app);
}

}
