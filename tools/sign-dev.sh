#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-dev}"
WORKSPACE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${WORKSPACE_DIR}/dist"
IMAGE="ghcr.io/sempersupra/winebotappbuilder-signer:latest"

mkdir -p "${DIST_DIR}"

echo "--- Signing WinInspect Binaries via WBAB Container ---"

docker run --rm \
    -v "${WORKSPACE_DIR}:/v" \
    -w /v \
    "${IMAGE}" \
    bash -c "echo 'Signing logic here using osslsigncode inside container'"

echo "--- Signing Complete ---"