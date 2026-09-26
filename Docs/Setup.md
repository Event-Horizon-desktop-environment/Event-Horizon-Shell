# Setup

Complete, copy-paste install and configuration guide for Event Horizon.
Works with: **Hyprland**, **Niri**, **Mango (mangowm)**, Sway, Labwc, Triad.

---

## 1. Get the source

Run **exactly** this — the `--recursive` is required, it pulls the bundled fonts:

```bash
git clone --recursive https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

Already cloned it *without* `--recursive`? Fix it with:

```bash
git submodule update --init --recursive
```

> If you skip this, the build fails at the very end with
> `ERROR: File assets/fonts/inter/docs/font-files/InterVariable.ttf does not exist.`
> The submodule just wasn't checked out.

## 2. Install dependencies

Pick your distro, copy the whole block, paste it into a terminal:

| Distro | Command block |
|---|---|
| Arch | [Arch-Linux.md](Arch-Linux.md) |
| Fedora | [Fedora.md](Fedora.md) |
| Debian / PikaOS / Ubuntu | [Debian-Ubuntu.md](Debian-Ubuntu.md) |

Each page is a single copy-paste session — just follow it top to bottom.

Need a bare compilers/`just`/Meson refresher or a manual (non-just) build?
See [Build.md](Build.md).

## 3. Build and install

From the repo root (`~/Event-Horizon-Shell` — the folder you `cd`'d into in step 1):

```bash
sudo just install
```

That configures a release build to `/usr`, compiles, and installs. Takes a few minutes.
It's one command from start to finish.

## 4. Verify it's installed

```bash
EventHorizon --version
```

Should print something like `Event Horizon 0.9.6-beta2`.

## 5. Run it once, manually, before configuring autostart

Log into your compositor session. Open a terminal **inside** that Wayland session and run:

```bash
EventHorizon
```

You should see the dock + panel appear immediately. Ctrl+C in that terminal to stop it.

> If nothing appears, see [Troubleshooting.md](Troubleshooting.md) before you fiddle with autostart.

## 6. Make it autostart

Pick your compositor. Add the line(s) to your compositor config so the shell
starts every time you log in.

### Hyprland

Add to **`~/.config/hypr/hyprland.conf`**:

```conf
exec-once = EventHorizon        # the shell (dock + panel)
```

### Niri

Add to **`~/.config/niri/config.kdl`**:

```kdl
spawn-at-startup "EventHorizon";
```

### Mango (mangowm)

Add to **`~/.config/mango/config.conf`**:

```conf
exec-once=EventHorizon
```

---

## 7. Optional: match the compositor's colors to the shell

Pick a wallpaper from the shell's settings and Event Horizon generates a
matugen color palette *and* writes a compositor colors file for you.
Hook it up so window borders match your shell theme:

### Hyprland

Event Horizon writes `~/.config/hypr/eh/colors.conf`. Add this anywhere in
`~/.config/hypr/hyprland.conf`:

```conf
source = ~/.config/hypr/eh/colors.conf
```

### Niri

Event Horizon writes `~/.config/niri/eh/colors.kdl`. Add this anywhere in
`~/.config/niri/config.kdl`:

```kdl
include "eh/colors.kdl";
```

### Mango (mangowm)

Event Horizon writes `~/.config/mango/hyprmango/colors.matugen.conf`. Add this
anywhere in `~/.config/mango/config.conf`:

```conf
source-optional=~/.config/mango/hyprmango/colors.matugen.conf
```

The file is auto-reloaded by `mmsg -d reload_config` when the wallpaper changes.

## Configuration

The shell's own settings file:

```
~/.config/event-horizon/config.toml
```

An annotated example lives at `example.toml` in the repo. The in-shell settings
app is the easy way to change things: run `EventHorizon --eh-settings`.

## Extra entrypoints

- `EventHorizon` — full shell (dock + panel; this is the default)
- `EventHorizon --dock` — dock only (no panel)
- `EventHorizon --eh-settings` — standalone settings app
- `EventHorizon --eh-screenshot [file.png]` — screenshot to PNG