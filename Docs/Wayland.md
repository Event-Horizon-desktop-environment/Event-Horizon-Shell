# Wayland Protocol Reference

Comprehensive reference of every Wayland protocol available, their status in Event Horizon, and how they're used.

---

## Legend

| Column | Meaning |
|---|---|
| **Status** | `stable` / `staging` / `unstable` / `deprecated` / `vendored` (in `protocols/`) |
| **In EH?** | `YES` if the protocol XML is in `meson.build` and header in `protocols.hpp` |
| **Used?** | `YES` if any C++ code calls the protocol; `partial` if bound but unused; `no` if not yet wired |
| **Binding** | Where the protocol manager is obtained (`connection.cpp` or `unified_wayland_registry.cpp`) |
| **Support** | Which compositors implement the protocol |

---

## Core Wayland Protocol

Always available — no XML to vendor. These interfaces are defined in the core `wayland.xml` and shipped with `wayland-client.h`.

| Interface | In EH? | Used? | Purpose |
|---|---|---|---|
| `wl_display` | built-in | YES | Core display connection, event queue, roundtrip |
| `wl_registry` | built-in | YES | Global object registry — binding all protocols |
| `wl_compositor` | built-in | YES | Create `wl_surface` objects |
| `wl_subcompositor` | built-in | YES | Create sub-surfaces (used by popups, tooltips) |
| `wl_surface` | built-in | YES | On-screen pixel surface |
| `wl_region` | built-in | YES | Surface input/opaque region (used by OSD, dock) |
| `wl_buffer` | built-in | YES | Pixel buffer (SHM or DMA-BUF) |
| `wl_shm` | built-in | YES | Shared memory pool for CPU-rendered buffers |
| `wl_shm_pool` | built-in | YES | SHM pool allocation |
| `wl_seat` | built-in | YES | Input seat — pointer, keyboard, touch |
| `wl_pointer` | built-in | YES | Pointer input device |
| `wl_keyboard` | built-in | YES | Keyboard input device |
| `wl_touch` | built-in | YES | Touch input device |
| `wl_output` | built-in | YES | Monitor/output geometry and properties |
| `wl_data_device_manager` | built-in | YES | Drag-and-drop and clipboard |
| `wl_data_device` | built-in | YES | Clipboard/dnd device per seat |
| `wl_data_offer` | built-in | YES | Offered MIME types during dnd/copy |
| `wl_data_source` | built-in | YES | Source side of data transfer |
| `wl_callback` | built-in | YES | Frame callback for sync |
| `wl_shell` / `wl_shell_surface` | built-in | no | Legacy shell (superseded by xdg-shell) |
| `wl_fixes` | built-in | no | Error reporting (rarely used) |

---

## Stable Protocols (`/usr/share/wayland-protocols/stable/`)

### `xdg-shell`

| Field | Value |
|---|---|
| **Interface** | `xdg_wm_base` |
| **Status** | stable |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp`, `unified_wayland_registry.cpp` |
| **Purpose** | Desktop-style windows: toplevels, popups, maximized/minimized/fullscreen states, window geometry, resize edges |
| **Used by** | Settings app, dialog windows, standalone apps |

### `viewporter`

| Field | Value |
|---|---|
| **Interfaces** | `wp_viewporter`, `wp_viewport` |
| **Status** | stable |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Crop and scale surface content without re-rendering |
| **Used by** | Desktop background, dock, taskbar for fractional-scale fallback |

### `presentation-time`

| Field | Value |
|---|---|
| **Interfaces** | `wp_presentation`, `wp_presentation_feedback` |
| **Status** | stable |
| **In EH?** | **NO** |
| **Used?** | — |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Receive precise timestamps when a buffer is actually shown on screen. Critical for benchmarking, smooth animation tuning, video playback sync |
| **Add to EH** | Useful for the Vulkan rendering path to measure frame presentation latency; could feed into a benchmark OSD |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp`, wrap in `surface_extensions.cpp` |

