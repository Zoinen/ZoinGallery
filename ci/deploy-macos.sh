#!/usr/bin/env bash
set -euo pipefail

qt_root="${1:?usage: deploy-macos.sh <qt-root> [app-bundle]}"
app="${2:-build/bin/RelWithDebInfo/ZoinGallery.app}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ ! -d "${app}" ]]; then
    echo "App bundle not found: ${app}" >&2
    exit 1
fi

"${qt_root}/bin/macdeployqt" "${app}" \
    -qmldir="${repo_root}/qml" \
    -qmldir="${repo_root}/ZGStyle" \
    -libpath="${qt_root}/lib" \
    -libpath="${repo_root}/build/qwindowkit-install/lib" \
    -verbose=1

mkdir -p "${app}/Contents/Frameworks"
cp -f "${repo_root}"/build/qwindowkit-install/lib/*.dylib "${app}/Contents/Frameworks/" || true

qml_root="${app}/Contents/Resources/qml"
if ! find "${qml_root}/QtQuick/Controls" -name '*qtquickcontrols2plugin*' -print -quit >/dev/null 2>&1; then
    echo "macdeployqt did not deploy QtQuick.Controls into ${qml_root}" >&2
    find "${app}/Contents/Resources" -maxdepth 4 -type f | sort >&2 || true
    exit 1
fi
