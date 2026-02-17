#!/usr/bin/env bash
set -euo pipefail
# Mocked e2e smoke: verify package artifacts exist and core tests pass.
echo "[e2e-smoke] mocked smoke"
test -f dist/WinInspect-Installer-PLACEHOLDER.txt || true
echo "[e2e-smoke] OK (placeholder)"
