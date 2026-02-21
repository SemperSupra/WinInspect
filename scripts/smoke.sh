#!/usr/bin/env bash
set -euo pipefail

# WinInspect Smoke Test / Bisecting Challenge (Fresh Session)
# 1. Start a COMPLETELY NEW WineBot session.
# 2. Map custom ports without modifying the base WineBot repo.
# 3. Run all functional tests INTERNALLY (Container-only, Pipe IPC).
# 4. Run connectivity tests EXTERNALLY (Host-to-Guest, TCP IPC).

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/out"
WBAB_WINEBOT_DIR="${ROOT}/external/WineBot"
COMPOSE_DIR="${WBAB_WINEBOT_DIR}/compose"
PROJECT_NAME="wininspect-smoke"
WINEBOT_IMAGE="ghcr.io/mark-e-deyoung/winebot:v0.9.5"

echo "--- WinInspect Fresh-Session Bisecting Challenge ---"

# 1. Cleanup existing smoke session
echo "[smoke] Cleaning up any existing sessions..."
docker compose -p "${PROJECT_NAME}" -f "${COMPOSE_DIR}/docker-compose.yml" --profile interactive down -v || true

# 2. Create dynamic override for ports
OVERRIDE_FILE="${ROOT}/scripts/smoke-override.yml"
cat >"${OVERRIDE_FILE}" <<EOF
services:
  winebot-interactive:
    image: ${WINEBOT_IMAGE}
    ports:
      - "1985:1985"
      - "1986:1986"
      - "1987:1987"
      - "5900:5900"
      - "6080:6080"
EOF

# 3. Start fresh session
echo "[smoke] Starting FRESH WineBot session..."
docker compose -p "${PROJECT_NAME}" -f "${COMPOSE_DIR}/docker-compose.yml" -f "${OVERRIDE_FILE}" --profile interactive up -d

# 4. Wait for health (simple wait since we don't have the health check port 8000 easily accessible yet)
echo "[smoke] Waiting for WineBot to initialize..."
sleep 10

CONTAINER_ID=$(docker ps -aqf "name=${PROJECT_NAME}-winebot-interactive-1")
if [[ -z "${CONTAINER_ID}" ]]; then
  echo "FAILURE: Could not find fresh container."
  exit 1
fi

# 5. Deploy & Start Daemon
echo "[smoke] Deploying binaries to container ${CONTAINER_ID}..."
docker cp "${DIST_DIR}/wininspectd.exe" "${CONTAINER_ID}:/home/winebot/wininspectd.exe"
docker cp "${DIST_DIR}/wininspect.exe" "${CONTAINER_ID}:/home/winebot/wininspect.exe"

echo "[smoke] Starting wininspectd.exe inside container..."
docker exec -d --user winebot -w /home/winebot "${CONTAINER_ID}" \
  bash -c "wine wininspectd.exe --headless --port 1985 --public --log-level DEBUG > daemon.log 2>&1"

echo "[smoke] Waiting for TCP Server to listen..."
for i in {1..10}; do
  if docker exec "${CONTAINER_ID}" grep -q "TCP Server listening" /home/winebot/daemon.log 2>/dev/null; then
    echo "SUCCESS: Daemon is listening."
    break
  fi
  echo "Waiting for daemon ($i/10)..."
  sleep 2
  if [[ $i -eq 10 ]]; then
    echo "FAILURE: Daemon did not start listening in time."
    docker exec "${CONTAINER_ID}" cat /home/winebot/daemon.log || true
    exit 1
  fi
done

echo -e "\n--- PHASE 1: INTERNAL CONTAINER TESTS ---"

run_internal() {
  docker exec --user winebot -e WINEPREFIX=/wineprefix -w /home/winebot "${CONTAINER_ID}" wine wininspect.exe "$@"
}

echo "[internal] Checking health (with retries)..."

