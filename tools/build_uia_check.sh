#!/usr/bin/env bash
set -e
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SRC_DIR}"

if [[ -z "${CXX}" ]]; then
  if command -v x86_64-w64-mingw32-g++ >/dev/null; then
    CXX=x86_64-w64-mingw32-g++
  elif command -v cl >/dev/null; then
    # MSVC
    cl check_uia.cpp /link ole32.lib oleaut32.lib uuid.lib
    echo "Built check_uia.exe"
    exit 0
  else
    echo "No suitable cross-compiler found (checked x86_64-w64-mingw32-g++ and cl)."
    exit 1
  fi
fi

echo "Compiling using ${CXX}..."
"${CXX}" -o check_uia.exe check_uia.cpp -lole32 -loleaut32 -luuid
echo "Built check_uia.exe"
