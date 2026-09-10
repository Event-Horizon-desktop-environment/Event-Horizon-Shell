# Configuration

The configuration system is centered around `ShellConfig` defined in `src/configuration/shell_config.hpp`. Configuration is loaded from a TOML file and provides a runtime snapshot that all shell components read.

## Config File Location

```
~/.config/event-horizon/config.toml
$XDG_CONFIG_HOME/event-horizon/config.toml
```

An example configuration is provided in `example.toml` at the project root.

## Config Structure

### Appearance (`[appearance]`)

Controls visual appearance including:
- Overlay opacities for popups (control center, app drawer, launchpad, tooltips, etc.)
- Launchpad grid dimensions, cell gap, icon fill percentage
- View mode (grid vs. list)
- Dock and bar heights for various button sizes
- Workspace dot/bar sizes
- Background blur settings
- Matugen color scheme and mode (light/dark)
- Matugen external template toggles (GTK, kitty, neovim, Firefox, Steam, etc.)
- Font family overrides
- Label visibility and truncation
- Day/night schedule for nightlight
- Wallpaper color extraction mode

### Dock (`[dock]`)

Controls the dock panel including:
- Icon size
- Auto-hide behavior and delay
- Dock position, offset, and margin
- Pinned application IDs
- Widget layout (left, center, right sections)
- Tray, workspace, and trash visibility
- Popup margins
- Window focus on hover delay
- Display mode (pinned-only, visible-windows, etc.)

### Taskbar (`[taskbar]`)

Separate but parallel settings for the taskbar panel:
- Height and icon size
- Auto-hide
- Position and margin
- Pinned applications
- Widget layout
- Panel mode (pinned-only, visible-windows, etc.)

### Layout (`[layout]`)

Controls overall UI layout:
- Matugen integration toggles

### Desktop (`[desktop]`)

Desktop icon and widget settings:
- Icon size, grid spacing, and label settings
- Archive mounting behavior
- Per-widget configuration (clock, calendar, weather, media player, system monitor)
- Desktop entry ID and layout position for each widget

## Config Watching

The configuration file is watched for changes using `src/desktop_shell/shared/config_watch.hpp`. When the file is modified on disk, the config snapshot is reloaded and shell components are notified via callbacks.

## Matugen Integration

The config system integrates with Material You color generation:
- `matugen_palette.cpp` - Generates color palettes from wallpaper images
- `matugen_external_templates.cpp` - Generates theme files for external applications (GTK, terminals, editors, browsers, etc.)
- `chrome_matugen_tint.hpp` - Applies Matugen tints to chrome colors
- `hyprland_border_matugen.cpp` - Generates Hyprland border colors from Matugen

## Chrome Colors

The `derived_chrome_colors()` function computes a set of material colors from the config snapshot:
- `dockFillR/G/B` - Surface fill color
- `drawerDimR/G/B` - Dimmed surface color
- `accentR/G/B` - Accent / primary color
- `outlineR/G/B` - Outline / border color

These are used throughout the shell for consistent theming.
