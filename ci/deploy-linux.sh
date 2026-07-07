#!/usr/bin/env bash
set -euo pipefail

qt_root="${1:?usage: deploy-linux.sh <qt-root> <package-name>}"
package_name="${2:?usage: deploy-linux.sh <qt-root> <package-name>}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_root="${repo_root}/build/artifacts/${package_name}"

executable="$(find "${repo_root}/build/bin" -type f -name ZoinGallery -perm -111 | head -n 1)"
if [[ -z "${executable}" ]]; then
    echo "Could not find ZoinGallery executable under build/bin" >&2
    exit 1
fi

rm -rf "${package_root}" "${package_root}.tar.zst"
mkdir -p \
    "${package_root}/bin" \
    "${package_root}/lib" \
    "${package_root}/plugins" \
    "${package_root}/qml" \
    "${package_root}/resources"

cp -f "${executable}" "${package_root}/bin/ZoinGallery"

copy_if_exists() {
    local source="$1"
    local target_dir="$2"
    if [[ -e "${source}" ]]; then
        cp -a "${source}" "${target_dir}/"
    fi
}

copy_libs_from_dir() {
    local source_dir="$1"
    if [[ -d "${source_dir}" ]]; then
        find "${source_dir}" -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' \) -exec cp -a {} "${package_root}/lib/" \;
    fi
}

copy_libs_from_dir "${repo_root}/build/qwindowkit-install/lib"
copy_libs_from_dir "${repo_root}/build/qwindowkit-install/lib64"

copy_system_runtime_libs() {
    local search_root
    for search_root in /lib /lib64 /usr/lib /usr/lib64; do
        [[ -d "${search_root}" ]] || continue
        find "${search_root}" -maxdepth 3 \( -type f -o -type l \) \( \
            -name 'libX11*.so*' -o \
            -name 'libXau*.so*' -o \
            -name 'libXcursor*.so*' -o \
            -name 'libXdmcp*.so*' -o \
            -name 'libXext*.so*' -o \
            -name 'libXfixes*.so*' -o \
            -name 'libXi*.so*' -o \
            -name 'libXrandr*.so*' -o \
            -name 'libXrender*.so*' -o \
            -name 'libXtst*.so*' -o \
            -name 'libxcb*.so*' -o \
            -name 'libxkbcommon*.so*' \
        \) -exec cp -Lf {} "${package_root}/lib/" \; 2>/dev/null || true
    done
}

copy_system_runtime_libs

copy_if_exists "${qt_root}/plugins/platforms" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/imageformats" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/iconengines" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/tls" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/wayland-decoration-client" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/wayland-graphics-integration-client" "${package_root}/plugins"
copy_if_exists "${qt_root}/plugins/xcbglintegrations" "${package_root}/plugins"

for module in Qt QtQml QtQuick; do
    copy_if_exists "${qt_root}/qml/${module}" "${package_root}/qml"
done

cat >"${package_root}/bin/qt.conf" <<'EOF'
[Paths]
Prefix=..
Plugins=plugins
Qml2Imports=qml
Libraries=lib
EOF

cat >"${package_root}/run-zoingallery.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${app_dir}/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${app_dir}/plugins"
export QML2_IMPORT_PATH="${app_dir}/qml"
export QT_QPA_PLATFORM_PLUGIN_PATH="${app_dir}/plugins/platforms"

exec "${app_dir}/bin/ZoinGallery" "$@"
EOF
chmod +x "${package_root}/run-zoingallery.sh"

cat >"${package_root}/README.txt" <<EOF
ZoinGallery CI build ${package_name}

Run with:
  ./run-zoingallery.sh

This is an unsigned CI validation build. It bundles Qt/QML/QWindowKit
dependencies, but it is still built for the target Linux distribution shown
in the artifact name.
EOF

collect_deps() {
    local file="$1"
    LD_LIBRARY_PATH="${package_root}/lib:${qt_root}/lib:${LD_LIBRARY_PATH:-}" ldd "${file}" 2>/dev/null | awk '
        /=>/ && $3 ~ /^\// { print $3 }
        /^[[:space:]]*\// { print $1 }
    '
}

