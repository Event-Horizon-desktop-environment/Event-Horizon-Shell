# Usage

## Capture modes

```
horizon-shot                     Interactive GUI (default)
horizon-shot -s, --select        Interactive region selection
horizon-shot -f, --focused       Capture focused window
horizon-shot -a, --all           Capture all screens
horizon-shot -o, --output NAME   Capture a specific output by name
horizon-shot -w, --window [ID]   Capture a window by index, app_id, or title
```

## Frame options

```
--cursor             Include cursor in capture
--border             Show window border
--no-shadow          Disable drop shadow
--no-frame           Hide decoration frame
--inset PX           Padding around captured image (default: 32)
--corner-radius PX   Corner radius (default: 24)
```

## Output

```
-c, --copy                Copy to clipboard (instead of saving)
-O, --output-file PATH    Save to a specific path
```

Without `--copy` or `--output-file`, captures are saved to `/tmp/horizon-shot-XXXXXX.png`.

## Utility

```
--list-windows    List all open windows with index, app_id, and title
--list-outputs    List available outputs (name, resolution, position)
--hdr             Capture HDR linear data (exports as JXL or PNG16)
-h, --help       Show help and exit
```

## Keybinds

The program is intended to be launched from a compositor keybind. Example for Hyprland:

```conf
bind = Print, exec, horizon-shot --select --copy
bind = Shift+Print, exec, horizon-shot --focused --copy
```

## CLI mode examples

```sh
# Take a screenshot of the focused window with shadow and cursor
horizon-shot --focused --cursor

# Capture a specific output without decoration
horizon-shot --output DP-1 --no-frame

# Interactive selection, copy to clipboard
horizon-shot --select --copy

# Full screen capture with custom padding and save to a file
horizon-shot --inset 16 -O ~/screenshot.png

# List windows and capture by index
horizon-shot --list-windows
horizon-shot --window 0 -O ~/window.png

# Capture a window by app_id substring
horizon-shot --window code -O ~/code.png

# Capture with HDR data
horizon-shot --hdr -O ~/shot.jxl
```

## CLI limitations

These features are only available in the interactive GUI (run `horizon-shot` with no arguments):

| Missing from CLI | Available in GUI |
|---|---|
| **Window activation before capture** | Activates the selected window via `zwlr_foreign_toplevel_handle_v1_activate` |
| **Output picker** (choose which screen) | Interactive output list with "All Screens" toggle |
| **HDR/10bit badges** | Shows HDR capabilities per output in the list |
| **Preview with zoom/pan** | Scroll to zoom, drag to pan the captured image |
| **File chooser dialog** (Save / Save As) | Opens an interactive save dialog |
| **Auto-copy to clipboard** | Every capture is automatically copied |
| **Periodic window list refresh** | Event loop re-checks for new/closed windows |
| **Status bar** | Shows "Ready", "Captured", error messages, etc. |
| **App icons in window list** | Loads themed icons for each app |
| **Matugen dynamic theming** | Follows the desktop shell colour scheme |

`--window`, `--list-windows`, and `--hdr` are now available in CLI mode.

Frame options (`--border`, `--no-shadow`, `--inset`, etc.) work in both CLI and GUI modes. In GUI mode, they're applied at export time via the capture/export buttons.
