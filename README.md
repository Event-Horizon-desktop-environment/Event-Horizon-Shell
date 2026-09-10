# Event Horizon

Wayland shell (dock, panel, desktop, embedded settings) for wlroots-style compositors. Single binary: **`EventHorizon`**.

## Quick start

```bash
git clone https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
```

Install your distro's dependency packages (one copy-paste block each), then
build and install in a single step:

```bash
sudo just install
EventHorizon --version    # verify: prints "Event Horizon 0.9.5-beta1"
```

Distro dependency lists:

- [Arch Linux](Docs/Arch-Linux.md)
- [Fedora](Docs/Fedora.md)
- [Debian / Ubuntu](Docs/Debian-Ubuntu.md)

## Dependencies

Requires a C++23 toolchain, **Meson**, **Ninja**, and the dev packages listed for your distro:

- [Arch Linux](Docs/Arch-Linux.md)
- [Fedora](Docs/Fedora.md)
- [Debian / Ubuntu](Docs/Debian-Ubuntu.md)

Required for every build: **Wayland**, **Cairo/Pango**, **fontconfig**, **libxkbcommon**, **sdbus-c++**, **PipeWire**, **Vulkan** (headers + loader — GPU rendering is always compiled in), **PAM**, **libcurl/OpenSSL**, **libffmpegthumbnailer**, **liblz4/libwebp/libpng**.

Optional / auto-detected: **librsvg** (SVG), **libdrm**, **lcms2** (ICC color transforms), **poppler-glib** (PDF previews), **libarchive**, **libjpeg** (`-Djpeg=true`), **libjxl** (`-Djxl=true`), **jemalloc**.

If configure fails, install whatever `-dev` / `-devel` package Meson names in the error.

## Compile

```bash
meson setup build
meson compile -C build
```

Release build:

```bash
meson setup build-release --buildtype=release
meson compile -C build-release
```

See the distro page for `just` recipes.

Optional flags (see `meson configure build`):

- `-Djpeg=true` / `-Djpeg=false` — JPEG support for wallpapers
- `-Dembed_assets=true` — bundle logo/fonts in the binary (default where enabled)

## Install to `/usr`

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

Verify:

```bash
command -v EventHorizon
```

Optional symlink for shorter name:

```bash
sudo ln -sf /usr/bin/EventHorizon /usr/bin/event-horizon
```

### Staged install

```bash
DESTDIR=/tmp/stage meson install -C build
# then copy from /tmp/stage/usr/ as needed
```

## Run

From a Wayland session (e.g. Hyprland), start the shell the way your compositor config expects — often `EventHorizon` or `EventHorizon --both`.

**Extra entrypoints:** `EventHorizon --eh-settings` (standalone settings), `EventHorizon --eh-screenshot …` (screenshot to PNG).
