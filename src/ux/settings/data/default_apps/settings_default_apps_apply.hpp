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

// The system default that was in place before we first applied a choice for
// this category (empty if the category was never changed by the app).
[[nodiscard]] std::string captured_system_default(int category_index);

// Reverts the category to the captured system default (or drops the user-level
// override when the system had none). Used by the "Use system default" action.
void restore_category_system_default(int category_index);

}
