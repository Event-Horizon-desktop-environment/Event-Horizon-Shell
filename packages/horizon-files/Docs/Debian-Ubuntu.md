# Debian / Ubuntu

## Dependencies

```bash
sudo apt install meson g++ just \
  libwayland-dev wayland-protocols \
  libfreetype-dev libfontconfig-dev \
  libcairo2-dev libpango1.0-dev \
  libxkbcommon-dev libglib2.0-dev \
  libsdbus-c++-dev \
  libwebp-dev libjpeg-dev libssl-dev cmake \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
  libpoppler-glib-dev libarchive-dev librsvg2-dev
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
