# Horizon File Manager

Wayland-native file manager (sidebar, tabs, grid/list, thumbnails, search, trash, file ops) for wlroots-style compositors. Single binary: **`horizon-files`**.

## Dependencies

Requires a C++23 toolchain, **Meson**, **Ninja**, and the dev packages listed for your distro:

- [Arch Linux](Docs/Arch-Linux.md)
- [Fedora](Docs/Fedora.md)
- [Debian / Ubuntu](Docs/Debian-Ubuntu.md)

Optional: **poppler-glib** (PDF thumbnails), **libarchive** (EPUB/archive previews), **librsvg** (SVG thumbnails). Video thumbnails build **ffmpegthumbnailer** from source (needs libav\* dev packages).

Third-party code is vendored as git submodules (`third_party/`). After cloning:

```bash
git submodule update --init --recursive
```

If configure fails, install whatever `-dev` / `-devel` package Meson names in the error.

## Compile

```bash
git submodule update --init --recursive
meson setup build
meson compile -C build
```

Release build:

```bash
meson setup build-release --buildtype=release -Dstrip=true
meson compile -C build-release
```

See the distro page for `just` recipes.

## Install to `/usr`

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

## Run

From a Wayland session (e.g. Hyprland):

```bash
horizon-files
```
