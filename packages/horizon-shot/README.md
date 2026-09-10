# Horizon Shot

Screenshot tool (region selection, window capture, screen capture) for wlroots-style compositors. Single binary: **`horizon-shot`**.

## Dependencies

Requires a C++23 toolchain, **Meson**, **Ninja**, and the dev packages listed for your distro:

- [Arch Linux](Docs/Arch-Linux.md)
- [Fedora](Docs/Fedora.md)
- [Debian / Ubuntu](Docs/Debian-Ubuntu.md)

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

- `-Dpng=true` / `-Dpng=false` — PNG export support
- `-Djxl=true` / `-Djxl=false` — JPEG XL export support
- `-Drsvg=true` / `-Drsvg=false` — SVG icon rendering via librsvg

## Install to `/usr`

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

Verify:

```bash
command -v horizon-shot
```

## Run

```sh
horizon-shot            # interactive GUI
horizon-shot --select   # region selection
horizon-shot --focused  # capture focused window
horizon-shot --all      # capture all screens
```

See [Usage](Docs/Usage.md) for all options and examples.
