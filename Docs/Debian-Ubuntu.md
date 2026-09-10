# Debian / PikaOS / Ubuntu

Tested on PikaOS (Debian sid base) and Ubuntu 26.04 or newer. Older releases
(e.g. 24.04) have older package names and are **not** supported — see
Troubleshooting if you insist.

Everything below is copy-paste. Run each block in order from a terminal.

## 1. Get the source

```bash
git clone https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

## 2. Install dependencies

```bash
sudo apt install meson g++ just cmake mold \
  libwayland-dev wayland-protocols \
  libvulkan-dev \
  libfreetype-dev libfontconfig-dev \
  libcairo2-dev libpango1.0-dev \
  libxkbcommon-dev libglib2.0-dev \
  libsdbus-c++-dev libpipewire-0.3-dev \
  libpam0g-dev libaudit-dev \
  libpolkit-agent-1-dev libpolkit-gobject-1-dev \
  libcurl4-openssl-dev libssl-dev \
  libwebp-dev libpng-dev liblz4-dev zlib1g-dev \
  libdrm-dev libffmpegthumbnailer-dev ffmpegthumbnailer \
  libjemalloc-dev libjemalloc2 \
  librsvg2-dev libpoppler-glib-dev \
  libarchive-dev liblcms2-dev
```

If `apt` says "Unable to locate package \<name\>", that name is not in your
repo — it doesn't mean the command failed permanently; skip it with `apt
search <name>` and re-check, or read Troubleshooting.

## 3. Build and install (one command)

```bash
sudo just install
```

This configures `build-release/` (release build, `--prefix=/usr`), compiles,
and installs. There is nothing to configure by hand — the first run does the
`meson setup` internally. Re-run it after every `git pull`; it only rebuilds
what changed.

## 4. Verify

```bash
EventHorizon --version
```

Expected output: `Event Horizon 0.9.5-beta1`. If it says `just: command not found`,
you skipped step 2.

## 5. Run

Event Horizon is a layer-shell client — it must run inside a Wayland session
(Hyprland, Niri, Sway, Mango, Labwc, or Triad). Add it to your compositor's
startup config, e.g. Hyprland:

```ini
exec-once = EventHorizon
```

or launch it manually in a terminal from inside the session:

```bash
EventHorizon
```

## Optional image codecs

`-Djpeg=true` / `-Djxl=true` builds need two more packages, then a reconfigure
(`just install` keeps the flags set):

```bash
sudo apt install libjpeg-dev libjxl-dev
meson configure build-release -Djpeg=true -Djxl=true
sudo just install
```

## Troubleshooting

- **Older releases (Ubuntu 24.04, Debian 12)**: the header packages were
  renamed in 2025. Use `libfreetype6-dev` instead of `libfreetype-dev`,
  `libfontconfig1-dev` instead of `libfontconfig-dev`, and
  `libpango-1.0-dev` instead of `libpango1.0-dev`. Anything older than 24.04
  is not supported.
- **`jemalloc` is not a package**: it is `libjemalloc-dev` (headers) +
  `libjemalloc2` (runtime) — exactly as listed in step 2.
- **Configure/compile fails naming a package**: install it, then re-run
  step 3.
- **Meson too old** (error mentions `>=1.11` but the package Meson is older):
  distro `meson` can lag behind. Install the newest one without root:
  `pipx install meson`, then open a new terminal.
- **`sudo apt install` fails because a repo is missing, e.g. `wayland-protocols`,
  `libpipewire-0.3-dev`, `libjxl-dev`**: these come from Debian sid /
  Ubuntu 26.04, not Ubuntu 24.04 — upgrade or use PikaOS.
- **Link errors mentioning `mold`**: `mold` is missing — it is in the step-2
  list; re-run step 2.

## Manual build (no `just`)

Low-level equivalent of step 3:

```bash
meson setup build --buildtype=release --prefix=/usr
meson compile -C build
sudo meson install -C build
```

Clean rebuild (removes the whole build dir):

```bash
rm -rf build-release
```

## `just` recipes

- **`just build-release`** — creates `build-release/` if needed, then compiles a release build.
- **`just install-release`** — compiles then `sudo meson install`.
- **`sudo just install-release`** — `meson install` only (skip compile, for when root owns the build dir).
- **`just configure-dev`** / **`just build-dev`** — debug build with no embedded assets, fastest incremental rebuilds.

## Notes

- Vulkan (`libvulkan-dev` above) is required — WSI is always compiled in.
  PipeWire (`libpipewire-0.3-dev`) is also required, not optional.
- `libffmpegthumbnailer-dev` + the `ffmpegthumbnailer` binary are required —
  the video-thumbnail pieces.
- `libjemalloc-dev` + `libjemalloc2` enable the jemalloc allocator
  (auto-detected).
- `librsvg2-dev` provides SVG rendering; `libpoppler-glib-dev` PDF previews;
  `libarchive-dev` archive viewing; `liblcms2-dev` ICC color transforms.