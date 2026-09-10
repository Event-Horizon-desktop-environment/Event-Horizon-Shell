# Debian / Ubuntu

## Dependencies

```bash
sudo apt install meson g++ just \
  libwayland-dev \
  libcairo2-dev \
  libxkbcommon-dev \
  libdrm-dev
```

Optional:

```bash
sudo apt install libpng-dev librsvg2-dev
```

Note: `libjxl-dev` may not be available on older releases.

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
