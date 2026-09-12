# Event Horizon

Wayland shell (dock, panel, desktop, embedded settings) for wlroots-style compositors.
Single binary: **`EventHorizon`**.

Works with **Hyprland**, **Niri**, **Mango (mangowm)**, Sway, Labwc, and Triad.

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