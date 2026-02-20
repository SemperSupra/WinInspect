#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/out" 

echo "--- WinInspect E2E Notepad Challenge ---"

# 1. Start Daemon
echo "[smoke] Starting wininspectd.exe..."
wine "${DIST_DIR}/wininspectd.exe" --headless &
DAEMON_PID=$!

cleanup() {
  echo "[smoke] Cleaning up..."
  kill $DAEMON_PID 2>/dev/null || true
  wineserver -k 2>/dev/null || true
}
trap cleanup EXIT
sleep 3

# 2. Run Health/Diagnostic Check
echo "[smoke] Running Health Check..."
HEALTH=$(wine "${DIST_DIR}/wininspect.exe" health)
echo "${HEALTH}"
if [[ "${HEALTH}" != *"diagnostics"* ]]; then
  echo "FAILURE: Health report missing diagnostics."
  exit 1
fi

# 3. Start Notepad
echo "[smoke] Spawning Notepad..."
wine notepad.exe &
sleep 2

# 4. Find Notepad HWND
echo "[smoke] Finding Notepad..."
NOTEPAD_HWND=$(wine "${DIST_DIR}/wininspect.exe" find-regex "Untitled - Notepad" "Notepad" | grep -o '0x[0-9A-F]\+' | head -n 1)
if [[ -z "${NOTEPAD_HWND}" ]]; then
  echo "FAILURE: Could not find Notepad HWND."
  exit 1
fi
echo "Found Notepad: ${NOTEPAD_HWND}"

# 5. Stealth Type into Notepad
echo "[smoke] Sending text to Notepad..."
wine "${DIST_DIR}/wininspect.exe" control-send "${NOTEPAD_HWND}" "SMOKE_TEST_PASSPHRASE"

# 6. Test Clipboard Write/Read
echo "[smoke] Testing Clipboard..."
wine "${DIST_DIR}/wininspect.exe" clip-write "WININSPECT_CLIPBOARD_SUCCESS"
CLIP_VAL=$(wine "${DIST_DIR}/wininspect.exe" clip-read)
if [[ "${CLIP_VAL}" == *"WININSPECT_CLIPBOARD_SUCCESS"* ]]; then
  echo "SUCCESS: Clipboard verified."
else
  echo "FAILURE: Clipboard data mismatch."
  exit 1
fi

# 7. Test Registry Write/Read
echo "[smoke] Testing Registry..."
wine "${DIST_DIR}/wininspect.exe" reg-write "HKCU\\Software\\WinInspectSmoke" "LastStatus" "SZ" "Passed"
REG_VAL=$(wine "${DIST_DIR}/wininspect.exe" reg-read "HKCU\\Software\\WinInspectSmoke")
if [[ "${REG_VAL}" == *"Passed"* ]]; then
  echo "SUCCESS: Registry verified."
else
  echo "FAILURE: Registry data mismatch."
  exit 1
fi

# 8. Kill Notepad
echo "[smoke] Killing Notepad..."
NOTEPAD_PID=$(wine "${DIST_DIR}/wininspect.exe" info "${NOTEPAD_HWND}" | grep -o '"pid":[0-9]\+' | cut -d: -f2)
wine "${DIST_DIR}/wininspect.exe" kill "${NOTEPAD_PID}"

echo "--- NOTEPAD CHALLENGE PASSED ---"
