set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# Vulkan WSI is always enabled in the binary.
# On GCC, the tomlplusplus dependency logs many "Compiler … supports … NO (cached)" lines — those are optional
# MSVC-only flag probes, not errors.
# Mold linker: `sudo dnf install mold` for 5-10× faster linking + ~2× less RAM.
default: build

mold_link_args := "-Dcpp_link_args=-fuse-ld=mold -Dc_link_args=-fuse-ld=mold"
unity_args := "-Dunity=subprojects"

configure:
    meson setup build-debug {{ mold_link_args }}

configure-debug:
    meson setup build-debug {{ mold_link_args }}

configure-release:
    meson setup build-release --buildtype=release -Dembed_assets=false {{ mold_link_args }} {{ unity_args }}

# Dev build: no embedded assets, no JPEG, lighter debug info. Fastest incremental rebuilds.
configure-dev:
    meson setup build-dev --buildtype=debug -Dembed_assets=false -Djpeg=false -Dcpp_args=-g1 {{ mold_link_args }}

build-dev:
    meson compile -C build-dev

build:
    @if [ ! -f build-debug/build.ninja ]; then just configure; fi
    meson compile -C build-debug

build-debug:
    meson compile -C build-debug

build-release:
    meson compile -C build-release

# Run the unit test suite (meson test). Pure logic tests — no compositor required.
test:
    @if [ ! -f build-debug/build.ninja ]; then just configure; fi
    meson test -C build-debug --print-errorlogs

# Ccache build (local: `just configure-ccache && just build-ccache`).
configure-ccache:
    CC="ccache gcc" CXX="ccache g++" meson setup build-ccache --buildtype=debug -Dembed_assets=false {{ mold_link_args }}

build-ccache:
    CC="ccache gcc" CXX="ccache g++" meson compile -C build-ccache

# Full install to /usr: configure, build, and install in one step.
# Run as: sudo just install
install:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{ justfile_directory() }}"
    if [ -f build-release/build.ninja ]; then
      meson configure build-release --buildtype=release --prefix=/usr -Dembed_assets=false >/dev/null
    else
      meson setup build-release --buildtype=release --prefix=/usr -Dembed_assets=false {{ mold_link_args }} {{ unity_args }}
    fi
    meson compile -C build-release
    exec meson install -C build-release

# As normal user: `just install-release` → `build-release`, then `sudo meson install`.
# After that (or if you already built): `sudo just install-release` installs only — avoids root owning `build-release/`.
install-release:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{ justfile_directory() }}"
    if [ "$(id -u)" -eq 0 ]; then
    	test -f build-release/build.ninja || { echo >&2 "error: missing build-release/ — run: just build-release (as your user)"; exit 1; }
    	exec meson install -C build-release
    fi
    just build-release
    exec sudo meson install -C build-release

run:
    rm -rf build-release
    just configure-release
    just build-release
    ./build-release/EventHorizon

# Debug builds now enable ALL profiling/diagnostics automatically via `debug_profile.hpp`
# (`#ifndef NDEBUG`). Set any EH_*=0 to disable a specific trace. Release builds still opt-in.
run-debug:
    rm -rf build-debug
    just configure
    just build
    ./build-debug/EventHorizon

run-release: configure-release build-release
    ./build-release/EventHorizon

rebuild:
    rm -rf build-debug
    just configure
    just build

rebuild-release:
    rm -rf build-release
    just configure-release
    just build-release

# ── Debugging ───────────────────────────────────────────────────────────────────
# ASan+UBSan: use-after-free, heap corruption, many UBs (~2x slower).
# Fedora GCC 16 + bfd ld often fails Meson's sanitizer link probe; Clang works.
configure-asan:
    command -v clang++ >/dev/null || { echo >&2 "configure-asan needs clang++ (dnf install clang)"; exit 1; }
    rm -rf build-asan
    CC=clang CXX=clang++ meson setup build-asan --buildtype=debug \
    	-Db_sanitize=address,undefined \
    	-Db_lundef=false

build-asan:
    meson compile -C build-asan

# Run shell with sanitizer abort + traces. Prefer from a TTY or nested compositor so logs stay visible.
run-asan: build-asan
    ASAN_OPTIONS=detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1:verbosity=1 \
    UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1 \
    ./build-asan/EventHorizon

# Same ASan binary under GDB (break on __asan_report_* automatically when ASan fires).
gdb-asan: build-asan
    ASAN_OPTIONS=detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1 \
    gdb -ex 'set environment ASAN_OPTIONS detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1' \
        -ex 'set environment UBSAN_OPTIONS print_stacktrace=1:abort_on_error=1' \
        -ex run --args ./build-asan/EventHorizon

# Debug symbols build (default build-debug is usually enough); run under GDB from another session.
gdb-shell:
    gdb -ex run --args ./build-debug/EventHorizon

# Dock + settings only (lighter than shell-debug).
dock-verbose:
    EH_DOCK_DEBUG=1 EH_SETTINGS_DEBUG=1 ./build-debug/EventHorizon

# Debug builds auto-enable all profiling. Same as plain `./build-debug/EventHorizon`.
# Set EH_*=0 to suppress individual traces. Per-iter loop: EH_MAIN_LOOP_TRACE=2. WAYLAND_DEBUG=1 for wire dump.
shell-debug:
    ./build-debug/EventHorizon
