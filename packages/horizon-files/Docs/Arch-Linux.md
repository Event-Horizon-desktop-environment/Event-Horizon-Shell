# Arch Linux

## Dependencies

```bash
sudo pacman -S meson gcc just \
  wayland wayland-protocols \
  freetype2 fontconfig \
  cairo pango \
  libxkbcommon glib2 \
  sdbus-cpp \
  libwebp libjpeg-turbo openssl cmake \
  ffmpeg \
  poppler-glib libarchive librsvg
```

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

### With `just`

- **`just build`** — creates `build-debug/` if needed, then compiles a debug build.
- **`just build-release`** — creates `build-release/` if needed, then compiles a release build.
- **`just install`** — release build + install to `/usr`.
