#pragma once

#include "desktop_shell/dock/core/dock_app.h"

void dock_pin_identity_cache_refresh(DockApp& app);
bool dock_pin_identity_list_contains_toplevel_key(DockApp& app, const std::string& normalizedToplevelAppId);
bool dock_pin_identity_norm_matches_anchor(DockApp& app, const std::string& anchorNormalizedAppKey,
                                           const std::string& normalizedToplevelAppId);
