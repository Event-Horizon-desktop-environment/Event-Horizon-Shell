#pragma once

#include <cstdint>

struct DockApp;

namespace eh::shell::dashboard {

void dashboard_pointer_enter(DockApp& app);
void dashboard_pointer_leave(DockApp& app);
void dashboard_pointer_motion(DockApp& app);
void dashboard_pointer_press(DockApp& app, std::uint32_t serial);

// Trigger strip enter: pure toggle — closed -> open (reveal),
// open -> close (dismiss). The two states live on different surfaces, so no
// latch or timer is needed: after a dismiss the pointer is still on the
// trigger surface with no fresh enter until it leaves and comes back.
void dashboard_trigger_enter(DockApp& app);

// Returns true when a slider drag was ended by this release.
[[nodiscard]] bool dashboard_button_release(DockApp& app);

// Returns true when the scroll was consumed over a slider.
[[nodiscard]] bool dashboard_axis(DockApp& app, double deltaPx);

void dashboard_open(DockApp& app);
void dashboard_close(DockApp& app);

}  // namespace eh::shell::dashboard
