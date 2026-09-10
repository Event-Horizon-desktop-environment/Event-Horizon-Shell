# Build System

Event Horizon uses the Meson build system with Ninja as the backend.

## Prerequisites

- C++23 compiler (GCC 14+ or Clang 18+)
- Meson (>= 1.11) and Ninja — distro packages can lag; if your Meson is older,
  install a newer one without root-first: `pipx install meson` (re-login after)
- python3 (used by Meson and the build-time material-color generator)
- A Wayland session to run the shell (Hyprland, Niri, Sway, Mango, Labwc, or Triad)

## Quick Start

The copy-paste path — exact dependency commands for your distro live in
[Arch-Linux](Arch-Linux.md), [Fedora](Fedora.md), and [Debian-Ubuntu](Debian-Ubuntu.md):

```sh
git clone https://github.com/Event-Horizon-desktop-environment/Event-Horizon-Shell.git
cd Event-Horizon-Shell
# <install your distro's dependency packages — see the distro guides>
sudo just install
EventHorizon --version    # verify: prints "Event Horizon 0.9.5-beta1"
```

`sudo just install` configures `build-release/` (`--buildtype=release`,
`--prefix=/usr`), compiles, and installs in one command. Everything in the
`## Build Commands` section below is the manual, low-level equivalent.

## Dependencies

### Required (hard)

Resolved via pkg-config:

- **Wayland** — `wayland-client`, `wayland-protocols` (falls back to the bundled XML in `protocols/`), and the `wayland-scanner` tool
- **Cairo / Pango** — `cairo`, `pango`, `pangocairo`, `pangoft2`
- **Fonts** — `freetype2`, `fontconfig`
- **Input** — `libxkbcommon`
- **D-Bus** — `sdbus-c++` (systemd or standalone flavor)
- **Audio** — `libpipewire-0.3` (PipeWire session/stream management is compiled in unconditionally)
- **Auth** — `pam`
- **Network** — `libcurl`, `libcrypto` (OpenSSL)
- **Thumbnails** — `libffmpegthumbnailer` (required; the `subprojects/` entry is only a fallback)
- **GPU** — `vulkan` (Vulkan WSI is always enabled; needs Vulkan headers + loader dev packages)
- **Data** — `liblz4`, `libwebp`, `libpng` (each auto-falls back to a bundled subproject, so effectively required)
- **Misc** — `libdl`

### Options / auto-detected

- `polkit_agent` (default on) — `polkit-agent-1`, `polkit-gobject-1`, `gio-2.0`, `glib-2.0`, `gobject-2.0`. Disable with `-Dpolkit_agent=false` if you want to skip GLib + polkit.
- `librsvg-2.0` — SVG rendering (falls back to bundled nanosvg when absent)
- `libdrm` — monitor identification / EDID info
- `lcms2` — ICC color-profile transforms for wallpaper palette accuracy (`lcms2::lcms2` module name also tried)
- `poppler-glib` — PDF previews
- `libarchive` — archive viewing
- `libjpeg` / `libjpeg-turbo` — album art and thumbnails (`-Djpeg=true`)
- `libjxl` — JPEG XL decode support (`-Djxl=true`)
- `jemalloc` (`-Djemalloc=auto` feature) — memory allocator to reduce fragmentation on glibc

> **Note:** Do not `sudo just install` before satisfying the required set — Meson will fail fast naming the exact `-dev`/`-devel` package missing.

## Build Commands

```sh
# Development build
meson setup build-dev
ninja -C build-dev

# Release build
meson setup build-release --buildtype=release
ninja -C build-release
```

## Build Options

Defined in `meson_options.txt` (plus the builtin Meson options, e.g. `--buildtype`):

| Option | Default | Description |
|---|---|---|
| `native_march` | `true` | Release only: `-march=native -mtune=native` (faster on *this* CPU; binaries not portable). Use `-Dnative_march=false` for generic packages. |
| `release_omit_frame_pointer` | `true` | Release only: `-fomit-frame-pointer` |
| `jpeg` | `false` | Link libjpeg(-turbo) for album art / thumbnails |
| `jxl` | `false` | Enable JPEG XL decode support (libjxl) |
| `embed_assets` | `false` | Compile logo + Material Symbols font into the binary. Default false: assets load from disk (saves ~15 MB resident memory per process). |
| `polkit_agent` | `true` | Embed a PolicyKit-1 session auth agent with a layer-shell password dialog |
| `jemalloc` | `auto` | Feature; use jemalloc on glibc builds to reduce fragmentation |
| `disable_mangowm_log` | `false` | Strip all MANGOWM diagnostic logging at compile time |

The `justfile` bundles these into `configure-dev` (no assets, `-Djpeg=false`, `-Dcpp_args=-g1`), `configure-release` (`-Dembed_assets=false`, unity build, mold), and `configure` (debug).

## Vendored dependencies

Third-party code under `third_party/` (compiled in-place or included as headers):

- `tomlplusplus/` — TOML parser (vendored header library, `v3.4.0`)
- `material-color-utilities/` — Material You color engine
- `absl-shim/` — minimal Abseil substitutes so material-color-utilities builds without the real library
- `wuffs/` — image decoding library
- `zune-jpeg/` — JPEG decoder
- `nanosvg/` — SVG parser (used when librsvg is absent)
- `stb/` — single-file public-domain image/utility libraries
- `nlohmann/` — JSON for Modern C++ (single header)
- `adw-gtk3/` + `adw-gtk3-dark/` — bundled GTK themes (install via `tools/download-adw-gtk3.sh`)

Meson subprojects (`subprojects/`):

- `ffmpegthumbnailer/` — symlink fallback; the system `libffmpegthumbnailer` is required if it is not populated

> **Note:** Older docs listed `cava/`, `unifont/`, and a vendored `wayland-protocols/` here — those no longer exist.

## Binary Targets

`meson.build` produces a shared core library plus the panel/app binaries:

| Target | Purpose |
|---|---|
| `libeh_shell_core.so` | Shared core library (all shell code + embedded assets) |
| `EventHorizon` | Main shell binary |
| `eh-ipc` | CLI client for the shell IPC socket |
| `eh-startup-probe` | Startup timing probe |
| `horizon-live-wallpaper` | Standalone live wallpaper |
| `horizon-disks` | Disk utility app |
| `horizonClipboardDaemon` | Clipboard helper daemon |
| `horizon-settings` | Settings app |
| `horizon-wallpaper` | Wallpaper setter |
| `horizon-notifications` | Notification service host |
| `horizon-desktop` | Standalone desktop shell |
| `horizon-dock` | Standalone dock panel |
| `horizon-taskbar` | Standalone taskbar panel |

Plus the test suite: `test_horizon_colors`, `test_contract_ipc`, `test_vram_boost`, `test_vram_boost_live`, `test_icon_cache`, `test_damage_region`.

## Source Layout

`src/` sources (plus generated protocol code from XML in `protocols/`) compile into `libeh_shell_core.so`; every binary links it so the supervisor and split-out child processes share one copy in memory. See [ProjectMap](ProjectMap.md) for the full file-by-file map.