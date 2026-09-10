set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default: build

configure:
    meson setup build-debug

configure-release:
    meson setup build-release --buildtype=release

build:
    @if [ ! -f build-debug/build.ninja ]; then just configure; fi
    meson compile -C build-debug

build-release:
    @if [ ! -f build-release/build.ninja ]; then just configure-release; fi
    meson compile -C build-release

install:
    #!/usr/bin/env bash
    set -euo pipefail
    cd "{{ justfile_directory() }}"
    meson setup build-release --buildtype=release --prefix=/usr
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

rebuild:
    rm -rf build-debug
    just configure
    just build

rebuild-release:
    rm -rf build-release
    just configure-release
    just build-release

run: build
    ./build-debug/horizon-shot
