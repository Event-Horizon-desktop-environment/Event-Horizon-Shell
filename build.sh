#!/usr/bin/env bash
# Event Horizon full build + optional extras installer.
#
# 1. Detects distro (arch / fedora / debian families, incl. derivatives).
# 2. Installs shell compile deps.
# 3. Configures + compiles the shell (captures logs).
# 4. Asks whether to install to /usr. On "yes" it also clones/builds/installs
#    Horizon-File-Manager and Horizon-Photo-Viewer (plus their delta deps).
# 5. On any meson/compile failure prints a proper readout, including a
#    missing-dependency -> distro package suggestion.
#
# Usage: ./build.sh [--yes] [--no-extras] [--extras] [--prefix=/usr]
set -uo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
LOGDIR="$ROOT/.build-logs"
mkdir -p "$LOGDIR"
SHELL_LOG="$LOGDIR/shell.log"

AUTO_YES=0
WITH_EXTRAS=1
PREFIX="/usr"
FM_REPO="https://github.com/Event-Horizon-desktop-environment/Horizon-File-Manager"
PV_REPO="https://github.com/Event-Horizon-desktop-environment/Horizon-Photo-Viewer"

for arg in "$@"; do
  case "$arg" in
    --yes|-y) AUTO_YES=1 ;;
    --no-extras) WITH_EXTRAS=0 ;;
    --extras) WITH_EXTRAS=1 ;;
    --prefix=*) PREFIX="${arg#--prefix=}" ;;
    -h|--help)
      echo "Usage: ./build.sh [--yes] [--no-extras|--extras] [--prefix=/usr]"; exit 0 ;;
    *) echo "Unknown arg: $arg (see --help)"; exit 2 ;;
  esac
done

info() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33mWARN\033[0m %s\n' "$*" >&2; }
err()  { printf '\033[1;31mERR\033[0m %s\n' "$*" >&2; }

ask_yes() { # ask_yes "prompt" -> 0=yes
  local prompt="$1" ans
  if [ "$AUTO_YES" -eq 1 ]; then return 0; fi
  printf '%s [Y/n] ' "$prompt" > /dev/tty
  read -r ans < /dev/tty || ans=""
  case "$ans" in [nN]|[nN][oO]) return 1 ;; *) return 0 ;; esac
}

# ── Distro detection ──────────────────────────────────────────────────────────
DISTRO=""; PM=""
detect_distro() {
  local id="" like=""
  if [ -f /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    id="${ID:-}"; like="${ID_LIKE:-}"
  fi
  local hay="$id $like"
  case "$hay" in
    *arch*|*cachyos*|*endeavouros*|*manjaro*) DISTRO=arch ;;
    *fedora*|*rhel*|*centos*|*nobara*|*ultramarine*) DISTRO=fedora ;;
    *debian*|*ubuntu*|*pika*|*pop*|*mint*|*elementary*|*zorin*) DISTRO=debian ;;
    *)
      if command -v pacman >/dev/null 2>&1; then DISTRO=arch
      elif command -v dnf >/dev/null 2>&1; then DISTRO=fedora
      elif command -v apt-get >/dev/null 2>&1; then DISTRO=debian
      else err "Unsupported distro (ID='$id'). See Docs/Arch-Linux.md, Docs/Fedora.md, Docs/Debian-Ubuntu.md."; exit 1
      fi ;;
  esac
  case "$DISTRO" in arch) PM=pacman ;; fedora) PM=dnf ;; debian) PM=apt ;; esac
  info "Detected distro family: $DISTRO (manager: $PM, ID='$id')"
}

install_pkgs() { # install_pkgs pkg... (never upgrades the system)
  case "$PM" in
    pacman) sudo pacman -S --noconfirm --needed "$@" ;;
    dnf)    sudo dnf install -y "$@" ;;
    apt)    sudo apt-get install -y "$@" ;;
  esac
}

# ── Package lists (shell + extras deltas) ─────────────────────────────────────
shell_pkgs() {
  case "$DISTRO" in
    arch) cat <<'EOF'
base-devel meson ninja just cmake mold git pkgconf python wayland wayland-protocols vulkan-headers vulkan-icd-loader freetype2 fontconfig cairo pango libxkbcommon glib2 sdbus-cpp libpipewire polkit pam curl openssl libwebp libpng lz4 libdrm ffmpegthumbnailer jemalloc librsvg poppler-glib libarchive lcms2
EOF
      ;;
    fedora) cat <<'EOF'
