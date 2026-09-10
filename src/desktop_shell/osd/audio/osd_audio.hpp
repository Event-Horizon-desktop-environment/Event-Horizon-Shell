#pragma once

struct DockApp;

namespace eh::shell::osd {

void osd_audio_bind(DockApp& app);

void osd_audio_apply_saved_defaults(DockApp& app);

void osd_audio_poll_pending(DockApp& app);
void osd_audio_shutdown() noexcept;

}