### `tablet-v2`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_tablet_manager_v2`, `zwp_tablet_seat_v2`, `zwp_tablet_v2`, `zwp_tablet_tool_v2`, `zwp_tablet_pad_v2` |
| **Status** | stable |
| **In EH?** | **NO** |
| **Used?** | — |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Graphics tablet support: pen pressure, tilt, barrel rotation, eraser detection, pad buttons/strips/rings |
| **Add to EH** | Enable pen input on desktop widgets, annotate screenshots, pressure-sensitive drawing in the shell |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp`, create `TabletService` in `src/wl/` |

### `linux-dmabuf-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_linux_dmabuf_v1`, `zwp_linux_buffer_params_v1` |
| **Status** | stable |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Zero-copy GPU buffer sharing via DMA-BUF file descriptors |
| **Used by** | Vulkan renderer (`vulkan_wayland.cpp`) for GPU-rendered layer surfaces |

---

## Staging Protocols (`/usr/share/wayland-protocols/staging/`)

### `ext-session-lock-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_session_lock_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `unified_wayland_registry.cpp` |
| **Purpose** | Lock the session with custom graphics — fullscreen lock surfaces on every output |
| **Used by** | `LockScreen` in `desktop_shell/lockscreen/` |

### `ext-workspace-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_workspace_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | List and control workspaces (virtual desktops) via compositor events |
| **Used by** | Workspace manager in desktop shell, workspace strip widget |

### `ext-idle-notify-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_idle_notifier_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Get notified when the user has been idle for a duration |
| **Used by** | `IdleService` — screen blanking, auto-lock, power management |

### `ext-background-effect-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_background_effect_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Request compositor-side background blur or other effects on surfaces |
| **Used by** | Desktop background layer for blur behind transparent dock/taskbar |

### `ext-data-control-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_data_control_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Full clipboard and drag-and-drop control from a privileged client |
| **Used by** | Clipboard manager, clipboard history |

### `ext-foreign-toplevel-list-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_foreign_toplevel_list_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES (Hyprland only) |
| **Binding** | `connection.cpp`, `unified_wayland_registry.cpp` |
| **Purpose** | List and track open toplevel windows |
| **Used by** | `ExtForeignToplevels`, dock taskbar, window switcher |

### `ext-image-capture-source-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_image_capture_source_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | — (bound but unused) |
| **Binding** | `connection.cpp` |
| **Purpose** | Create opaque image capture source objects (outputs or toplevels) |
| **Used by** | — (precursor to `ext-image-copy-capture-v1`) |

### `ext-image-copy-capture-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_image_copy_capture_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | partial — `ImageCopyCapture` RAII wrapper in `src/wl/image_copy_capture.hpp` |
| **Binding** | `connection.cpp` |
| **Purpose** | Capture image content into client-allocated buffers (zero-copy screenshots) |
| **Used by** | — (future replacement for wlr-screencopy) |

### `fractional-scale-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_fractional_scale_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Get per-surface fractional scale factors (e.g., 1.5×, 1.25×) for HiDPI |
| **Used by** | Dock, taskbar, desktop — all layer surfaces |

### `single-pixel-buffer-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_single_pixel_buffer_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | partial — `SinglePixelBuffer` RAII wrapper in `src/wl/single_pixel_buffer.hpp` |
| **Binding** | `connection.cpp` |
| **Purpose** | Create 1×1 pixel buffers of a single color — useful for solid-color surfaces without allocating SHM |
| **Used by** | — (could replace solid-color SHM buffers in backgrounds) |

### `tearing-control-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_tearing_control_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | partial — `TearingControl` RAII wrapper in `src/wl/tearing_control.hpp` |
| **Binding** | `connection.cpp` |
| **Purpose** | Allow surfaces to present with tearing (no vsync) for lower latency in games |
| **Used by** | — (could be exposed in settings or per-app profiles) |

### `xdg-activation-v1`

| Field | Value |
|---|---|
| **Interface** | `xdg_activation_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Request focus/activation for a toplevel surface via activation tokens |
| **Used by** | `ActivationService` — launching apps with proper focus |

### `xdg-dialog-v1`

| Field | Value |
|---|---|
| **Interface** | `xdg_wm_dialog_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Mark a toplevel as a modal dialog of another toplevel |
| **Used by** | Dialog windows, file chooser, settings dialogs |

