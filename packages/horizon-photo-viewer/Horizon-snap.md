# Integration Plan: Horizon-shot → Horizon-photo

## Overview

Merge the full Horizon-shot screenshot tool into Horizon-photo as a single binary (`horizon-photo-viewer`) with `--screenshot` mode. All protocol XMLs, core infrastructure, and screenshot UI from horizon-shot become part of horizon-photo's build tree. The screenshot mode is a separate code path that shares Wayland, Cairo, and utility infrastructure but has its own event loop and UI rendering.

---

## Step 1 — Protocol XMLs (8 new files)

Copy from `horizon-shot/protocols/` to `horizon-photo/protocol/`:

| # | File | Purpose |
|---|------|---------|
| 1 | `wlr-screencopy-unstable-v1.xml` | Primary capture backend (wlroots) |
| 2 | `wlr-layer-shell-unstable-v1.xml` | Selection overlay (fullscreen dim + drag rect) |
| 3 | `wlr-data-control-unstable-v1.xml` | Clipboard copy (wlroots) |
| 4 | `ext-image-copy-capture-v1.xml` | Portable capture backend (ext) |
| 5 | `ext-image-capture-source-v1.xml` | Capture source selection (ext) |
| 6 | `ext-foreign-toplevel-list-v1.xml` | Window listing (ext) |
| 7 | `ext-data-control-v1.xml` | Clipboard copy (ext) |
| 8 | `wlr-foreign-toplevel-management-unstable-v1.xml` | Window listing (wlroots) |

**`protocol/meson.build`** — Add 8 entries to `protocol_xmls` list. Layer-shell needs the `sed s/namespace/name_space/g` post-processing step from horizon-shot's meson.build.

---

## Step 2 — Extend `hpv::WaylandConnection`

**`src/wayland/core/connection.hpp`** — Add members:
- `zwlr_screencopy_manager_v1* screencopy_mgr_`
- `ext_image_copy_capture_manager_v1* ext_cc_mgr_`
- `ext_output_image_capture_source_manager_v1* ext_output_src_mgr_`
- `ext_foreign_toplevel_image_capture_source_manager_v1* ext_toplevel_src_mgr_`
- `ext_data_control_manager_v1* ext_data_ctrl_mgr_`
- `zwlr_data_control_manager_v1* wlr_data_ctrl_mgr_`
- `ext_foreign_toplevel_list_v1* ext_toplevel_list_`
- `zwlr_foreign_toplevel_manager_v1* wlr_toplevel_mgr_`
- `zwlr_layer_shell_v1* layer_shell_`
- Output tracking: `struct OutputSlot`, `std::vector<std::unique_ptr<OutputSlot>> tracked_outputs_`, `bind_xdg_for_tracked_()`, `refresh_logical_outputs()`, `logical_output_bounds()`, `pick_largest_logical_output()`
- `bool has_xwayland_`
- Include new protocol headers

**`src/wayland/core/connection.cpp`** — In `registry_global`: bind all new interfaces. In `disconnect()`: destroy all new members. Add output-tracking logic (wl_output listener with output_name/geometry/mode/done/scale callbacks, OutputSlot management).

---

## Step 3 — Add Core Source Files from Horizon-shot

All namespace-migrated from `hs::` to `hpv::`:

| File in horizon-photo | Source | Changes needed |
|-----------------------|--------|----------------|
| `src/core/screencopy.hpp` | `src/core/screencopy.hpp` | Namespace `hs::core` → `hpv` |
| `src/core/screencopy.cpp` | `src/core/screencopy.cpp` | Namespace, includes, `HS_LOG` → adapt |
| `src/core/foreign_toplevels.hpp` | `src/core/foreign_toplevels.hpp` | Namespace `hs::core` → `hpv` |
| `src/core/foreign_toplevels.cpp` | `src/core/foreign_toplevels.cpp` | Namespace, includes |
| `src/core/wlr_foreign_toplevels.hpp` | `src/core/wlr_foreign_toplevels.hpp` | Namespace `hs::core` → `hpv` |
| `src/core/wlr_foreign_toplevels.cpp` | `src/core/wlr_foreign_toplevels.cpp` | Namespace, includes |
| `src/core/color_info.hpp` | `src/core/color_info.hpp` | Namespace `hs::core` → `hpv` |
| `src/core/color_info.cpp` | `src/core/color_info.cpp` | Namespace, includes |
| `src/core/memfd.hpp` | `src/core/memfd.hpp` | Already free functions, no ns |
| `src/core/memfd.cpp` | `src/core/memfd.cpp` | Already free functions |
| `src/core/logging.hpp` | `src/core/logging.hpp` | `hs::core` → `hpv`, rename macro `HS_LOG` → `HPV_LOG` |
| `src/core/logging.cpp` | `src/core/logging.cpp` | Namespace |
| `src/core/icon_cache.hpp` | `src/core/icon_cache.hpp` | Namespace `hs::icons` → `hpv::icons` |
| `src/core/icon_cache.cpp` | `src/core/icon_cache.cpp` | Namespace, includes |
| `src/core/clipboard.hpp` | `src/core/clipboard.hpp` | Namespace `hs::core` → `hpv` |
| `src/core/clipboard.cpp` | `src/core/clipboard.cpp` | Namespace, includes |
| `src/core/shell_config.hpp` | `src/core/shell_config.hpp` | Namespace `hs::config` → `hpv::config` |
| `src/core/dialog_base.hpp` | `src/core/dialog_base.hpp` | Namespace → `hpv` |
| `src/core/file_chooser_dialog.hpp` | `src/core/file_chooser_dialog.hpp` | Namespace → `hpv` |

