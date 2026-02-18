#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-dev}"
WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${DIST_DIR}"

echo "--- Packaging WinInspect Installer (Version: ${VERSION}) ---"

if command -v makensis &> /dev/null; then
    # Default to standard local build path if not specified
    BUILD_SRC="${BUILD_SRC:-..\build\Release}"
    makensis -DVERSION="${VERSION}" -DBUILD_SRC="${BUILD_SRC}" "${WORKSPACE_DIR}/tools/wininspect.nsi"
    mv "${DIST_DIR}/WinInspect-Installer.exe" "${DIST_DIR}/WinInspect-Installer-${VERSION}.exe"
else
    echo "ERROR: makensis not found. Cannot build installer."
    exit 1
fi

echo "--- Packaging Complete ---"