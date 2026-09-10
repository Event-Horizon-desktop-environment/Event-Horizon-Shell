# Arch Linux

## Dependencies

```bash
sudo pacman -S meson gcc just \
  wayland \
  cairo \
  libxkbcommon \
  libdrm
```

Optional:

```bash
sudo pacman -S libpng libjxl librsvg
```

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

### With `just`

- **`just build-release`** — creates `build-release/` if needed, then compiles a release build.
- **`sudo just install`** — configures a release build with `--prefix=/usr`, compiles, and installs in one step.
- **`just install-release`** — compiles then `sudo meson install`.
- **`sudo just install-release`** — `meson install` only (skip compile).
