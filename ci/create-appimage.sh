#!/usr/bin/env bash
set -euo pipefail

package_root="${1:?usage: create-appimage.sh <linux-package-root> <appimage-name-without-extension>}"
appimage_name="${2:?usage: create-appimage.sh <linux-package-root> <appimage-name-without-extension>}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
artifacts_dir="${repo_root}/build/artifacts"
appdir="${artifacts_dir}/${appimage_name}.AppDir"
appimagetool="${repo_root}/build/tools/appimagetool-x86_64.AppImage"
output="${artifacts_dir}/${appimage_name}.AppImage"

if [[ ! -x "${package_root}/run-zoingallery.sh" ]]; then
    echo "Linux package root is missing run-zoingallery.sh: ${package_root}" >&2
    exit 1
fi

rm -rf "${appdir}" "${output}"
mkdir -p "${appdir}/opt/ZoinGallery" "${repo_root}/build/tools"

cp -a "${package_root}/." "${appdir}/opt/ZoinGallery/"
cp -f "${repo_root}/resources/Logo.svg" "${appdir}/ZoinGallery.svg"
cp -f "${repo_root}/resources/Logo.svg" "${appdir}/.DirIcon"

cat >"${appdir}/AppRun" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${here}/opt/ZoinGallery/run-zoingallery.sh" "$@"
EOF
chmod +x "${appdir}/AppRun"

cat >"${appdir}/ZoinGallery.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=ZoinGallery
Comment=Image gallery and viewer
Exec=ZoinGallery
Icon=ZoinGallery
Categories=Graphics;Photography;Viewer;
Terminal=false
EOF

if [[ ! -x "${appimagetool}" ]]; then
    curl -fsSL \
        "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" \
        -o "${appimagetool}"
    chmod +x "${appimagetool}"
fi

ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "${appimagetool}" "${appdir}" "${output}"
chmod +x "${output}"