### `color-management-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_color_manager_v1` |
| **Status** | staging |
| **In EH?** | YES |
| **Used?** | partial — `ColorManager` RAII wrapper in `src/wl/color_management.hpp` |
| **Binding** | `connection.cpp` |
| **Purpose** | Color management: HDR, wide gamut, ICC profiles, SDR brightness |
| **Used by** | — (future: HDR wallpaper, HDR color picker, nightlight) |

### `cursor-shape-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_cursor_shape_manager_v1` |
| **Status** | staging |
| **In EH?** | Vendored in `protocols/` (older version) |
| **Used?** | **NO** |
| **Support** | wlroots, KWin, Hyprland |
| **Purpose** | Set cursor image by shape name (e.g., "default", "pointer", "text", "crosshair") — no need for cursor surfaces |
| **Add to EH** | Replace all `wl_cursor` / cursor surface code with simple shape requests |
| **How** | Add to `meson.build` from system (staging) path, bind in `connection.cpp`, use wherever cursor images are set |

### `alpha-modifier-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_alpha_modifier_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Set per-surface alpha multiplier. Compositor applies the alpha during blending |
| **Used by** | — |
| **Add to EH** | Could replace software alpha in fade animations for surfaces (lock screen, OSD, notifications) |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp`, integrate into `SurfaceExtensions` |

### `color-representation-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_color_representation_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, Mutter |
| **Purpose** | Additional color representation metadata: alpha mode, pixel format hints |
| **Used by** | — Companion to `color-management-v1` |
| **Add to EH** | Only if implementing full color-management pipeline |

### `commit-timing-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_commit_timing_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, Mutter |
| **Purpose** | Let clients hint desired commit timing for smoother frame scheduling (adaptive sync) |
| **Used by** | — |
| **Add to EH** | Could optimize animation smoothness for the compositor |

### `content-type-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_content_type_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Declare surface content type: "photo", "video", "game", "ui" — compositor optimizes scanout |
| **Used by** | — |
| **Add to EH** | Useful for media surfaces to indicate video/game content for VRR optimization |

### `drm-lease-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_drm_lease_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots (specific wlr-output-management) |
| **Purpose** | Lease DRM connectors to clients (VR headsets, external displays directly driven) |
| **Used by** | VR runtimes (Monado, SteamVR) |
| **Add to EH** | Not directly relevant to a desktop shell |

### `ext-transient-seat-v1`

| Field | Value |
|---|---|
| **Interface** | `ext_transient_seat_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, Mutter |
| **Purpose** | Create temporary Wayland seats for remote desktop, input injection, test automation |
| **Used by** | — |
| **Add to EH** | Could enable remote desktop / input forwarding features |

### `fifo-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_fifo_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots |
| **Purpose** | Control fifo (vsync) presentation mode per-surface. Complementary to `tearing-control-v1` |
| **Used by** | — |
| **Add to EH** | Alongside tearing control for per-app VRR settings |

### `linux-drm-syncobj-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_linux_drm_syncobj_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots |
| **Purpose** | Explicit synchronization with DRM sync objects (timeline semaphores). Modern replacement for `linux-explicit-synchronization-unstable-v1` |
| **Used by** | Vulkan/OpenGL rendering pipelines |
| **Add to EH** | Pair with Vulkan renderer for proper explicit sync |

### `pointer-warp-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_pointer_warp_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, Mutter |
| **Purpose** | Warp the pointer cursor to a specific position on a surface (useful for RDP/VNC, lock screen) |
| **Used by** | — |
| **Add to EH** | Lock screen could warp pointer to center; remote desktop support |

### `security-context-v1`

| Field | Value |
|---|---|
| **Interface** | `wp_security_context_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, Mutter |
| **Purpose** | Create a restricted Wayland connection: sandboxing clients, limiting protocol access |
| **Used by** | Flatpak, portal backends |
| **Add to EH** | Not directly relevant — more for compositor/compositor integration |

### `xdg-session-management-v1`

| Field | Value |
|---|---|
| **Interface** | `xdg_session_manager_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | Mutter, (wlroots pending) |
| **Purpose** | Session management protocol: save and restore application state across logouts |
| **Used by** | — |
| **Add to EH** | Could integrate with the logout dialog to save/restore app state |

