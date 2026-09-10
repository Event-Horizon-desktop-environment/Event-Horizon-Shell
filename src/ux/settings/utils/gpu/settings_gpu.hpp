#pragma once

#include <memory>

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
void settings_clear_all_gpu_surfaces(App& app);

void settings_gpu_embed_shell_vk(std::shared_ptr<eh::wayland::VulkanDisplayContext> vk);
void settings_gpu_reset_shell_vk();
