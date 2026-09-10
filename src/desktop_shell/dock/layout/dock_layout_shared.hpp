#pragma once

#include "desktop_shell/dock/core/dock_app.h"
#include "desktop_shell/shared/core/running_snapshot.hpp"

#include <cstdint>
#include <string>
#include <vector>

const DockApp::Toplevel* dock_toplevel_by_serial(const DockApp& app, std::uint64_t serial);

void dock_pinned_merge_running_slot(DockApp& app, const std::vector<eh::shell::shared::RunningGroup>& runningGroups,
                                    const std::string& normalizedPin, std::uint64_t& chosenSerial, bool& anyActivated,
                                    std::string* iconIdOut = nullptr);

bool dock_strip_widget_blocked(const DockSettings& st, const std::string& widgetToken);

bool dock_pointer_on_any_dock_layer(const DockApp& app);
