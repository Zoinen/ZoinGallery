#!/usr/bin/env bash
set -euo pipefail

package_root="${1:?usage: smoke-test-linux-package.sh <package-root>}"
launcher="${package_root}/run-zoingallery.sh"

test -x "${launcher}"

run_briefly() {
    env -u LD_LIBRARY_PATH -u QT_PLUGIN_PATH -u QML2_IMPORT_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
        QT_DEBUG_PLUGINS=1 \
        "${launcher}" >"${RUNNER_TEMP:-/tmp}/zoingallery-package-smoke.out" 2>"${RUNNER_TEMP:-/tmp}/zoingallery-package-smoke.err" &
    local pid=$!
    sleep 8

    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
        echo "Packaged ZoinGallery started and stayed alive for the smoke window."
        return 0
    fi

    local status=0
    wait "${pid}" || status=$?
    echo "--- stdout ---"
    sed -n '1,120p' "${RUNNER_TEMP:-/tmp}/zoingallery-package-smoke.out" || true
    echo "--- stderr ---"
    sed -n '1,160p' "${RUNNER_TEMP:-/tmp}/zoingallery-package-smoke.err" || true
    return "${status}"
}

if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "${BASH_SOURCE[0]}" "${package_root}"
    exit $?
fi

run_briefly
