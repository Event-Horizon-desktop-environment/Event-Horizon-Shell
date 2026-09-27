#pragma once

namespace eh::shell::stage { struct StageApp; }

namespace eh::shell::osd {

void osd_audio_bind(eh::shell::stage::StageApp& app);

void osd_audio_apply_saved_defaults();

void osd_audio_poll_pending(eh::shell::stage::StageApp& app);
void osd_audio_shutdown() noexcept;

}
