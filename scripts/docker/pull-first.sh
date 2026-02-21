#!/usr/bin/env bash
set -euo pipefail

# Strict Pull-First Helper
# Enforces that canonical images are pulled from GHCR before docker run.

IMAGE_REF="$1"
DOCKERFILE_PATH="${2:-}" # Optional: Path to local Dockerfile for ALLOW_LOCAL_BUILD

DEBUG="${DEBUG:-0}"
ALLOW_LOCAL_BUILD="${ALLOW_LOCAL_BUILD:-0}"

if [[ "${DEBUG}" == "1" ]]; then
    echo "[pull-first] Resolved Image: ${IMAGE_REF}"
fi

if [[ "${ALLOW_LOCAL_BUILD}" == "1" && -n "${DOCKERFILE_PATH}" && -f "${DOCKERFILE_PATH}" ]]; then
    CONTEXT_DIR=$(dirname "${DOCKERFILE_PATH}")
    if [[ "${DEBUG}" == "1" ]]; then
        echo "[pull-first] ALLOW_LOCAL_BUILD=1 detected. Building from ${DOCKERFILE_PATH}..."
    fi
    docker build -t "${IMAGE_REF}" -f "${DOCKERFILE_PATH}" "${CONTEXT_DIR}"
else
    if [[ "${DEBUG}" == "1" ]]; then
        echo "[pull-first] Enforcing pull for ${IMAGE_REF}..."
    fi
    docker pull "${IMAGE_REF}"
fi
