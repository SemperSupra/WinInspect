#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-dev}"
WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${DIST_DIR}"

echo "--- Packaging WinInspect Installer (Version: ${VERSION}) ---"

PACKAGER_IMAGE="ghcr.io/sempersupra/winebotappbuilder-packager:latest"

echo "--- Pulling WBAB Packager Image ---"
docker pull "${PACKAGER_IMAGE}"

docker run --rm \
    -v "${WORKSPACE_DIR}:/v" \
    -w /v \
    "${PACKAGER_IMAGE}" \
    bash -c "makensis -DVERSION=${VERSION} -DBUILD_SRC=/v/build tools/wininspect.nsi"

if [[ -f "${DIST_DIR}/WinInspect-Installer.exe" ]]; then
    mv "${DIST_DIR}/WinInspect-Installer.exe" "${DIST_DIR}/WinInspect-Installer-${VERSION}.exe"
    echo "--- Packaging Complete ---"
else
    echo "ERROR: Installer not found after container run."
    exit 1
fi