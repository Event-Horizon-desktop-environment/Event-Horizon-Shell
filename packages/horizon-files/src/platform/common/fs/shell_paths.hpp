#pragma once

#include <string>

namespace eh::shell::paths {

[[nodiscard]] std::string state_home_dir();

[[nodiscard]] std::string legacy_settings_ini_path();

[[nodiscard]] std::string normalize_desktop_app_id(std::string s);

}
