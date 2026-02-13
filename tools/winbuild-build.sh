#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${WORKSPACE_DIR}/build"
DIST_DIR="${WORKSPACE_DIR}/dist"

mkdir -p "${DIST_DIR}"

echo "--- Building WinInspect Core & Win32 Clients ---"
cmake -S "${WORKSPACE_DIR}" -B "${BUILD_DIR}" -DWININSPECT_BUILD_TESTS=ON
cmake --build "${BUILD_DIR}" --config Release

echo "--- Building Portable CLI (Go) ---"
if command -v go &> /dev/null; then
    pushd "${WORKSPACE_DIR}/clients/portable" > /dev/null
    
    echo "  Building for Linux (x64)..."
    GOOS=linux GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-linux-x64"
    
    echo "  Building for Windows (x64)..."
    GOOS=windows GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-win-x64.exe"
    
    echo "  Building for macOS (Universal)..."
    GOOS=darwin GOARCH=amd64 go build -o "${DIST_DIR}/wi-portable-mac-x64"
    GOOS=darwin GOARCH=arm64 go build -o "${DIST_DIR}/wi-portable-mac-arm64"
    
    popd > /dev/null
else
    echo "Skipping Go build (compiler not found)."
fi

echo "--- Build Complete ---"
echo "Artifacts located in: ${DIST_DIR}"