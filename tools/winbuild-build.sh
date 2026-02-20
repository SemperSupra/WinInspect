#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${WORKSPACE_DIR}/dist"
VERSION="${1:-dev}"
WBAB_IMAGE="ghcr.io/sempersupra/winebotappbuilder-winbuild:latest"
GOFLOW_IMAGE="supragoflow-build:local"

mkdir -p "${DIST_DIR}"

echo "--- Pulling WBAB Canonical Image ---"
docker pull "${WBAB_IMAGE}"

echo "--- Building C/C++ Components via WBAB ---"
docker run --rm \
    -v "${WORKSPACE_DIR}:/v" \
    -w /v \
    -e VERSION="${VERSION}" \
    "${WBAB_IMAGE}" \
    wbab-build

echo "--- Building Portable CLI via SupraGoFlow ---"
# SupraGoFlow expects to run at the root of the Go module
docker run --rm \
    -v "${WORKSPACE_DIR}/clients/portable:/v" \
    -v "${DIST_DIR}:/dist" \
    -w /v \
    -e GOOS=windows \
    -e GOARCH=amd64 \
    "${GOFLOW_IMAGE}" \
    bash -c "go build -o /dist/wi-portable-${VERSION}-win-x64.exe"

# Robustly find binaries generated in the container and move them to versioned names if needed
# (Though the bash script above handles the Go CLI, we still need to collect the C++ ones)
find_and_cp() {
    local name=$1
    local target=$2
    local found=$(find "${WORKSPACE_DIR}/build" -name "${name}" -type f | head -n 1)
    if [[ -n "${found}" ]]; then
        cp "${found}" "${target}"
    else
        echo "WARNING: Could not find ${name}"
    fi
}

find_and_cp "wininspectd.exe" "${DIST_DIR}/wininspectd-${VERSION}-win-x64.exe"
find_and_cp "wininspect.exe" "${DIST_DIR}/wininspect-${VERSION}-win-x64.exe"
find_and_cp "wininspect-gui.exe" "${DIST_DIR}/wininspect-gui-${VERSION}-win-x64.exe"

echo "--- Build Complete ---"
echo "Artifacts located in: ${DIST_DIR}"
