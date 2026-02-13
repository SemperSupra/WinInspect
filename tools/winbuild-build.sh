#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${WORKSPACE_DIR}/build"
DIST_DIR="${WORKSPACE_DIR}/dist"
VERSION="${1:-dev}"

mkdir -p "${DIST_DIR}"

echo "--- Building WinInspect Core & Win32 Clients (Version: ${VERSION}) ---"
cmake -S "${WORKSPACE_DIR}" -B "${BUILD_DIR}" -DWININSPECT_BUILD_TESTS=ON
cmake --build "${BUILD_DIR}" --config Release

cp "${BUILD_DIR}/wininspectd.exe" "${DIST_DIR}/wininspectd-${VERSION}-win-x64.exe" 2>/dev/null || cp "${BUILD_DIR}/Release/wininspectd.exe" "${DIST_DIR}/wininspectd-${VERSION}-win-x64.exe"
cp "${BUILD_DIR}/wininspect.exe" "${DIST_DIR}/wininspect-${VERSION}-win-x64.exe" 2>/dev/null || cp "${BUILD_DIR}/Release/wininspect.exe" "${DIST_DIR}/wininspect-${VERSION}-win-x64.exe"
cp "${BUILD_DIR}/wininspect-gui.exe" "${DIST_DIR}/wininspect-gui-${VERSION}-win-x64.exe" 2>/dev/null || cp "${BUILD_DIR}/Release/wininspect-gui.exe" "${DIST_DIR}/wininspect-gui-${VERSION}-win-x64.exe"

echo "--- Building Portable CLI (Go) ---"
if command -v go &> /dev/null; then
    pushd "${WORKSPACE_DIR}/clients/portable" > /dev/null
    
    echo "  Building for Linux (x64)..."
    GOOS=linux GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-${VERSION}-linux-x64"
    
    echo "  Building for Windows (x64)..."
    GOOS=windows GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-${VERSION}-win-x64.exe"
    
    echo "  Building for macOS (Universal)..."
    GOOS=darwin GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-${VERSION}-mac-x64"
    GOOS=darwin GOARCH=arm64 go build -o "${DIST_DIR}/wi-portable-${VERSION}-mac-arm64"
    
    popd > /dev/null
else
    echo "Skipping Go build (compiler not found)."
fi

echo "--- Build Complete ---"
echo "Artifacts located in: ${DIST_DIR}"