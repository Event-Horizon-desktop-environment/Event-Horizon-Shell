#pragma once

struct App;

void settings_clamp_dock_scroll_px(App& app);
void settings_clamp_panel_scroll_px(App& app);
void settings_clamp_taskbar_scroll_px(App& app);
void settings_clamp_sidebar_scroll_px(App& app);
void settings_clamp_autostart_scroll_px(App& app);
void settings_apply_wheel_scroll_delta(App& app, double delta_px);

// Single source of truth.
// The animated scroll offset for the active tab. Painting translates content
// by this, and hit-testing adds it back — so both always agree, even mid
// animation.
double settings_scroll_px(const App& app);
int settings_scroll_px_int(const App& app);

// The active tab's resting (non-animated) scroll offset.
int settings_scroll_raw_px(const App& app);

// After a clamp function has adjusted the resting offset (window resize,
// content height change, ...) keep the animated controller in sync so paint
// can never rest out of range.
void settings_scroll_sync_after_clamp(App& app);

// Schedule a wl_surface.frame callback + commit (to trigger the callback).
// The callback (settings_surface_frame_done) may call draw() if needed.
void schedule_settings_surface_frame(App& app);