meson gcc-c++ just cmake mold ninja-build pkg-config python3 git wayland-devel wayland-protocols-devel vulkan-loader-devel vulkan-headers freetype-devel fontconfig-devel cairo-devel pango-devel libxkbcommon-devel glib2-devel sdbus-cpp-devel pipewire-devel pam-devel polkit-devel libcurl-devel openssl-devel libwebp-devel libpng-devel lz4-devel libdrm-devel librsvg2-devel ffmpegthumbnailer-devel jemalloc-devel lcms2-devel libarchive-devel poppler-glib-devel
EOF
      ;;
    debian) cat <<'EOF'
meson g++ just cmake mold ninja-build pkg-config python3 git libwayland-dev wayland-protocols libvulkan-dev libfreetype-dev libfontconfig-dev libcairo2-dev libpango1.0-dev libxkbcommon-dev libglib2.0-dev libsdbus-c++-dev libpipewire-0.3-dev libpam0g-dev libaudit-dev libpolkit-agent-1-dev libpolkit-gobject-1-dev libcurl4-openssl-dev libssl-dev libwebp-dev libpng-dev liblz4-dev zlib1g-dev libdrm-dev libffmpegthumbnailer-dev ffmpegthumbnailer librsvg2-dev libjpeg-dev liblcms2-dev libpoppler-glib-dev libarchive-dev libjemalloc-dev libjemalloc2
EOF
      ;;
  esac
}

fm_extra_pkgs() { # delta on top of shell pkgs
  case "$DISTRO" in
    arch) echo "ffmpeg libjpeg-turbo" ;;
    fedora) echo "ffmpeg-devel libjpeg-turbo-devel" ;;
    debian) echo "libavcodec-dev libavformat-dev libavutil-dev libswscale-dev" ;;
  esac
}

pv_extra_pkgs() {
  case "$DISTRO" in
    arch) echo "libjpeg-turbo libheif libavif libraw libjxl exiv2" ;;
    fedora) echo "libjpeg-turbo-devel libwebp-devel libheif-devel libavif-devel libraw-devel libjxl-devel exiv2-devel dbus-devel" ;;
    debian) echo "libdbus-1-dev libheif-dev libavif-dev libraw-dev libjxl-dev exiv2-dev" ;;
  esac
}

