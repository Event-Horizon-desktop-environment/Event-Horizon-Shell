#pragma once

#include "desktop_shell/shared/popup/geometry/margins.hpp"

struct DockApp;

void popup_finish_draw(DockApp& app, bool vk_path, bool queue_caret_followup, bool log_cc_first_paint);
