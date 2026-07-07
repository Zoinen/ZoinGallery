#!/usr/bin/env bash
set -euo pipefail

distro="${1:?usage: install-linux-deps.sh <ubuntu|debian|fedora|arch>}"

case "${distro}" in
    ubuntu|debian)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            build-essential \
            ca-certificates \
            file \
            git \
            libdbus-1-3 \
            libegl1 \
            libegl-dev \
            libfontconfig1 \
            libfreetype6 \
            libgl1 \
            libgl-dev \
            libglx-mesa0 \
            libglx-dev \
            libx11-xcb1 \
            libxkbcommon-x11-0 \
            libxcb-cursor0 \
            libxcb-icccm4 \
            libxcb-image0 \
            libxcb-keysyms1 \
            libxcb-randr0 \
            libxcb-render-util0 \
            libxcb-shape0 \
            libxcb-xfixes0 \
            libxcb-xinerama0 \
            libxcb-xinput0 \
            libxcb1 \
            ninja-build \
            patchelf \
            pkg-config \
            python3 \
            python3-pip \
            python3-venv \
            xvfb \
            zstd
        ;;
    fedora)
        dnf install -y \
            ca-certificates \
            cmake \
            dbus-libs \
            file \
            fontconfig \
            freetype \
            gcc \
            gcc-c++ \
            git \
            libX11-xcb \
            libxcb \
            libxkbcommon-x11 \
            make \
            mesa-libEGL \
            mesa-libEGL-devel \
            mesa-libGL \
            mesa-libGL-devel \
            ninja-build \
            patchelf \
            pkgconf-pkg-config \
            python3 \
            python3-pip \
            xcb-util-cursor \
            xcb-util-image \
            xcb-util-keysyms \
            xcb-util-renderutil \
            xcb-util-wm \
            xorg-x11-server-Xvfb \
            zstd
        ;;
    arch)
        pacman -Syu --noconfirm --needed \
            base-devel \
            ca-certificates \
            cmake \
            dbus \
            file \
            fontconfig \
            freetype2 \
            git \
            libx11 \
            libxcb \
            libxkbcommon-x11 \
            mesa \
            ninja \
            patchelf \
            pkgconf \
            python \
            python-pip \
            xcb-util-cursor \
            xcb-util-image \
            xcb-util-keysyms \
            xcb-util-renderutil \
            xcb-util-wm \
            xorg-server-xvfb \
            zstd
        ;;
    *)
        echo "Unsupported Linux distro '${distro}'" >&2
        exit 2
        ;;
esac
