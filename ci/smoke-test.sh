#!/usr/bin/env bash
set -euo pipefail

qt_root="${1:?usage: smoke-test.sh <qt-root>}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_briefly() {
    local executable="$1"
    shift

    "$executable" "$@" >"${RUNNER_TEMP:-/tmp}/zoingallery-smoke.out" 2>"${RUNNER_TEMP:-/tmp}/zoingallery-smoke.err" &
    local pid=$!
    sleep 8

    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
        echo "ZoinGallery started and stayed alive for the smoke window."
        return 0
    fi

    local status=0
    wait "${pid}" || status=$?
    echo "--- stdout ---"
    sed -n '1,120p' "${RUNNER_TEMP:-/tmp}/zoingallery-smoke.out" || true
    echo "--- stderr ---"
    sed -n '1,160p' "${RUNNER_TEMP:-/tmp}/zoingallery-smoke.err" || true

    if [[ "${status}" -eq 0 ]]; then
        echo "ZoinGallery exited cleanly during the smoke window."
        return 0
    fi

    return "${status}"
}

case "$(uname -s)" in
    Darwin)
        app="${repo_root}/build/bin/RelWithDebInfo/ZoinGallery.app/Contents/MacOS/ZoinGallery"
        test -x "${app}"
        export DYLD_LIBRARY_PATH="${qt_root}/lib:${repo_root}/build/qwindowkit-install/lib:${repo_root}/build/lib/RelWithDebInfo:${DYLD_LIBRARY_PATH:-}"
        run_briefly "${app}"
        ;;
    Linux)
        executable="$(find "${repo_root}/build/bin" -type f -name ZoinGallery -perm -111 | head -n 1)"
        test -n "${executable}"
        export LD_LIBRARY_PATH="${qt_root}/lib:${repo_root}/build/qwindowkit-install/lib:${repo_root}/build/lib/RelWithDebInfo:${LD_LIBRARY_PATH:-}"
        export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
        export QSG_RHI_BACKEND="${QSG_RHI_BACKEND:-opengl}"

        if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
            xvfb-run -a "${BASH_SOURCE[0]}" "${qt_root}"
            exit $?
        fi

        if [[ -z "${DISPLAY:-}" ]]; then
            Xvfb :99 -screen 0 1280x800x24 >"${RUNNER_TEMP:-/tmp}/zoingallery-xvfb.log" 2>&1 &
            xvfb_pid=$!
            trap 'kill "${xvfb_pid}" 2>/dev/null || true' EXIT
            export DISPLAY=:99
            sleep 2
        fi

        run_briefly "${executable}"
        ;;
    *)
        echo "Unsupported smoke-test platform: $(uname -s)" >&2
        exit 2
        ;;
esac
