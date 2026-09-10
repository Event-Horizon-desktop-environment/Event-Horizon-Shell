#pragma once

struct wl_display;
struct DockApp;

bool dock_init_on_display(DockApp& app, wl_display* display);
void dock_init_deferred_startup(DockApp& app);
void dock_install_loop_fds(DockApp& app);
void dock_handle_timer(DockApp& app);
void dock_handle_media_anim_timer(DockApp& app);
void dock_handle_inotify(DockApp& app);
void dock_handle_tray(DockApp& app);

void dock_try_start_deferred_tray(DockApp& app);
void dock_after_display_dispatch(DockApp& app);
void dock_schedule_frame(DockApp& app);

void dock_sync_settings_from_drag_preview(DockApp& app);
void dock_cleanup(DockApp& app, bool disconnect_display);

void launch_settings_app(DockApp& app);