should_bundle_system_dep() {
    local dependency_name
    dependency_name="$(basename "$1")"

    case "${dependency_name}" in
        libX11*.so*|libXau*.so*|libXcursor*.so*|libXdmcp*.so*|libXext*.so*|libXfixes*.so*|\
        libXi*.so*|libXrandr*.so*|libXrender*.so*|libXtst*.so*|\
        libbsd*.so*|libbrotli*.so*|libbz2*.so*|libdbus-1.so*|libdouble-conversion.so*|\
        libexpat*.so*|libffi*.so*|libfontconfig.so*|libfreetype.so*|libglib-2.0.so*|\
        libgraphite2.so*|libharfbuzz*.so*|libmd*.so*|libpcre2-*.so*|libpng*.so*|\
        libuuid.so*|libxcb*.so*|libxkbcommon*.so*|libz.so*|libzstd.so*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

queue_file="${package_root}/.deps.queue"
seen_file="${package_root}/.deps.seen"
pending_file="${package_root}/.deps.pending"
: >"${queue_file}"
: >"${seen_file}"

{
    printf '%s\n' "${package_root}/bin/ZoinGallery"
    find "${package_root}/lib" "${package_root}/plugins" "${package_root}/qml" -type f \( -name '*.so' -o -name '*.so.*' \)
} | sort -u >"${queue_file}"

while true; do
    comm -23 <(sort -u "${queue_file}") <(sort -u "${seen_file}") >"${pending_file}"
    next_candidate="$(sed -n '1p' "${pending_file}")"
    [[ -n "${next_candidate}" ]] || break
    [[ -f "${next_candidate}" ]] || {
        printf '%s\n' "${next_candidate}" >>"${seen_file}"
        continue
    }

    printf '%s\n' "${next_candidate}" >>"${seen_file}"
    while IFS= read -r dependency; do
        [[ -f "${dependency}" ]] || continue
        case "${dependency}" in
            "${qt_root}/"*|*"/.conan2/"*|*"${repo_root}/build/qwindowkit-install/"*)
                cp -Lf "${dependency}" "${package_root}/lib/" || true
                ;;
            *)
                if should_bundle_system_dep "${dependency}"; then
                    cp -Lf "${dependency}" "${package_root}/lib/" || true
                fi
                ;;
        esac
        copied="${package_root}/lib/$(basename "${dependency}")"
        if [[ -f "${copied}" ]]; then
            printf '%s\n' "${copied}" >>"${queue_file}"
        fi
    done < <(collect_deps "${next_candidate}")
done

rm -f "${queue_file}" "${seen_file}" "${pending_file}"
find "${package_root}/lib" -type f -name '*.so*' -exec chmod u+w {} +

if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '$ORIGIN/../lib:$ORIGIN' "${package_root}/bin/ZoinGallery" || true
    find "${package_root}/lib" "${package_root}/plugins" "${package_root}/qml" -type f \( -name '*.so' -o -name '*.so.*' \) \
        -exec patchelf --set-rpath '$ORIGIN/../../../lib:$ORIGIN/../../lib:$ORIGIN/../lib:$ORIGIN' {} \; 2>/dev/null || true
fi

required_qml_plugin="${package_root}/qml/QtQuick/Controls/libqtquickcontrols2plugin.so"
if [[ ! -f "${required_qml_plugin}" ]]; then
    echo "Missing QtQuick.Controls QML plugin in Linux package: ${required_qml_plugin}" >&2
    exit 1
fi

required_xcb_cursor="${package_root}/lib/libxcb-cursor.so.0"
if [[ ! -f "${required_xcb_cursor}" ]]; then
    echo "Missing xcb cursor runtime library in Linux package: ${required_xcb_cursor}" >&2
    exit 1
fi

xcb_plugin="${package_root}/plugins/platforms/libqxcb.so"
if [[ -f "${xcb_plugin}" ]]; then
    missing_xcb_deps="$(LD_LIBRARY_PATH="${package_root}/lib:${LD_LIBRARY_PATH:-}" ldd "${xcb_plugin}" 2>/dev/null | awk '/not found/ { print }')"
    if [[ -n "${missing_xcb_deps}" ]]; then
        echo "Missing runtime dependencies for Qt xcb platform plugin:" >&2
        echo "${missing_xcb_deps}" >&2
        exit 1
    fi
fi

(cd "${repo_root}/build/artifacts" && tar --zstd -cf "${package_name}.tar.zst" "${package_name}")
