#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Mark E. DeYoung

set -euo pipefail
# In WBAB mode, this would run layered tests + contract + policy gates.
echo "[wbab-test] running local ctest as placeholder."
ctest --test-dir build -C Release --output-on-failure

if command -v go &> /dev/null; then
    if [ -d "clients/portable" ]; then
        echo "Running Go tests..."
        pushd "clients/portable" > /dev/null
        go test ./...
        popd > /dev/null
    fi
fi