### `xdg-system-bell-v1`

| Field | Value |
|---|---|
| **Interface** | `xdg_system_bell_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | KWin, Mutter |
| **Purpose** | Ring the system bell — compositor handles visual/audio feedback |
| **Used by** | — |
| **Add to EH** | Could replace custom bell implementation in terminal integration |

### `xdg-toplevel-drag-v1`

| Field | Value |
|---|---|
| **Interfaces** | `xdg_toplevel_drag_manager_v1`, `xdg_toplevel_drag_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | KWin, Mutter |
| **Purpose** | Drag a toplevel surface by its thumbnail during DnD (e.g., drag browser tab out) |
| **Used by** | — |
| **Add to EH** | Could enable workspace-organized drag-and-drop of windows |

### `xdg-toplevel-icon-v1`

| Field | Value |
|---|---|
| **Interfaces** | `xdg_toplevel_icon_manager_v1`, `xdg_toplevel_icon_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | KWin, Mutter |
| **Purpose** | Set icons on toplevel windows (replaces legacy WM_HINTS icon) |
| **Used by** | — |
| **Add to EH** | Could provide window icons via a dedicated protocol instead of app ID lookup |

### `xdg-toplevel-tag-v1`

| Field | Value |
|---|---|
| **Interfaces** | `xdg_toplevel_tag_manager_v1`, `xdg_toplevel_tag_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | KWin, Mutter |
| **Purpose** | Tag toplevel windows with strings for grouping, filtering, or identification |
| **Used by** | — |
| **Add to EH** | Workspace-per-tag, smart window grouping in taskbar |

### `xwayland-shell-v1`

| Field | Value |
|---|---|
| **Interface** | `xwayland_shell_v1` |
| **Status** | staging |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Associate X11 windows with `wl_surface` handles via XWayland (for override-redirect, etc.) |
| **Used by** | — |
| **Add to EH** | Improve XWayland window integration in dock/taskbar |

---

## Unstable Protocols (`/usr/share/wayland-protocols/unstable/`)

### `pointer-constraints-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_pointer_constraints_v1`, `zwp_locked_pointer_v1`, `zwp_confined_pointer_v1` |
| **Status** | unstable |
| **In EH?** | YES |
| **Used?** | YES (new — `PointerLock`, `PointerConfine` in `src/wl/pointer_constraints.cpp`) |
| **Binding** | `connection.cpp` |
| **Purpose** | Lock pointer to a surface (hide cursor, relative motion) or confine within surface bounds |
| **Used by** | Lock screen (pointer confinement), future drag operations |

### `relative-pointer-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_relative_pointer_manager_v1`, `zwp_relative_pointer_v1` |
| **Status** | unstable |
| **In EH?** | YES |
| **Used?** | YES (new — `RelativePointer` in `src/wl/pointer_constraints.cpp`) |
| **Binding** | `connection.cpp` |
| **Purpose** | Receive relative pointer motion deltas (dx, dy) — required complement to pointer lock |
| **Used by** | Lock screen (future: drag operations, custom cursor handling) |

### `keyboard-shortcuts-inhibit-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_keyboard_shortcuts_inhibit_manager_v1`, `zwp_keyboard_shortcuts_inhibitor_v1` |
| **Status** | unstable |
| **In EH?** | YES |
| **Used?** | partial — `KeyboardShortcutsInhibit` RAII wrapper in `src/wl/keyboard_shortcuts_inhibit.hpp` |
| **Binding** | `connection.cpp` |
| **Purpose** | Let fullscreen/gaming clients inhibit compositor keyboard shortcuts |
| **Used by** | — (could integrate with game detection for auto-inhibit) |

### `idle-inhibit-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwp_idle_inhibit_manager_v1` |
| **Status** | unstable |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Prevent the compositor from idling/screensaving for a specific surface |
| **Used by** | `IdleInhibitor` — fullscreen video detection, presentation mode |

### `xdg-output-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zxdg_output_manager_v1` |
| **Status** | unstable |
| **In EH?** | YES |
| **Used?** | YES |
| **Binding** | `connection.cpp` |
| **Purpose** | Extended output geometry: logical position, logical size, name, description |
| **Used by** | Output management, monitor layout, workspace per output |