# Map a missing pkg-config/program name from meson logs to a distro package.
suggest_pkg() { # suggest_pkg <lowercase-missing-token>
  local t="$1"
  case "$DISTRO" in
    arch)
      case "$t" in
        *wayland-client*|*wayland-scanner*) echo "wayland" ;;
        *wayland-protocols*) echo "wayland-protocols" ;;
        *vulkan*) echo "vulkan-headers + vulkan-icd-loader" ;;
        *cairo*) echo "cairo" ;; *pango*) echo "pango" ;;
        *freetype*) echo "freetype2" ;; *fontconfig*) echo "fontconfig" ;;
        *xkbcommon*) echo "libxkbcommon" ;; *sdbus*) echo "sdbus-cpp" ;;
        *pipewire*) echo "libpipewire" ;; *pam*) echo "pam" ;;
        *polkit-agent*|*polkit-gobject*|*polkit*) echo "polkit" ;;
        *curl*) echo "curl" ;; *crypto*|*ssl*|*openssl*) echo "openssl" ;;
        *webp*) echo "libwebp" ;; *libpng*|*png*) echo "libpng" ;;
        *lz4*) echo "lz4" ;; *drm*) echo "libdrm" ;;
        *ffmpegthumbnailer*) echo "ffmpegthumbnailer" ;; *ffmpeg*|*libav*) echo "ffmpeg" ;;
        *rsvg*) echo "librsvg" ;; *jpeg*|*turbojpeg*) echo "libjpeg-turbo" ;;
        *lcms2*|*lcms*) echo "lcms2" ;; *poppler*) echo "poppler-glib" ;;
        *archive*) echo "libarchive" ;; *jemalloc*) echo "jemalloc" ;;
        *mold*) echo "mold" ;; *just*) echo "just" ;; *meson*) echo "meson" ;;
        *heif*) echo "libheif" ;; *avif*) echo "libavif" ;; *libraw*|*raw*) echo "libraw" ;;
        *jxl*) echo "libjxl" ;; *exiv2*|*exif*) echo "exiv2" ;;
        *dbus*) echo "dbus" ;; *ninja*) echo "ninja" ;; *cmake*) echo "cmake" ;;
        *) echo "" ;;
      esac ;;
    fedora)
      case "$t" in
        *wayland-client*|*wayland-scanner*) echo "wayland-devel" ;;
        *wayland-protocols*) echo "wayland-protocols-devel" ;;
        *vulkan*) echo "vulkan-loader-devel + vulkan-headers" ;;
        *cairo*) echo "cairo-devel" ;; *pango*) echo "pango-devel" ;;
        *freetype*) echo "freetype-devel" ;; *fontconfig*) echo "fontconfig-devel" ;;
        *xkbcommon*) echo "libxkbcommon-devel" ;; *sdbus*) echo "sdbus-cpp-devel" ;;
        *pipewire*) echo "pipewire-devel" ;; *pam*) echo "pam-devel" ;;
        *polkit*) echo "polkit-devel" ;;
        *curl*) echo "libcurl-devel" ;; *crypto*|*ssl*|*openssl*) echo "openssl-devel" ;;
        *webp*) echo "libwebp-devel" ;; *libpng*|*png*) echo "libpng-devel" ;;
        *lz4*) echo "lz4-devel" ;; *drm*) echo "libdrm-devel" ;;
        *ffmpegthumbnailer*) echo "ffmpegthumbnailer-devel" ;; *ffmpeg*|*libav*) echo "ffmpeg-devel" ;;
        *rsvg*) echo "librsvg2-devel" ;; *jpeg*|*turbojpeg*) echo "libjpeg-turbo-devel" ;;
        *lcms2*|*lcms*) echo "lcms2-devel" ;; *poppler*) echo "poppler-glib-devel" ;;
        *archive*) echo "libarchive-devel" ;; *jemalloc*) echo "jemalloc-devel" ;;
        *mold*) echo "mold" ;; *just*) echo "just" ;; *meson*) echo "meson" ;;
        *heif*) echo "libheif-devel" ;; *avif*) echo "libavif-devel" ;; *libraw*|*raw*) echo "libraw-devel" ;;
        *jxl*) echo "libjxl-devel" ;; *exiv2*|*exif*) echo "exiv2-devel" ;;
        *dbus*) echo "dbus-devel" ;; *ninja*) echo "ninja-build" ;; *cmake*) echo "cmake" ;;
        *) echo "" ;;
      esac ;;
    debian)
      case "$t" in
        *wayland-client*|*wayland-scanner*) echo "libwayland-dev" ;;
        *wayland-protocols*) echo "wayland-protocols" ;;
        *vulkan*) echo "libvulkan-dev" ;;
        *cairo*) echo "libcairo2-dev" ;; *pango*) echo "libpango1.0-dev" ;;
        *freetype*) echo "libfreetype-dev" ;; *fontconfig*) echo "libfontconfig-dev" ;;
        *xkbcommon*) echo "libxkbcommon-dev" ;; *sdbus*) echo "libsdbus-c++-dev" ;;
        *pipewire*) echo "libpipewire-0.3-dev" ;; *pam*) echo "libpam0g-dev (+ libaudit-dev)" ;;
        *polkit-agent*) echo "libpolkit-agent-1-dev" ;; *polkit-gobject*) echo "libpolkit-gobject-1-dev" ;;
        *polkit*) echo "libpolkit-agent-1-dev + libpolkit-gobject-1-dev" ;;
        *curl*) echo "libcurl4-openssl-dev" ;; *crypto*|*ssl*|*openssl*) echo "libssl-dev" ;;
        *webp*) echo "libwebp-dev" ;; *libpng*|*png*) echo "libpng-dev" ;;
        *lz4*) echo "liblz4-dev" ;; *drm*) echo "libdrm-dev" ;;
        *ffmpegthumbnailer*) echo "libffmpegthumbnailer-dev (+ ffmpegthumbnailer)" ;;
        *ffmpeg*|*libavcodec*|*libavformat*|*libavutil*|*libswscale*) echo "libavcodec-dev + libavformat-dev + libavutil-dev + libswscale-dev" ;;
        *rsvg*) echo "librsvg2-dev" ;; *jpeg*|*turbojpeg*) echo "libjpeg-dev" ;;
        *lcms2*|*lcms*) echo "liblcms2-dev" ;; *poppler*) echo "libpoppler-glib-dev" ;;
        *archive*) echo "libarchive-dev" ;; *jemalloc*) echo "libjemalloc-dev (+ libjemalloc2)" ;;
        *mold*) echo "mold" ;; *just*) echo "just" ;; *meson*) echo "meson" ;;
        *heif*) echo "libheif-dev" ;; *avif*) echo "libavif-dev" ;; *libraw*|*raw*) echo "libraw-dev" ;;
        *jxl*) echo "libjxl-dev" ;; *exiv2*|*exif*) echo "exiv2-dev" ;;
        *dbus*) echo "libdbus-1-dev" ;; *ninja*) echo "ninja-build" ;; *cmake*) echo "cmake" ;;
        *) echo "" ;;
      esac ;;
  esac
}

