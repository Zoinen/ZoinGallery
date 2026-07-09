#!/usr/bin/env bash
set -euo pipefail

bundle="${1:?usage: smoke-test-flatpak.sh <flatpak-bundle>}"
app_id="io.github.Zoinen.ZoinGallery"
test -f "${bundle}"

script="$(mktemp)"
cat >"${script}" <<'EOF'
set -euo pipefail
bundle="$1"
app_id="$2"

flatpak --user install -y --bundle "${bundle}"

flatpak --user run "${app_id}" >"${RUNNER_TEMP:-/tmp}/zoingallery-flatpak-smoke.out" 2>"${RUNNER_TEMP:-/tmp}/zoingallery-flatpak-smoke.err" &
pid=$!
sleep 8

if kill -0 "${pid}" 2>/dev/null; then
    kill "${pid}" 2>/dev/null || true
    wait "${pid}" 2>/dev/null || true
    echo "ZoinGallery Flatpak started and stayed alive for the smoke window."
    exit 0
fi

status=0
wait "${pid}" || status=$?
echo "--- stdout ---"
sed -n '1,120p' "${RUNNER_TEMP:-/tmp}/zoingallery-flatpak-smoke.out" || true
echo "--- stderr ---"
sed -n '1,220p' "${RUNNER_TEMP:-/tmp}/zoingallery-flatpak-smoke.err" || true
exit "${status}"
EOF

if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a dbus-run-session -- bash "${script}" "${bundle}" "${app_id}"
else
    dbus-run-session -- bash "${script}" "${bundle}" "${app_id}"
fi

rm -f "${script}"
