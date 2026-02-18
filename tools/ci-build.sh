#!/usr/bin/env bash
set -euo pipefail

# CI Build script for WBAB container (Cross-compile + Wine setup)

echo "--- Building WinInspect Core & Win32 Clients (Containerized) ---"

mkdir -p build
cd build

# Configure CMake for MinGW-w64 cross-compilation with Wine emulator for tests
cmake -S .. -B . \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres \
    -DCMAKE_FIND_ROOT_PATH=/usr/x86_64-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
    -DWININSPECT_BUILD_TESTS=ON

cmake --build . --config Release

# Copy artifacts to out/ (expected by WBAB convention)
echo "--- Copying artifacts to out/ ---"
rm -rf ../out
mkdir -p ../out
find . -name "*.exe" -exec cp -f {} "../out/" \;
find . -name "*.dll" -exec cp -f {} "../out/" \;