**Logging adapter**: Horizon-shot's `HS_LOG()` macro prints file/line/function. Rename to `HPV_SCREENSHOT_LOG` to avoid conflicts with any hpv logging.

---

## Step 4 — Add Screenshot Feature Source Files

All under `src/screenshot/`, namespace `hpv::screenshot`, `hpv::capture`, etc.:

| File | Source | Notes |
|------|--------|-------|
| `src/screenshot/app.hpp` | `src/screenshot/app.hpp` | `hs::screenshot` → `hpv::screenshot`, adjust includes for new path |
| `src/screenshot/app.cpp` | `src/screenshot/app.cpp` | Namespace, includes, `HS_LOG` → adapt |
| `src/screenshot/capture/capture.hpp` | `src/screenshot/capture/capture.hpp` | `hs::screenshot` → `hpv::capture` |
| `src/screenshot/capture/capture.cpp` | `src/screenshot/capture/capture.cpp` | Namespace, includes, logging |
| `src/screenshot/capture/drm_kms.hpp` | `src/screenshot/capture/drm_kms.hpp` | `hs::screenshot` → `hpv::capture` |
| `src/screenshot/capture/drm_kms.cpp` | `src/screenshot/capture/drm_kms.cpp` | Namespace, includes, logging |
| `src/screenshot/ui/paint.hpp` | `src/screenshot/ui/paint.hpp` | `hs::screenshot` → `hpv::screenshot` |
| `src/screenshot/ui/paint.cpp` | `src/screenshot/ui/paint.cpp` | Namespace, includes, logging |
| `src/screenshot/ui/layout.hpp` | `src/screenshot/ui/layout.hpp` | `hs::screenshot` → `hpv::screenshot` |
| `src/screenshot/ui/layout.cpp` | `src/screenshot/ui/layout.cpp` | Namespace, includes, logging |
| `src/screenshot/ui/input.hpp` | `src/screenshot/ui/input.hpp` | `hs::screenshot` → `hpv::screenshot` |
| `src/screenshot/ui/input.cpp` | `src/screenshot/ui/input.cpp` | Namespace, includes, logging |
| `src/screenshot/ui/button/button.hpp` | `src/screenshot/ui/button/button.hpp` | Keep plain namespace or `hpv::screenshot` |
| `src/screenshot/ui/button/button.cpp` | `src/screenshot/ui/button/button.cpp` | Namespace, includes |

**Include path adjustments**: Wherever horizon-shot used `#include "core/shm_buffer.hpp"`, change to `#include "wayland/buffer/shm_buffer.hpp"` (for the hpv version). The horizon-shot ShmBuffer has cairo integration (`cairo_surface()`, `cairo()`). The hpv version is simpler and doesn't have Cairo methods.

**ShmBuffer approach**: Keep horizon-shot's ShmBuffer as a separate class (e.g. `hpv::screenshot::ShmBuffer`) used only by screenshot code, since it's tightly coupled to the screenshot rendering pipeline.

---

## Step 5 — Build System Updates

**`src/meson.build`** — Add:
- All core files: `core/screencopy.cpp`, `core/foreign_toplevels.cpp`, `core/wlr_foreign_toplevels.cpp`, `core/color_info.cpp`, `core/memfd.cpp`, `core/icon_cache.cpp`, `core/clipboard.cpp`, `core/logging.cpp`
- All screenshot files: `screenshot/app.cpp`, `screenshot/capture/capture.cpp`, `screenshot/capture/drm_kms.cpp`, `screenshot/ui/paint.cpp`, `screenshot/ui/layout.cpp`, `screenshot/ui/input.cpp`, `screenshot/ui/button/button.cpp`
- Conditional deps: `libpng` (for `write_png` in screencopy), `libjxl` (for JXL HDR capture)
- Include directory: add `screenshot/` to the include path

**`meson.build` (root)** — Add:
- Conditional deps for libpng, libjxl (same pattern as the existing jpeg/webp/etc. in `src/meson.build`)

---

## Step 6 — Main Integration

