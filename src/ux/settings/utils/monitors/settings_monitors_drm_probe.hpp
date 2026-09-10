#pragma once

#include "ux/settings/data/monitors/settings_monitors.hpp"

#include <unordered_map>

namespace eh::settings_monitors {

void drm_probe_merge_caps_into(std::unordered_map<std::string, OutputCaps>* caps);

}
