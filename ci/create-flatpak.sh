#!/usr/bin/env bash
set -euo pipefail

package_root="${1:?usage: create-flatpak.sh <linux-package-root> <bundle-name-without-extension>}"
bundle_name="${2:?usage: create-flatpak.sh <linux-package-root> <bundle-name-without-extension>}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_root="$(realpath "${package_root}")"
app_id="io.github.Zoinen.ZoinGallery"
runtime_version="24.08"
work_dir="${repo_root}/build/flatpak"
manifest="${work_dir}/io.github.Zoinen.ZoinGallery.yml"
repo_dir="${work_dir}/repo"
build_dir="${work_dir}/build"
artifacts_dir="${repo_root}/build/artifacts"
bundle="${artifacts_dir}/${bundle_name}.flatpak"

if [[ ! -x "${package_root}/run-zoingallery.sh" ]]; then
    echo "Linux package root is missing run-zoingallery.sh: ${package_root}" >&2
    exit 1
fi

rm -rf "${work_dir}" "${bundle}"
mkdir -p "${work_dir}" "${artifacts_dir}"

rsvg-convert -w 64 -h 64 "${repo_root}/resources/Logo.svg" -o "${work_dir}/io.github.Zoinen.ZoinGallery-64.png"
rsvg-convert -w 128 -h 128 "${repo_root}/resources/Logo.svg" -o "${work_dir}/io.github.Zoinen.ZoinGallery-128.png"
rsvg-convert -w 256 -h 256 "${repo_root}/resources/Logo.svg" -o "${work_dir}/io.github.Zoinen.ZoinGallery-256.png"

cat >"${work_dir}/ZoinGallery.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ZoinGallery
Comment=Image gallery and viewer
Exec=ZoinGallery
Icon=${app_id}
Categories=Graphics;Photography;Viewer;
Terminal=false
EOF

cat >"${manifest}" <<EOF
app-id: ${app_id}
runtime: org.freedesktop.Platform
runtime-version: '${runtime_version}'
sdk: org.freedesktop.Sdk
command: ZoinGallery
finish-args:
  - --share=ipc
  - --socket=x11
  - --socket=wayland
  - --device=dri
  - --filesystem=home:ro
modules:
  - name: zoingallery
    buildsystem: simple
    build-commands:
      - mkdir -p /app/bin /app/opt/ZoinGallery /app/share/applications /app/share/icons/hicolor/64x64/apps /app/share/icons/hicolor/128x128/apps /app/share/icons/hicolor/256x256/apps
      - cp -a package/. /app/opt/ZoinGallery/
      - install -Dm644 ZoinGallery.desktop /app/share/applications/${app_id}.desktop
      - install -Dm644 ${app_id}-64.png /app/share/icons/hicolor/64x64/apps/${app_id}.png
      - install -Dm644 ${app_id}-128.png /app/share/icons/hicolor/128x128/apps/${app_id}.png
      - install -Dm644 ${app_id}-256.png /app/share/icons/hicolor/256x256/apps/${app_id}.png
      - |
        cat > /app/bin/ZoinGallery <<'WRAPPER'
        #!/usr/bin/env bash
        set -euo pipefail
        exec /app/opt/ZoinGallery/run-zoingallery.sh "\$@"
        WRAPPER
      - chmod +x /app/bin/ZoinGallery
    sources:
      - type: dir
        path: ${package_root}
        dest: package
      - type: file
        path: ${work_dir}/ZoinGallery.desktop
      - type: file
        path: ${work_dir}/${app_id}-64.png
      - type: file
        path: ${work_dir}/${app_id}-128.png
      - type: file
        path: ${work_dir}/${app_id}-256.png
EOF

flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak --user install -y flathub "org.freedesktop.Platform//${runtime_version}" "org.freedesktop.Sdk//${runtime_version}"
flatpak-builder --user --force-clean --repo="${repo_dir}" "${build_dir}" "${manifest}"
flatpak build-bundle "${repo_dir}" "${bundle}" "${app_id}"
