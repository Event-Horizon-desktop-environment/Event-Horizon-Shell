# Fedora

## Dependencies

```bash
sudo dnf install meson gcc-c++ ninja-build \
  wayland-devel wayland-protocols-devel \
  vulkan-loader-devel \
  dbus-devel \
  cairo-devel libxkbcommon-devel
```

Optional decoders:

```bash
sudo dnf install libjpeg-turbo-devel libwebp-devel libheif-devel libavif-devel libraw-devel libjxl-devel exiv2-devel lcms2-devel
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
