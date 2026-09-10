#pragma once

#include "configuration/shell_config.hpp"

#include <string>

namespace eh::settings::default_apps {

// Pushes a single category's default to the system via `xdg-mime default
// <id> <mime...>` so other apps (xdg-open, portals, browsers) honor it.
void apply_category_default(const std::string& desktop_id, int category_index);

// Applies every non-empty stored id; skips work entirely when the config has
// not changed since the last call, so unrelated settings saves stay cheap.
void apply_from_config(const eh::config::DefaultAppsSettings& d);

}
