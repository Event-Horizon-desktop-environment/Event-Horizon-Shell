# Fedora

## Dependencies

```bash
sudo dnf install meson g++ just \
  wayland-devel wayland-protocols-devel \
  freetype-devel fontconfig-devel \
  cairo-devel pango-devel \
  libxkbcommon-devel glib2-devel \
  sdbus-cpp-devel \
  libwebp-devel libjpeg-turbo-devel \
  openssl-devel cmake \
  ffmpeg-devel \
  poppler-glib-devel libarchive-devel librsvg2-devel
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
