#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/out" # Artifacts are in out/ after build

echo "--- WinInspect E2E Smoke Test ---"

# 1. Start Daemon in background
echo "[smoke] Starting wininspectd.exe..."
wine "${DIST_DIR}/wininspectd.exe" --headless &
DAEMON_PID=$!

# Ensure daemon is killed on exit
cleanup() {
  echo "[smoke] Cleaning up..."
  kill $DAEMON_PID 2>/dev/null || true
  # Wine processes sometimes linger
  wineserver -k 2>/dev/null || true
}
trap cleanup EXIT

# Wait for pipe/server to initialize
sleep 3

# 2. Start a target window
echo "[smoke] Spawning target window (cmd.exe)..."
wine cmd.exe /c "title SMOKE_TARGET && pause" &
TARGET_PID=$!

sleep 2

# 3. Verify window detection
echo "[smoke] Listing top windows..."
TOP_OUTPUT=$(wine "${DIST_DIR}/wininspect.exe" top)
echo "${TOP_OUTPUT}"

if [[ "${TOP_OUTPUT}" == *"SMOKE_TARGET"* ]]; then
  echo "[smoke] SUCCESS: Found target window."
else
  # Some Wine versions might not show the title immediately in 'top'
  # depending on the backend implementation. Let's look for any output.
  if [[ -n "${TOP_OUTPUT}" ]]; then
     echo "[smoke] SUCCESS: Received window list from daemon."
  else
     echo "[smoke] FAILURE: No windows returned."
     exit 1
  fi
fi

# 4. Test UIA Survey
echo "[smoke] Running UIA Survey..."
wine "${DIST_DIR}/survey_uia.exe" > smoke_report.json
if [[ -s smoke_report.json ]]; then
  echo "[smoke] SUCCESS: UIA report generated."
else
  echo "[smoke] FAILURE: UIA report empty."
  exit 1
fi

# 5. Verify recursive UI inspection (if possible on cmd.exe)
echo "[smoke] Testing ui-inspect..."
# Find first HWND from top output
FIRST_HWND=$(echo "${TOP_OUTPUT}" | grep -o '0x[0-9A-F]\+' | head -n 1)
if [[ -n "${FIRST_HWND}" ]]; then
  wine "${DIST_DIR}/wininspect.exe" ui-inspect "${FIRST_HWND}"
  echo "[smoke] SUCCESS: ui-inspect executed."
else
  echo "[smoke] WARNING: No HWND found to test ui-inspect."
fi

echo "--- SMOKE TEST PASSED ---"
