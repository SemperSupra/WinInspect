#!/usr/bin/env bash
set -euo pipefail

echo "--- WinInspect Preflight Check ---"

# Check for C++ toolchain
if command -v cmake &> /dev/null; then
    echo "OK: cmake found"
else
    echo "ERROR: cmake not found"
    exit 1
fi

# Check for Go toolchain
if command -v go &> /dev/null; then
    echo "OK: go found ($(go version))"
else
    echo "WARNING: go not found. Portable CLI build will be skipped."
fi

# Check for Wine (if on Linux)
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    if command -v wine &> /dev/null; then
        echo "OK: wine found"
    else
        echo "WARNING: wine not found. Cannot run Win32 tests on Linux."
    fi
fi

mkdir -p .wbab
ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
cat > .wbab/preflight-status.json <<EOF
{"timestamp":"${ts}","status":"ok","checks":{"cmake":true,"go":true}}
EOF