### `pointer-gestures-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_pointer_gestures_v1`, `zwp_pointer_gesture_swipe_v1`, `zwp_pointer_gesture_pinch_v1`, `zwp_pointer_gesture_hold_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter, Hyprland |
| **Purpose** | Semantic touchpad gestures: swipe (3-finger workspace switch), pinch (zoom), hold |
| **Add to EH** | Enable workspace switching via 3-finger swipe, overview via pinch |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp`, create `PointerGestureService` in `src/wl/` |

### `primary-selection-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_primary_selection_device_manager_v1`, `zwp_primary_selection_device_v1`, `zwp_primary_selection_source_v1`, `zwp_primary_selection_offer_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Primary selection (middle-click paste) — separate from regular clipboard |
| **Add to EH** | Enable primary selection clipboard management |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp` |

### `text-input-unstable-v3`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_text_input_manager_v3`, `zwp_text_input_v3` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter, Hyprland |
| **Purpose** | Input method framework — compose text with IMEs for CJK and other non-Latin scripts |
| **Add to EH** | Enable text input in spotlight search, settings search fields, lock screen password |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp` |

### `input-method-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_input_method_manager_v1`, `zwp_input_method_v1`, `zwp_input_method_context_v1` |
| **Status** | unstable (older, paired with `text-input-unstable-v1`) |
| **In EH?** | **NO** |
| **Support** | wlroots |
| **Purpose** | Older IME protocol — superseded by `text-input-unstable-v3` |
| **Add to EH** | Prefer v3 instead |

### `tablet-unstable-v2`

| Field | Value |
|---|---|
| **Interfaces** | (same as stable `tablet-v2` but unstable) |
| **Status** | unstable (superseded by stable `tablet-v2`) |
| **In EH?** | **NO** |
| **Add to EH** | Use stable `tablet-v2` instead |

### `tablet-unstable-v1`

| Field | Value |
|---|---|
| **Status** | unstable (superseded by v2) |
| **In EH?** | **NO** |
| **Add to EH** | Use stable `tablet-v2` instead |

### `xdg-decoration-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zxdg_decoration_manager_v1`, `zxdg_toplevel_decoration_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | Let clients choose between client-side (CSD) and server-side (SSD) decorations |
| **Add to EH** | Could request SSD for settings/dialog windows to have consistent look |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp` |

### `xdg-foreign-unstable-v2`

| Field | Value |
|---|---|
| **Interfaces** | `zxdg_exporter_v2`, `zxdg_importer_v2`, `zxdg_exported_v2`, `zxdg_imported_v2` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | KWin, Mutter (wlroots: wlr-foreign-toplevel) |
| **Purpose** | Export a surface handle for embedding in another client (parent-child relationships) |
| **Add to EH** | Could enable embedding app previews directly in shell surfaces |

### `xdg-foreign-unstable-v1`

| Field | Value |
|---|---|
| **Status** | unstable (older version, superseded by v2) |
| **In EH?** | **NO** |
| **Add to EH** | Use v2 instead |

### `linux-explicit-synchronization-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_linux_explicit_synchronization_v1`, `zwp_linux_surface_synchronization_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin |
| **Purpose** | Explicit acquire/release fences for DMA-BUF buffers. Required for proper GL/Vulkan rendering without implicit sync |
| **Add to EH** | Pair with Vulkan renderer. Consider `linux-drm-syncobj-v1` (staging) instead |
| **How** | Add to `meson.build` & `protocols.hpp`, bind in `connection.cpp` |

### `input-timestamps-unstable-v1`

| Field | Value |
|---|---|
| **Interfaces** | `zwp_input_timestamps_manager_v1`, `zwp_input_timestamps_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots, KWin, Mutter |
| **Purpose** | High-resolution timestamps (µs) on input events — more precise than `wl_pointer.motion` time |
| **Used by** | Drawing apps, games, latency measurement |
| **Add to EH** | Could improve gesture recognition accuracy |

### `fullscreen-shell-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwp_fullscreen_shell_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | Minimal (legacy, rarely implemented) |
| **Purpose** | Display a single surface per output in fullscreen (kiosk mode, no compositor UI) |
| **Add to EH** | Not relevant for a desktop shell |

