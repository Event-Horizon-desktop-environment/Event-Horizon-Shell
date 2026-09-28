#pragma once

#include <memory>

#include "wl/buffer/damage_region.hpp"

struct App;

namespace eh::config {
struct ShellConfig;
}

namespace eh::wayland {
class VulkanDisplayContext;
}

bool settings_want_vk(const App& app, const eh::config::ShellConfig& sc);
void settings_sync_renderer_backend_state(App& app, const eh::config::ShellConfig& sc);
bool settings_ensure_vk_raster(App& app, int buf_w, int buf_h);
bool settings_present_vk_raster(App& app, int buf_w, int buf_h, bool* transient_failure);
// Damage-aware present: uploads only frame_damage (unioned with each swapchain
// image's history internally); effective_damage_out receives the region the
// caller must pass to wl_surface_damage_buffer (may be empty).
bool settings_present_vk_raster_damaged(App& app, int buf_w, int buf_h, bool* transient_failure,
                                        const eh::wayland::DamageRegion& frame_damage,
                                        eh::wayland::DamageRegion* effective_damage_out);
void settings_clear_all_gpu_surfaces(App& app);

void settings_gpu_embed_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk);
void settings_gpu_reset_shell_vk();
