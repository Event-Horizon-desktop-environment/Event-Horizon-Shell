# Unified Shell Infrastructure

Located in `src/desktop_shell/unified/`. Compositor detection, output management, and Wayland protocol registry binding shared across all shell components.

---

## File-by-File Detail

### `compositor_kind.cpp`

**Purpose:** Detects which Wayland compositor the shell is running under, using environment variables and desktop hints. Called once at startup.

**Key functions:**
- `detect_compositor_kind()` - cached singleton detection:
  1. Checks `HYPRLAND_INSTANCE_SIGNATURE` → Hyprland
  2. Checks `NIRI_SOCKET` → Niri
  3. Checks `SWAYSOCK` → Sway
  4. Checks `TRIAD_SOCKET` → Triad
  5. Checks `LABWC_PID` → Labwc
  6. Falls back to checking `XDG_CURRENT_DESKTOP`, `XDG_SESSION_DESKTOP`, `DESKTOP_SESSION`
- `compositor_kind_cstr(CompositorKind)` - returns human-readable name
- `compositor_env_hint()` - returns combined desktop env hint string for debugging

### `compositor_kind.hpp`

**Enum:** `CompositorKind` with values `Unknown, Niri, Hyprland, Sway, Mango, Labwc, Triad`.

Free functions: `detect_compositor_kind()`, `compositor_kind_cstr()`, `compositor_env_hint()`, `is_niri()`, `is_hyprland()`, `is_sway()`, `is_mango()`, `is_labwc()`, `is_triad()`.

### `unified_layer_outputs.cpp`

**Purpose:** Output selection logic for layer surfaces. Determines which `wl_output` should host dock/taskbar layer surfaces.

**Key functions:**
- `dock_pick_layer_output(DockApp&)` - selects the target `wl_output` for the dock layer:
  1. If `EH_DOCK_OUTPUT` env var is set, match by name
  2. If `settings.outputName` is set and not "auto"/"all", match by name
  3. Otherwise pick the output with the smallest `logical_x` (leftmost, i.e. primary)
  4. Fallback: return first available output
- `dock_collect_layer_target_outputs(DockApp&)` - determines which outputs should get dock surfaces:
  1. If output name is "all" (or `output_assign_is_all_displays()`): return all outputs
  2. If specific output: return only that output
  3. Default: return only the primary output (leftmost)

### `unified_wayland_registry.cpp` / `unified_wayland_registry.hpp`

**Purpose:** Wayland global registry handler. Manages output listing, xdg-output binding, and output geometry/position tracking.

**Anonymous helpers:**
- `dock_ensure_output_slot(DockApp&, wl_output*)` - finds or creates a `DockOutputSlot` for a given wl_output
- `dock_xdg_logical_position()` - xdg-output listener: stores logical position
- `dock_xdg_logical_size()` - xdg-output listener: stores logical size, marks output `ready`
- `dock_bind_xdg_for_slot()` - creates `zxdg_output_v1` for a slot and attaches listener
- `dock_bind_xdg_all_slots()` - batch xdg-output binding for all slots
- `dock_output_geometry()` / `dock_output_mode()` - wl_output geometry and mode listeners
- `dock_registry_handle_global()` - primary registry handler:
  - `wl_output`: creates slot, binds xdg-output, may trigger initial dock creation
  - `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_seat`: stores global
  - `zwlr_layer_shell_v1`: stores, may trigger initial dock creation
  - `zxdg_output_manager_v1`: stored for later xdg-output binding
  - `zwp_virtual_keyboard_manager_v1`, `zwlr_data_control_manager_v1`: stored
  - `zwp_idle_inhibit_manager_v1`, `ext_idle_notifier_v1`: stored
  - `xdg_activation_v1`: stored
  - `ext_session_lock_manager_v1`: stored
  - `zwlr_foreign_toplevel_manager_v1`: stored, triggers window tracking setup
  - `zwlr_gamma_control_manager_v1`: stored for nightlight
  - `zwlr_screencopy_manager_v1`: stored for screenshots
  - `wp_viewporter`, `wp_fractional_scale_manager_v1`, `wp_tearing_control_manager_v1`: stored
  - `zwp_linux_dmabuf_v1`: stored for Vulkan path
  - `ext_background_effect_manager_v1`: stored for blur
- `dock_registry_handle_global_remove()` - handles output removal (removes slot, triggers dock rebuild)
