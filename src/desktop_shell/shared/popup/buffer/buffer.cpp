#include "desktop_shell/shared/popup/buffer/buffer.hpp"

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/dock/paint/dock_raster_backend.hpp"

namespace eh::shell::dock {

void destroy_popup_buffer(DockApp& app) {
   
  eh::dock::clear_popup_gl_raster(app);
}

}