### `xwayland-keyboard-grab-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwp_xwayland_keyboard_grab_manager_v1` |
| **Status** | unstable |
| **In EH?** | **NO** |
| **Support** | wlroots |
| **Purpose** | Let XWayland grab the keyboard exclusively (needed for X11 apps that grab input) |
| **Add to EH** | Rarely needed directly; compositor handles this transparently |

### `xdg-shell-unstable-v5` / `xdg-shell-unstable-v6`

| Field | Value |
|---|---|
| **Status** | deprecated / superseded by stable `xdg-shell` |
| **In EH?** | NO |
| **Add to EH** | Do not use |

---

## Vendored Protocols (`protocols/` directory)

### `wlr-layer-shell-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwlr_layer_shell_v1` |
| **Status** | vendored (not in system wayland-protocols) |
| **In EH?** | YES |
| **Used?** | YES (core of all shell surfaces) |
| **Purpose** | Create surfaces that live in compositor layers: backgrounds, panels, notifications, OSDs |
| **Used by** | Dock, taskbar, desktop, OSD, notifications, lock screen, wallpaper — every surface |

### `wlr-foreign-toplevel-management-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwlr_foreign_toplevel_manager_v1` |
| **Status** | vendored |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | List and control open windows (used by dock taskbar, Alt+Tab, minimize/close) |
| **Used by** | `ForeignToplevelList` — for Sway, Niri, Mango (non-Hyprland) |

### `wlr-data-control-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwlr_data_control_manager_v1` |
| **Status** | vendored |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | Clipboard management from a privileged client |
| **Used by** | Clipboard manager, clipboard history |

### `wlr-screencopy-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwlr_screencopy_manager_v1` |
| **Status** | vendored |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | Capture screen content into client-allocated buffers |
| **Used by** | Screenshot tool, wallpaper thumbnailer |

### `wlr-gamma-control-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwlr_gamma_control_manager_v1` |
| **Status** | vendored |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | Set per-output gamma ramps for nightlight/color temperature adjustment |
| **Used by** | `GammaService` — nightlight feature |

### `virtual-keyboard-unstable-v1`

| Field | Value |
|---|---|
| **Interface** | `zwp_virtual_keyboard_manager_v1` |
| **Status** | vendored |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | Create virtual keyboards for input simulation (OSK, automation) |
| **Used by** | On-screen keyboard, accessibility, remote input |

### `dwl-ipc-unstable-v2`

| Field | Value |
|---|---|
| **Interface** | `zdwl_ipc_manager_v2` |
| **Status** | vendored (from dwl WM) |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | dwl-specific IPC protocol: tags, layouts, status |
| **Used by** | Mango/Triad workspace and window management backend |

### `hyprland-toplevel-mapping-v1`

| Field | Value |
|---|---|
| **Interface** | `hyprland_toplevel_mapping_manager_v1` |
| **Status** | vendored (Hyprland-specific) |
| **In EH?** | YES |
| **Used?** | YES |
| **Purpose** | Map Hyprland window addresses to toplevel handles |
| **Used by** | Hyprland backend for workspace/window management |

### `hyprland-focus-grab-v1`

| Field | Value |
|---|---|
| **Interfaces** | `hyprland_focus_grab_manager_v1`, `hyprland_focus_grab_v1` |
| **Status** | vendored (Hyprland-specific) |
| **In EH?** | In `protocols/` but NOT in `meson.build` or `protocols.hpp` |
| **Used?** | **NO** |
| **Support** | Hyprland only |
| **Purpose** | Limit input focus to a set of surfaces (grab), excluding others. Useful for modal dialogs, lock screens, OSDs that must capture input |
| **Add to EH** | Could improve lock screen — grab focus so only lock surfaces receive input |

### `cursor-shape-v1` (vendored copy)

| Field | Value |
|---|---|
| **Interface** | `wp_cursor_shape_manager_v1` |
| **Status** | vendored (older version) — system has newer in staging |
| **In EH?** | In `protocols/` but NOT in `meson.build` or `protocols.hpp` |
| **Used?** | **NO** |
| **Add to EH** | Use system staging version instead — remove vendored copy |

