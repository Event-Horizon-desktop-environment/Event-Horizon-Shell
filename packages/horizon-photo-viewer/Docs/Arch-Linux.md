# Arch Linux

## Dependencies

```bash
sudo pacman -S meson gcc ninja \
  wayland wayland-protocols \
  vulkan-devel \
  dbus \
  cairo libxkbcommon
```

Optional decoders (pass `-D<name>=true`):

```bash
sudo pacman -S libjpeg-turbo libwebp libheif libavif libraw libjxl exiv2 lcms2
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