# ── Failure diagnosis ─────────────────────────────────────────────────────────
diagnose_failure() { # diagnose_failure <label> <build-dir> <logfile>
  local label="$1" bdir="$2" log="$3"
  err "──── $label FAILED ────"
  echo ""
  echo "Last 40 lines of $log:"
  echo "────────────────────────────────────────"
  tail -n 40 "$log" 2>/dev/null || echo "(no log)"
  echo "────────────────────────────────────────"
  echo ""
  local mlog="$bdir/meson-logs/meson-log.txt"
  if [ -f "$mlog" ]; then
    local hits
    hits=$(grep -Ei "Dependency .* not found|Program .* not found|ERROR:" "$mlog" | head -n 20 || true)
    if [ -n "$hits" ]; then
      echo "Meson reported missing dependencies:"
      echo "$hits" | sed 's/^/  /'
      echo ""
      echo "Suggested packages for '$DISTRO':"
      while IFS= read -r line; do
        # pull a likely token: quoted 'foo' or Dependency foo
        tok=$(echo "$line" | grep -Eo "'[^']+'|\"[^\"]+\"" | head -n1 | tr -d "'\"" | tr '[:upper:]' '[:lower:]')
        [ -z "$tok" ] && tok=$(echo "$line" | tr '[:upper:]' '[:lower:]')
        sug=$(suggest_pkg "$tok")
        if [ -n "$sug" ]; then echo "  - $tok -> install: $sug"
        else echo "  - $tok -> (no mapping; search your package manager for this name)"; fi
      done <<< "$hits"
      echo ""
    fi
  fi
  if [ -f "$ROOT/assets/fonts/inter/docs/font-files/InterVariable.ttf" ]; then :;
  else
    # only relevant for the shell repo, but harmless elsewhere
    if grep -q "InterVariable.ttf" "$log" 2>/dev/null; then
      warn "You cloned without --recursive. Fix: git submodule update --init --recursive"
    fi
  fi
  echo "Full log: $log"
  [ -f "$mlog" ] && echo "Meson log: $mlog"
}

run_logged() { # run_logged <logfile> cmd... -> preserves exit code
  local log="$1"; shift
  "$@" 2>&1 | tee "$log"
  return "${PIPESTATUS[0]}"
}

# ── Shell build ───────────────────────────────────────────────────────────────
build_shell() {
  cd "$ROOT"
  info "Installing shell dependencies ($DISTRO)…"
  # shellcheck disable=SC2206
  local pkgs=( $(shell_pkgs) )
  if ! install_pkgs "${pkgs[@]}"; then
    err "Dependency install failed. Re-run with sudo access and network, then retry ./build.sh"
    exit 1
  fi
  ok "dependencies installed"

  git submodule update --init --recursive 2>&1 | tail -n 3 || true

  info "Configuring + compiling shell (log: $SHELL_LOG)…"
  : > "$SHELL_LOG"
  if command -v just >/dev/null 2>&1; then
    if ! run_logged "$SHELL_LOG" just build-release; then
      diagnose_failure "shell build (just build-release)" "$ROOT/build-release" "$SHELL_LOG"
      exit 1
    fi
  else
    warn "'just' not found; falling back to raw meson"
    if ! run_logged "$SHELL_LOG" meson setup build-release --buildtype=release --prefix="$PREFIX" -Dembed_assets=false -Dcpp_link_args=-fuse-ld=mold -Dc_link_args=-fuse-ld=mold -Dunity=subprojects; then
      diagnose_failure "shell configure" "$ROOT/build-release" "$SHELL_LOG"
      exit 1
    fi
    if ! run_logged "$SHELL_LOG" meson compile -C build-release; then
      diagnose_failure "shell compile" "$ROOT/build-release" "$SHELL_LOG"
      exit 1
    fi
  fi
  ok "shell compiled"
}

