set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default: build

configure:
    meson setup build-debug

configure-debug:
    meson setup build-debug

configure-release:
    meson setup build-release --buildtype=release -Dstrip=true

build:
    @if [ ! -f build-debug/build.ninja ]; then just configure; fi
    meson compile -C build-debug

build-debug:
    meson compile -C build-debug

build-release:
    meson compile -C build-release

configure-ccache:
    CC="ccache gcc" CXX="ccache g++" meson setup build-ccache --buildtype=debug

build-ccache:
    CC="ccache gcc" CXX="ccache g++" meson compile -C build-ccache

install:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{ justfile_directory() }}"
    meson setup build-release --buildtype=release --prefix=/usr -Dstrip=true
    meson compile -C build-release
    exec meson install -C build-release

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
    ./build-release/horizon-photo-viewer

run-debug:
    rm -rf build-debug
    just configure
    just build
    ./build-debug/horizon-photo-viewer

run-release: configure-release build-release
    ./build-release/horizon-photo-viewer

rebuild:
    rm -rf build-debug
    just configure
    just build

rebuild-release:
    rm -rf build-release
    just configure-release
    just build-release

configure-asan:
    command -v clang++ >/dev/null || { echo >&2 "configure-asan needs clang++"; exit 1; }
    rm -rf build-asan
    CC=clang CXX=clang++ meson setup build-asan --buildtype=debug \
    	-Db_sanitize=address,undefined \
    	-Db_lundef=false

build-asan:
    meson compile -C build-asan

run-asan: build-asan
    ASAN_OPTIONS=detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1:verbosity=1 \
    UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1 \
    ./build-asan/horizon-photo-viewer

gdb-asan: build-asan
    ASAN_OPTIONS=detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1 \
    UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1 \
    gdb -ex 'set environment ASAN_OPTIONS detect_stack_use_after_return=1:abort_on_error=1:halt_on_error=1' \
        -ex 'set environment UBSAN_OPTIONS print_stacktrace=1:abort_on_error=1' \
        -ex run --args ./build-asan/horizon-photo-viewer

gdb-shell:
    gdb -ex run --args ./build-debug/horizon-photo-viewer
