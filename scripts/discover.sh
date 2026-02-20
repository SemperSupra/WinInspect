#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${ROOT}/build/tools/survey-uia/survey_uia.exe"

echo "--- WinInspect Discovery ---"

if [[ -f "${BIN_PATH}" ]]; then
    echo "[discover] Running UIA Survey..."
    wine "${BIN_PATH}" > "${ROOT}/uia-report.json"
    echo "[discover] UIA Report saved to uia-report.json"
else
    echo "[discover] survey_uia.exe not found at ${BIN_PATH}. Please build it first."
    echo "[discover] Run: tools/wbab build"
fi

echo "[discover] Enumerating top-level windows..."
if [[ -f "${ROOT}/build/wininspect.exe" ]]; then
    wine "${ROOT}/build/wininspect.exe" top
else
    echo "[discover] wininspect.exe not found. Skipping window enumeration."
fi