---

## Protocols Summary — What to Add

### High priority (clear benefit for shell UX)

| Protocol | Effort | Impact |
|---|---|---|
| `cursor-shape-v1` | low | Replace all cursor surface code with simple shape names |
| `pointer-gestures-unstable-v1` | medium | Touchpad workspace switch, pinch-to-zoom in overview |
| `primary-selection-unstable-v1` | low | Middle-click paste clipboard |
| `text-input-unstable-v3` | medium | IME support for CJK/other non-Latin text input |
| `tablet-v2` | medium | Pen/tablet input for desktop widgets and drawing |
| `xdg-decoration-unstable-v1` | low | Request server-side decorations for shell windows |

### Medium priority

| Protocol | Effort | Impact |
|---|---|---|
| `presentation-time` | low | Frame timing feedback for animation tuning |
| `alpha-modifier-v1` | low | Hardware-accelerated fade animations |
| `content-type-v1` | low | Hint video/game content for compositor optimization |
| `commit-timing-v1` | low | Smoother animation frame scheduling |
| `linux-explicit-synchronization-unstable-v1` | medium | Proper Vulkan rendering sync |
| `linux-drm-syncobj-v1` | medium | Modern replacement for above |
| `pointer-warp-v1` | low | Warp cursor on lock screen / remote desktop |
| `ext-transient-seat-v1` | high (effort) | Remote desktop, input injection |

### Low priority / compositor internals

| Protocol | Reason |
|---|---|
| `xdg-foreign-unstable-v2` | Surface embedding — niche |
| `xdg-toplevel-drag-v1` | Tab drag-out — requires compositor support |
| `xdg-toplevel-icon-v1` | Window icons — app ID lookup already works |
| `xdg-toplevel-tag-v1` | Window tags — useful but compositor-dependent |
| `xdg-session-management-v1` | Session save/restore — compositor support limited |
| `xdg-system-bell-v1` | System bell — rarely used |
| `xwayland-shell-v1` | XWayland integration — compositor handles most cases |
| `fifo-v1` | VRR per-surface control — niche |
| `color-representation-v1` | Color pipeline detail — only with full CMS |
| `drm-lease-v1` | VR headsets — not relevant for shell |
| `security-context-v1` | Sandboxing — compositor-side |
| `fullscreen-shell-unstable-v1` | Kiosk — not relevant |
| `input-method-unstable-v1` | Superseded by text-input-v3 |
| `input-timestamps-unstable-v1` | High-res timestamps — marginal benefit |
| `xwayland-keyboard-grab-unstable-v1` | XWayland internal |
| `hyprland-focus-grab-v1` | Hyprland-only |
| `xdg-shell-unstable-v5/v6` | Deprecated |
| `tablet-unstable-v1/v2` | Superseded by stable tablet-v2 |
| `linux-dmabuf-unstable-v1` | Superseded by stable linux-dmabuf-v1 |

---

## Implementation Guide

To add a new protocol, follow these steps:

### 1. Add XML to `meson.build`

```meson
# In the foreach block (lines 73-135), add for system protocols:
['protocol-name',
  wayland_protos_dir / 'staging/protocol-name/protocol-name-v1.xml'],

# For vendored protocols:
['protocol-name',
  'protocols/protocol-name.xml'],
```

### 2. Add generated header to `protocols.hpp`

```cpp
#include "protocol-name-v1-client-protocol.h"
```

### 3. Bind in registry

- If the protocol is used by the shell (dock/taskbar/etc): add field to `DockApp` in `dock_app.h` and bind in `unified_wayland_registry.cpp`
- If the protocol is low-level: bind in `connection.cpp` and expose via `WaylandConnection` accessor

### 4. Create wrapper in `src/wl/`

Follow the existing patterns:
- Simple RAII wrapper (like `IdleInhibitor`, `PointerLock`)
- Service class (like `GammaService`, `VirtualKeyboardService`)
- Add the `.cpp` to `shell_cpp_sources` in `meson.build`

### 5. Bump protocol version

When binding, cap the version to what the code supports:
```cpp
std::min<uint32_t>(version, 1)  // if only v1 is implemented
```
