#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <submodule_path> <upstream_ref>"
    exit 1
fi

SM_PATH="$1"
UPSTREAM_REF="$2"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "--- Converging Submodule $SM_PATH to Upstream $UPSTREAM_REF ---"

# 1. Hard reset and clean submodule
echo "[submodule] Resetting working tree..."
cd "$ROOT/$SM_PATH"
git fetch origin # Ensure we have the latest from upstream
git checkout "$UPSTREAM_REF"
git reset --hard "origin/$UPSTREAM_REF" 2>/dev/null || git reset --hard "$UPSTREAM_REF"
git clean -fdx

# 2. Update superproject index
echo "[superproject] Updating submodule pointer..."
cd "$ROOT"
git add "$SM_PATH"

echo -e "\n--- Convergence Complete ---"
echo "Submodule $SM_PATH is now at $UPSTREAM_REF."
echo "To commit this change in the superproject, run:"
echo "  git commit -m \"build: update submodule $SM_PATH to $UPSTREAM_REF\""
