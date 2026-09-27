#pragma once

#include <cstdint>

namespace eh::shell::stage { struct StageApp; }

namespace eh::shell::dashboard {

void dashboard_pointer_enter(eh::shell::stage::StageApp& app);
void dashboard_pointer_leave(eh::shell::stage::StageApp& app);
void dashboard_pointer_motion(eh::shell::stage::StageApp& app);
void dashboard_pointer_press(eh::shell::stage::StageApp& app, std::uint32_t serial);

// Trigger strip enter: pure toggle — closed -> open (reveal),
// open -> close (dismiss). The two states live on different surfaces, so no
// latch or timer is needed: after a dismiss the pointer is still on the
// trigger surface with no fresh enter until it leaves and comes back.
void dashboard_trigger_enter(eh::shell::stage::StageApp& app);

// Returns true when a slider drag was ended by this release.
[[nodiscard]] bool dashboard_button_release(eh::shell::stage::StageApp& app);

// Returns true when the scroll was consumed over a slider.
[[nodiscard]] bool dashboard_axis(eh::shell::stage::StageApp& app, double deltaPx);

void dashboard_open(eh::shell::stage::StageApp& app);
void dashboard_close(eh::shell::stage::StageApp& app);

}  // namespace eh::shell::dashboard
