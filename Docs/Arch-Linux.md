# Arch Linux

Official repos only — no AUR packages required.

Everything below is copy-paste. Run each block in order from a terminal.

## 1. Get the source

```bash
git clone --recursive https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

## 2. Install dependencies to compile.

```bash
sudo pacman -S --needed meson just cmake mold \
  wayland wayland-protocols \
  freetype2 fontconfig \
  cairo pango \
  libxkbcommon glib2 \
  sdbus-cpp libpipewire polkit \
  pam curl openssl \
  libwebp libpng lz4 libdrm \
  ffmpegthumbnailer jemalloc \
  librsvg poppler-glib libarchive lcms2 \
  vulkan-headers vulkan-icd-loader
```

`--needed` skips anything already installed. `gcc` is part of base-devel, which every Arch install already has.

If you don't have a compositor to run the shell inside yet, install one now
(Hyprland is in the official repos):

```bash
sudo pacman -S hyprland
```

(Niri is also available as `niri`; mangowm is not in the official repos.)

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
(Hyprland, Niri, Sway, Mango, Labwc, or Triad). To test it before wiring up
autostart, open a terminal in your session and run:

```bash
EventHorizon
```

You should see the dock + panel appear. Then add your compositor's exact
autostart line + optional color matching — see [Setup.md](Setup.md) → "Make it autostart".

## Optional image codecs

`-Djpeg=true` / `-Djxl=true` builds need two more packages, then a reconfigure
(`just install` keeps the flags set):

```bash
sudo pacman -S --needed libjpeg-turbo libjxl
sudo meson configure build-release -Djpeg=true -Djxl=true
sudo just install
```

> The `sudo` on `meson configure` matters: after a `sudo just install` the
> `build-release/` dir is owned by root, so a plain `meson configure` fails
> with `PermissionError … cmd_line.txt` and the flags silently don't apply.

## Troubleshooting

- **`ERROR: File assets/fonts/inter/docs/font-files/InterVariable.ttf does
  not exist.`** You cloned without `--recursive` (or an older clone doesn't
  have the submodule checked out). In the repo root run
  `git submodule update --init --recursive`, then re-run step 3.
- **Configure/compile fails naming a package** (e.g. `libpipewire-0.3 not
  found`): install it, then re-run step 3. The Arch package name can differ
  from the pkg-config name — `libpipewire-0.3` → package `libpipewire`,
  `polkit-agent-1` / `polkit-gobject-1` → package `polkit`.
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

- Vulkan is a **hard** build requirement (WSI is always compiled in): the
  `vulkan.pc` file and headers come from `vulkan-headers`, the runtime loader
  from `vulkan-icd-loader`. Both are listed explicitly, even though a GPU
  driver package often pulls the loader in anyway.
- Arch ships headers and `.pc` files in the runtime packages (no `-devel`
  split): `polkit` provides `polkit-agent-1.pc` + `polkit-gobject-1.pc`,
  `glib2` provides `gio-2.0`/`gobject-2.0`.
- `libpipewire` provides `libpipewire-0.3`. Abseil is not needed: the vendored
  material-color-utilities code is compiled against small standard-library
  shims (`third_party/absl-shim`).
