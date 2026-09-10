#include "desktop_shell/widgets/popup/weather/weather_popup.hpp"

#include "../../../dock/core/dock_app.h"
#include "../../../shared/popup/session/session.hpp"

namespace eh::shell::dock::popup::weather {

void dock_weather_popup_handle_click(DockApp& app, double, double, uint32_t) {
   
  popup_close(app);
}

}
