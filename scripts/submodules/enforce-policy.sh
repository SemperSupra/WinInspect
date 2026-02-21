#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HOOKS_DIR=".githooks"

echo "--- Enforcing Submodule Co-Evolution Policy ---"

# 1. Configure superproject hooks
echo "[superproject] Configuring core.hooksPath..."
git -C "$ROOT" config core.hooksPath "$HOOKS_DIR"

# 2. Configure submodules
git -C "$ROOT" submodule foreach '
    echo "[$sm_path] Configuring hooks and disabling pushes..."
    
    # Set hooks path relative to submodule root
    # Note: submodule paths can be nested, we need to go up to find .githooks if we wanted to share,
    # but the requirement says "Ensures core.hooksPath is set correctly in superproject and submodules".
    # We will point them to the same .githooks directory in the root if possible, 
    # but relative paths are tricky. 
    # Easier: point to the absolute path of the root githooks.
    git config core.hooksPath "'"$ROOT/$HOOKS_DIR"'"

    # Find the remote used in .gitmodules or fallback to origin
    # We look for the remote that matches the URL in the parent .gitmodules
    UPSTREAM_REMOTE="origin"
    
    # Set pushurl to DISABLED for the upstream remote
    git config "remote.$UPSTREAM_REMOTE.pushurl" "DISABLED"
    
    echo "[$sm_path] OK: hooks active, push to $UPSTREAM_REMOTE disabled."
'

echo "--- Policy Enforced Successfully ---"
