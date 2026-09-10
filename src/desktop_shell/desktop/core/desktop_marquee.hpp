#pragma once

namespace eh::shell::desktop {

struct DesktopApp;



[[nodiscard]] bool marquee_drag_update_end_from_pointer(DesktopApp& app);

void normalize_marquee_final_rect(double& fx0, double& fy0, double& fx1, double& fy1, int w, int h);

[[nodiscard]] bool pointer_motion_marquee(DesktopApp& app);

[[nodiscard]] bool pointer_button_left_release_marquee(DesktopApp& app, bool left_button);

void desktop_left_press_marquee(DesktopApp& app);

}
