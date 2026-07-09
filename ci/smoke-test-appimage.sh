#!/usr/bin/env bash
set -euo pipefail

appimage="${1:?usage: smoke-test-appimage.sh <appimage>}"
test -x "${appimage}"

run_briefly() {
    APPIMAGE_EXTRACT_AND_RUN=1 "${appimage}" >"${RUNNER_TEMP:-/tmp}/zoingallery-appimage-smoke.out" 2>"${RUNNER_TEMP:-/tmp}/zoingallery-appimage-smoke.err" &
    local pid=$!
    sleep 8

    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
        echo "ZoinGallery AppImage started and stayed alive for the smoke window."
        return 0
    fi

    local status=0
    wait "${pid}" || status=$?
    echo "--- stdout ---"
    sed -n '1,120p' "${RUNNER_TEMP:-/tmp}/zoingallery-appimage-smoke.out" || true
    echo "--- stderr ---"
    sed -n '1,180p' "${RUNNER_TEMP:-/tmp}/zoingallery-appimage-smoke.err" || true
    return "${status}"
}

if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "${BASH_SOURCE[0]}" "${appimage}"
    exit $?
fi

run_briefly
