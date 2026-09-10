#pragma once

#include "app/file_browser/app_types.hpp"

namespace eh::file_browser {

/// Start a DnD session for the currently selected files.
/// Called once the pointer moves past the drag threshold.
void start_drag(AppState& app);

/// Clean up any DnD state (data source, icon surface).
void cancel_drag(AppState& app);

/// Update the drag icon position (for pointer-move feedback).
void update_drag_icon(AppState& app);

// ── wl_data_source callbacks ────────────────────────────────────

void data_source_target(void* data, wl_data_source* src, const char* mime);
void data_source_send(void* data, wl_data_source* src, const char* mime, int32_t fd);
void data_source_cancelled(void* data, wl_data_source* src);
void data_source_dnd_drop_performed(void* data, wl_data_source* src);
void data_source_dnd_finished(void* data, wl_data_source* src);
void data_source_action(void* data, wl_data_source* src, uint32_t dnd_action);

// ── Drop receiver (wl_data_device listener) ───────────────────

void data_device_enter(void* data, wl_data_device* dev, uint32_t serial, wl_surface* surface,
                       wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer);
void data_device_leave(void* data, wl_data_device* dev);
void data_device_motion(void* data, wl_data_device* dev, uint32_t time, wl_fixed_t x, wl_fixed_t y);
void data_device_drop(void* data, wl_data_device* dev);
void data_device_data_offer(void* data, wl_data_device* dev, wl_data_offer* offer);

/// Register the wl_data_device listener to accept drops.
void setup_drop_receiver(AppState& app);

} // namespace eh::file_browser