install_shell() {
  cd "$ROOT"
  info "Installing shell to $PREFIX…"
  if command -v just >/dev/null 2>&1; then
    sudo just install >>"$SHELL_LOG" 2>&1 || {
      diagnose_failure "shell install (just install)" "$ROOT/build-release" "$SHELL_LOG"; exit 1; }
  else
    sudo meson install -C build-release >>"$SHELL_LOG" 2>&1 || {
      diagnose_failure "shell install (meson install)" "$ROOT/build-release" "$SHELL_LOG"; exit 1; }
  fi
  ok "shell installed; verify: EventHorizon --version"
}

# ── Extras (file manager + photo viewer) ──────────────────────────────────────
sync_repo() { # sync_repo <url> <dest>
  local url="$1" dest="$2"
  if [ -d "$dest/.git" ]; then
    info "Updating $(basename "$dest")…"
    git -C "$dest" pull --ff-only 2>&1 | tail -n 2 || warn "git pull failed in $dest (continuing)"
  else
    info "Cloning $(basename "$dest")…"
    git clone --recursive "$url" "$dest" || { err "git clone failed: $url"; return 1; }
  fi
  git -C "$dest" submodule update --init --recursive 2>&1 | tail -n 2 || true
}

build_extra() { # build_extra <label> <dir> <extra-pkgs...>
  local label="$1" dir="$2"; shift 2
  local log="$LOGDIR/$(echo "$label" | tr '[:upper:]' '[:lower:]' | tr ' ' '-').log"
  info "Installing $label delta deps…"
  if [ "$#" -gt 0 ]; then
    install_pkgs "$@" || { err "delta dep install failed for $label"; return 1; }
  fi
  cd "$dir"
  info "Building $label (log: $log)…"
  : > "$log"
  if command -v just >/dev/null 2>&1 && grep -q "^install" justfile 2>/dev/null; then
    run_logged "$log" just build-release || {
      diagnose_failure "$label build" "$dir/build-release" "$log"; return 1; }
    sudo just install >>"$log" 2>&1 || {
      diagnose_failure "$label install" "$dir/build-release" "$log"; return 1; }
  else
    run_logged "$log" meson setup build-release --buildtype=release --prefix="$PREFIX" || {
      diagnose_failure "$label configure" "$dir/build-release" "$log"; return 1; }
    run_logged "$log" meson compile -C build-release || {
      diagnose_failure "$label compile" "$dir/build-release" "$log"; return 1; }
    sudo meson install -C build-release >>"$log" 2>&1 || {
      diagnose_failure "$label install" "$dir/build-release" "$log"; return 1; }
  fi
  ok "$label installed"
}

install_extras() {
  local parent
  parent="$(dirname "$ROOT")"
  local fm_dir="$parent/Horizon-File-Manager" pv_dir="$parent/Horizon-Photo-Viewer"
  sync_repo "$FM_REPO" "$fm_dir" || return 1
  sync_repo "$PV_REPO" "$pv_dir" || return 1
  # shellcheck disable=SC2206
  local fm_extra=( $(fm_extra_pkgs) ) pv_extra=( $(pv_extra_pkgs) )
  build_extra "Horizon-File-Manager" "$fm_dir" "${fm_extra[@]}" || return 1
  build_extra "Horizon-Photo-Viewer" "$pv_dir" "${pv_extra[@]}" || return 1
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
  detect_distro
  command -v sudo >/dev/null || { err "sudo is required"; exit 1; }
  command -v git >/dev/null || { err "git is required"; exit 1; }
  build_shell
  echo ""
  if ask_yes "Install shell to the system ($PREFIX) + fetch/build/install File Manager and Photo Viewer?"; then
    install_shell
    if [ "$WITH_EXTRAS" -eq 1 ]; then
      install_extras || { err "Extras failed (shell itself is installed). See logs in $LOGDIR."; exit 1; }
      ok "All done: shell + file manager + photo viewer installed."
    else
      ok "Shell installed (extras skipped via --no-extras)."
    fi
  else
    info "Install skipped. Binaries are in $ROOT/build-release/ (run ./build-release/EventHorizon to test)."
  fi
}

main "$@"
