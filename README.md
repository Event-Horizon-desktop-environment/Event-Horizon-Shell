# Event Horizon

Event Horizon — Wayland-native desktop shell in C++23. Dock, taskbar, desktop, widgets, and settings as layer-shell clients for your compositor.

Event Horizon is a complete desktop shell that runs alongside Hyprland, Niri, Mango, Sway, Labwc, and Triad. Panels, desktop icons/widgets, control center, notifications, wallpaper, lock screen, OSD, and embedded settings — rendered with Cairo/Vulkan over `wlr-layer-shell`, configured from TOML under `~/.config/event-horizon/`.
Main entry: **`EventHorizon`**, with focused `horizon-*` helpers sharing one core.

Latest release: **[0.9.5 (Beta 1)](https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell/releases/tag/0.9.5)**.

## Quick start

```bash
git clone --recursive https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

- Install your distro's deps and build: **[Docs/Setup.md](Docs/Setup.md)** — step-by-step, copy-paste, includes exact autostart configs for **Hyprland / Niri / Mango**.
- Having trouble? **[Docs/Troubleshooting.md](Docs/Troubleshooting.md)**
- Already running it and want the long-form docs? See the full index below.

## Docs

| Doc | What it covers |
|---|---|
| [Setup](Docs/Setup.md) | Full install guide + per-compositor autostart (Hyprland, Niri, Mango) + optional matugen color matching |
| [Arch Linux](Docs/Arch-Linux.md) | Arch/PikaOS dependency install block |
| [Fedora](Docs/Fedora.md) | Fedora dependency install block |
| [Debian / Ubuntu](Docs/Debian-Ubuntu.md) | Debian / PikaOS / Ubuntu dependency install block |
| [Build](Docs/Build.md) | Build system details, manual (non-just) builds, Meson flags |
| [Compositors](Docs/Compositors.md) | Which compositors are supported, how they're detected |
| [Configuration](Docs/Configuration.md) | The `config.toml` layout |
| [Troubleshooting](Docs/Troubleshooting.md) | Common install/launch failures and fixes |

## Entrypoints

- `EventHorizon` — full shell (dock + panel; default)
- `EventHorizon --dock` — dock only
- `EventHorizon --eh-settings` — standalone settings app
- `EventHorizon --eh-screenshot [file.png]` — screenshot to PNG

## Bonus: Event Horizon manages your Hyprland config

On Hyprland the shell ships its own full **Hyprland settings editor** (the
Hyprland tab in settings): window rules, binds, monitor config, animations,
blur, gaps, and more are read, edited, and written back to
`~/.config/hypr/hyprland(.conf|lua)` directly — no hand-editing needed.