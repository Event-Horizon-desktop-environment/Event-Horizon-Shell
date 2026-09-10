#pragma once

struct DockApp;

namespace eh::shell::dock {

void dock_popup_destroy_caret_frame(DockApp& app);
void dock_popup_queue_followup_frame(DockApp& app);

}
