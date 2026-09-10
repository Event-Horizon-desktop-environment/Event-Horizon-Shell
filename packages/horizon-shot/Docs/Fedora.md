# Fedora

## Dependencies

```bash
sudo dnf install meson gcc-c++ just \
  wayland-devel \
  cairo-devel \
  libxkbcommon-devel \
  libdrm-devel
```

Optional:

```bash
sudo dnf install libpng-devel libjxl-devel librsvg2-devel
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
