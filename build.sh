#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# ── Distro detection ──────────────────────────────────────────────────────
distro=""
if command -v apt-get &>/dev/null; then
  distro=debian
elif command -v dnf &>/dev/null; then
  distro=fedora
elif command -v pacman &>/dev/null; then
  distro=arch
else
  echo "Unsupported package manager. Install dependencies manually (see Docs/)."
  exit 1
fi

# ── Dependency install ────────────────────────────────────────────────────
case "$distro" in
  debian)
    sudo apt-get update
    sudo apt-get install -y meson g++ just \
      libwayland-dev wayland-protocols \
      libvulkan-dev \
      libfreetype-dev libfontconfig-dev \
      libcairo2-dev libpango1.0-dev \
      libxkbcommon-dev libglib2.0-dev \
      libsdbus-c++-dev libpipewire-0.3-dev \
      libpam0g-dev libaudit-dev \
      libpolkit-agent-1-dev libpolkit-gobject-1-dev \
      libcurl4-openssl-dev libssl-dev \
      libwebp-dev libpng-dev liblz4-dev zlib1g-dev \
      libdrm-dev libffmpegthumbnailer-dev \
      librsvg2-dev libjpeg-dev liblcms2-dev \
      libpoppler-glib-dev libarchive-dev \
      cmake mold libjemalloc-dev
    ;;
  fedora)
    sudo dnf install -y meson gcc-c++ just \
      wayland-devel wayland-protocols-devel \
      vulkan-loader-devel \
      freetype-devel fontconfig-devel \
      cairo-devel pango-devel \
      libxkbcommon-devel glib2-devel \
      sdbus-cpp-devel pipewire-devel \
      pam-devel polkit-devel libcurl-devel openssl-devel \
      libwebp-devel cmake libjxl libdrm-devel \
      librsvg2-devel ffmpegthumbnailer-devel \
      libjpeg-turbo-devel lcms2-devel \
      poppler-glib-devel libarchive-devel \
      mold jemalloc-devel
    ;;
  arch)
    sudo pacman -Syu --noconfirm --needed \
      base-devel meson ninja just \
      wayland wayland-protocols \
      vulkan-headers vulkan-icd-loader \
      freetype2 fontconfig \
      cairo pango \
      libxkbcommon glib2 \
      sdbus-cpp pipewire \
      pam curl openssl \
      libwebp libpng lz4 \
      libdrm ffmpegthumbnailer \
      librsvg libjpeg-turbo lcms2 \
      poppler-glib libarchive jemalloc \
      cmake mold
    ;;
esac

# ── Build & install ───────────────────────────────────────────────────────
exec sudo just install