**`src/main.cpp`** — Add CLI parsing:
```
--screenshot        Launch screenshot GUI
--select            Interactive region selection
--focused           Capture focused window
--window [sel]      Capture a window
--all               Capture all screens (default in GUI)
--cursor            Include cursor
--border            Show window border
--no-shadow         Disable drop shadow
--no-frame          Hide decoration frame
--inset <px>        Padding
--corner-radius <px>
--copy              Copy to clipboard
--output-file <path>
--hdr               Capture HDR data
--list-outputs      List outputs and exit
--list-windows      List windows and exit
```

When `--screenshot` or any capture flag is present, bypass `hpv::App` and call into screenshot mode. Otherwise run the normal photo viewer.

---

## Step 7 — Toolbar Integration (In-Viewer Screenshot Mode)

Instead of launching a separate screenshot GUI window, capture is initiated directly from the photo viewer's existing top toolbar. This keeps the UX unified — the viewer becomes a capture client too.

### New toolbar buttons

Add two new icon buttons to the right of the existing toolbar (alongside Open/Prev/Next/Zoom/Fit/Fullscreen/Slideshow/Info):

| Button | Icon | Action |
|--------|------|--------|
| **Screen** | `screen.svg` (`assets/screen.svg`) | Opens submenu listing all connected outputs + "All screens" |
| **Window** | `window.svg` (`assets/window.svg`) | Opens submenu listing all visible toplevel windows (app_id + title) |
| **Focused** | `focused.svg` (`assets/focused.svg`) | Captures the currently focused window immediately |
| **Selection** | `selection.svg` (`assets/selection.svg`) | Interactive region selection (layer-shell overlay + drag rectangle) |
| **Copy** | `copy.svg` (`assets/copy.svg`) | Copies the current view (or last capture) to clipboard as PNG — uses `ext-data-control` / `wlr-data-control` protocol |

### Submenu behavior

Submenus are rendered as popup overlays (same pattern as the existing settings/menu popups in `ui/overlay/settings.cpp` and `ui/overlay/menu.cpp`):

**Screen submenu:**
- One entry per output: `"eDP-1 (1920×1080)"`, `"HDMI-A-1 (3840×2160)"`, etc.
- Last entry: `"All screens"` — captures every output and stitches or saves individually
- Selecting an entry triggers the capture pipeline immediately

**Window submenu:**
- Dynamically populated from `ext_foreign_toplevel_list_v1` / `wlr_foreign_toplevel_manager_v1`
- Each entry shows: `"[app_id] — title"` (truncated to fit)
- Entries include a small icon (loaded via `IconCache`) next to the text
- Selecting an entry triggers window capture immediately

Both submenus close on selection. The captured image is loaded into the viewer as if it were an opened file (appears in the current view, navigable, zoomable, savable).

### Backend wiring

- A new `ScreenshotManager` member in `hpv::App` manages the capture state
- It holds references to the capture sources (`wlr_screencopy`, `ext_image_copy_capture`, DRM/KMS)
- `refresh_outputs()` and `refresh_windows()` are called on viewer startup and when the submenus are opened (to ensure fresh lists)
- The output/window lists are stored as vectors of descriptors (name, dimensions, wl_output*/handle, icon path)

### Optional capture options popup

After selecting a target, a small inline popup (or config defaults) offers:
- Include cursor? (toggle)
- Copy to clipboard? (toggle)
- Save path (default: `~/Pictures/Screenshot_<datetime>.png`)

These can also be set in the viewer's settings panel and remembered as defaults.

### Relationship to CLI `--screenshot` mode

The toolbar buttons and the `--screenshot` CLI flag share the same capture backend. The CLI mode just bypasses the viewer entirely and runs the capture synchronously, while the toolbar mode captures and loads the result into the current viewer session.

---

## Step 8 — Desktop File

Install `data/horizon-shot.desktop` as `horizon-screenshot.desktop` alongside the existing photo viewer desktop file.

---

## Files Summary

**New files (protocols):** 8 XML
**New files (core):** ~19 files (hpp + cpp)
**New files (screenshot):** ~14 files (hpp + cpp)
**Modified files:** 6 (meson.build x3, connection.hpp/.cpp, main.cpp)
**Total:** ~41 new + 6 modified

---

## Key Risks

1. **Integrating WaylandSeat vs hpv::Seat** — The screenshot code uses horizon-shot's WaylandSeat with different callbacks. These need to coexist without conflicting.
2. **ShmBuffer conflict** — Two different ShmBuffer implementations with different APIs. Keep them separate (screenshot uses its own).
3. **Layer-shell protocol codegen** — Requires the `sed s/namespace/name_space/g` workaround in the build system.
4. **stb_image_write** — Already present in hpv's third_party, but the include path setup needs to ensure the screenshot code can find it.
5. **Logging macro names** — `HS_LOG` needs to be renamed to avoid conflicts.