for i in {1..10}; do
  echo "Internal connection attempt $i/10..."
  docker exec "${CONTAINER_ID}" ss -tunl | grep 1985 || echo "Port 1985 not yet in ss"
  
  if run_internal health --session_id "internal-session" > internal_out.txt 2>&1; then
    cat internal_out.txt
    echo "SUCCESS: Internal connection established."
    break
  fi
  cat internal_out.txt || true
  echo "Retrying internal connection..."
  sleep 3
  if [[ $i -eq 10 ]]; then
    echo "FAILURE: Internal client could not connect to daemon."
    docker exec "${CONTAINER_ID}" cat /home/winebot/daemon.log || true
    exit 1
  fi
done

echo "[internal] Starting Notepad..."
docker exec -d --user winebot -e WINEPREFIX=/wineprefix "${CONTAINER_ID}" wine notepad.exe
sleep 3

echo "[internal] Finding Notepad..."
NOTEPAD_HWND=$(run_internal find-regex "Untitled - Notepad" "Notepad" --session_id "internal-session" | grep -o '0x[0-9A-F]\+' | head -n 1)
if [[ -z "${NOTEPAD_HWND}" ]]; then
  echo "FAILURE: Internal client could not find Notepad."
  exit 1
fi
echo "Found Notepad: ${NOTEPAD_HWND}"

echo "[internal] Sending stealth input..."
run_internal control-send "${NOTEPAD_HWND}" "INTERNAL_PIPE_SUCCESS" --session_id "internal-session"

echo "[internal] Verifying Registry..."
run_internal reg-write "HKCU\\Software\\WinInspectSmoke" "InternalTest" "SZ" "Passed" --session_id "internal-session"
REG_RES=$(run_internal reg-read "HKCU\\Software\\WinInspectSmoke" --session_id "internal-session")
if [[ "${REG_RES}" == *"Passed"* ]]; then
  echo "SUCCESS: Internal Registry check passed."
else
  echo "FAILURE: Internal Registry mismatch."
  exit 1
fi

echo "[internal] Cleaning up Notepad..."
NOTEPAD_PID=$(run_internal info "${NOTEPAD_HWND}" --session_id "internal-session" | grep -o '"pid":[0-9]\+' | cut -d: -f2)
run_internal kill "${NOTEPAD_PID}" --session_id "internal-session"

echo "--- PHASE 1 PASSED: INTERNAL LOGIC IS CORRECT ---"

# --- PHASE 2: EXTERNAL CONNECTIVITY ---
echo -e "\n--- PHASE 2: EXTERNAL HOST-TO-GUEST TESTS ---"

CONTAINER_IP=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "${CONTAINER_ID}")
PORTABLE_BIN="${ROOT}/dist/wi-portable-dev-linux-x64"
if [[ "$(uname -s)" != "Linux" ]]; then
  PORTABLE_BIN="${ROOT}/dist/wi-portable-dev-win-x64.exe"
fi

echo "[external] Container IP: ${CONTAINER_IP}"
echo "[external] Host binary: ${PORTABLE_BIN}"

# Use direct container IP for host-to-guest test
echo "[external] Testing connection to ${CONTAINER_IP}:1985..."
for i in {1..5}; do
  if "${PORTABLE_BIN}" -tcp "${CONTAINER_IP}:1985" health --session_id "external-session" > /dev/null 2>&1; then
    echo "SUCCESS: External connection established."
    break
  fi
  echo "Retrying external connection ($i/5)..."
  sleep 2
  if [[ $i -eq 5 ]]; then
    echo "FAILURE: Could not connect to daemon via TCP from host."
    echo "Dumping daemon.log from container:"
    docker exec "${CONTAINER_ID}" cat /home/winebot/daemon.log
    exit 1
  fi
done

echo "[external] Requesting top windows via host-native CLI..."
"${PORTABLE_BIN}" -tcp "${CONTAINER_IP}:1985" top --session_id "external-session"

echo -e "\n--- FRESH SESSION BISECTING CHALLENGE PASSED ---"

# Optional: keep alive for manual check if needed, but here we cleanup
docker compose -p "${PROJECT_NAME}" -f "${COMPOSE_DIR}/docker-compose.yml" -f "${OVERRIDE_FILE}" --profile interactive down -v
rm "${OVERRIDE_FILE}"
