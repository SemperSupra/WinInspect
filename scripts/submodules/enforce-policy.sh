#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOOKS_DIR=".githooks"

echo "--- Enforcing Engineering Policies (Co-Evolution + Pull-First) ---"

# 1. Configure superproject hooks
echo "[superproject] Configuring core.hooksPath..."
git -C "$ROOT" config core.hooksPath "$HOOKS_DIR"

# 2. Configure submodules
# We use a robust way to find submodules regardless of nesting or uninitialized state
git -C "$ROOT" submodule status --recursive | while read -r status path commit; do
    # status can be empty, +, or -
    echo "[$path] Configuring guardrails..."
    
    SUB_ABS="$ROOT/$path"
    
    # Propagate hooks
    git -C "$SUB_ABS" config core.hooksPath "$ROOT/$HOOKS_DIR"

    # Identify upstream remote
    # Most submodules use 'origin'. We set pushurl=DISABLED for it.
    # To be robust, we look for any remote matching the expected upstream patterns.
    REMOTES=$(git -C "$SUB_ABS" remote)
    for remote in $REMOTES; do
        URL=$(git -C "$SUB_ABS" remote get-url "$remote")
        if [[ "$URL" == *"SemperSupra/"* || "$URL" == *"mark-e-deyoung/WineBot"* ]]; then
            git -C "$SUB_ABS" config "remote.$remote.pushurl" "DISABLED"
            echo "[$path] OK: Push to $remote ($URL) disabled."
        fi
    done
done

echo "--- Policy Enforced Successfully ---"
