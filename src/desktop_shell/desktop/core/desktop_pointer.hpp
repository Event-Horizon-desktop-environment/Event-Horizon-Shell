#pragma once

#include <cstddef>

namespace eh::shell::desktop {

struct DesktopApp;



[[nodiscard]] bool desktop_pointer_local_xy(DesktopApp& app, size_t layer_idx, double* lx, double* ly);

}
