#pragma once

struct DockApp;

namespace eh::shell::popup {

bool popup_handle_items_click(DockApp& app);
bool popup_handle_items_motion(DockApp& app);

}
