#pragma once

#include <string>

namespace hpv {

// Move a file to the FreeDesktop Trash (XDG_DATA_HOME/Trash).
// Returns true on success, false on failure.
bool trash_file(const std::string& path);

} // namespace hpv
