#!/usr/bin/env bash
set -euo pipefail

qt_root="${1:?usage: build-qwindowkit.sh <qt-root>}"
build_type="${2:-RelWithDebInfo}"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qwk_source="${repo_root}/build/qwindowkit-src"
qwk_build="${repo_root}/build/qwindowkit-build"
qwk_install="${repo_root}/build/qwindowkit-install"

rm -rf "${qwk_source}" "${qwk_build}" "${qwk_install}"
git clone --recursive --branch main https://github.com/stdware/qwindowkit.git "${qwk_source}"

cmake -S "${qwk_source}" -B "${qwk_build}" -G Ninja \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_PREFIX_PATH="${qt_root}" \
    -DCMAKE_INSTALL_PREFIX="${qwk_install}" \
    -DQWINDOWKIT_BUILD_QUICK=TRUE \
    -DQWINDOWKIT_BUILD_WIDGETS=FALSE \
    -DQWINDOWKIT_BUILD_EXAMPLES=FALSE \
    -DQWINDOWKIT_BUILD_DOCUMENTATIONS=FALSE

cmake --build "${qwk_build}" --parallel
cmake --install "${qwk_build}"

test -f "${qwk_install}/lib/cmake/QWindowKit/QWindowKitConfig.cmake"
grep -R "QWindowKit::Quick" "${qwk_install}/lib/cmake/QWindowKit"
