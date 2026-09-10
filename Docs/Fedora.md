# Fedora

All package names verified against the Fedora repositories (F43+). Nothing
here comes from a third-party repo.

Everything below is copy-paste. Run each block in order from a terminal.

## 1. Get the source

```bash
git clone https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

## 2. Install dependencies

```bash
sudo dnf install meson gcc-c++ just cmake mold \
  wayland-devel wayland-protocols-devel \
  vulkan-loader-devel vulkan-headers \
  freetype-devel fontconfig-devel \
  cairo-devel pango-devel \
  libxkbcommon-devel glib2-devel \
  sdbus-cpp-devel pipewire-devel \
  pam-devel polkit-devel \
  libcurl-devel openssl-devel \
  libwebp-devel libpng-devel lz4-devel \
  libdrm-devel librsvg2-devel ffmpegthumbnailer-devel \
  jemalloc-devel lcms2-devel \
  libarchive-devel poppler-glib-devel
```

`ffmpegthumbnailer-devel` ships in the core Fedora repos since F38 — no
RPM Fusion or third-party repo is needed.

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
sudo dnf install libjpeg-turbo-devel libjxl-devel
meson configure build-release -Djpeg=true -Djxl=true
sudo just install
```

## Troubleshooting

- **`sudo dnf install` errors "No match for argument: <pkg>`**: the package
  name doesn't exist in Fedora (or the repo list is stale). Double-check the
  spelling, then `sudo dnf --refresh install <list>`.
- **Configure/compile fails naming a package**: install it, then re-run step 3.
- **Meson too old** (error mentions `>=1.11` but the package Meson is older):
  distro `meson` can lag behind. Install the newest one without root:
  `pipx install meson`, then open a new terminal.
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

- `sdbus-cpp-devel` is the current Fedora package name (formerly
  `sdbus-c++-devel`).
- Vulkan is a **hard** build requirement (WSI is always compiled in):
  `vulkan-loader-devel` (the `.pc` file) + `vulkan-headers` above are both
  required, not optional.
- `lcms2-devel` enables ICC color-profile transforms (optional but
  recommended).
- Abseil is not needed: the vendored material-color-utilities code is compiled
  against small standard-library shims (`third_party/absl-shim`).