#pragma once

#include "desktop_shell/dock/core/dock_app.h"

#include <string>

namespace eh::shell::popup {

bool popup_handle_click(DockApp& app, double x, double y, uint32_t serial);
bool popup_handle_motion(DockApp& app);
bool popup_handle_escape(DockApp& app);
bool popup_keep_open_for_slot(DockApp::PopupKind kind, int slotKind, const std::string& slotKey);
bool popup_dispatch_paint(DockApp& app, cairo_t* cr);
bool popup_dispatch_slot(DockApp& app, int slotKind, int slotCenterX, uint32_t serial);

}
