# Debian / Ubuntu

## Dependencies

```bash
sudo apt install meson g++ ninja-build \
  libwayland-dev wayland-protocols \
  libvulkan-dev \
  libdbus-1-dev \
  libcairo2-dev libxkbcommon-dev
```

Optional decoders:

```bash
sudo apt install libjpeg-dev libwebp-dev libheif-dev libavif-dev libraw-dev libjxl-dev exiv2-dev liblcms2-dev
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

- **`just build`** — debug build (auto-configures if needed)
- **`just build-release`** — release build
- **`just install`** — configure, compile, install to `/usr`
- **`just install-release`** — compile then `sudo meson install`
- **`sudo just install-release`** — install only (skip compile)
- **`just run`** — release build + launch
