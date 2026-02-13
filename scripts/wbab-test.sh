#!/usr/bin/env bash
set -euo pipefail
# In WBAB mode, this would run layered tests + contract + policy gates.
echo "[wbab-test] running local ctest as placeholder."
ctest --test-dir build -C Release --output-on-failure
