#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <submodule_path> <topic_branch> [<base_ref>]"
    exit 1
fi

SM_PATH="$1"
TOPIC_BRANCH="$2"
BASE_REF="${3:-master}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SM_NAME=$(basename "$SM_PATH")
EXPORT_DIR="$ROOT/third_party/patches/$SM_NAME/$TOPIC_BRANCH"

mkdir -p "$EXPORT_DIR/patches"

echo "--- Generating Issue Package for $SM_NAME ($TOPIC_BRANCH) ---"

# Enter submodule
cd "$ROOT/$SM_PATH"

# Ensure we are on the topic branch
# git checkout "$TOPIC_BRANCH" # Optional, assume user is there or wants patches from there

# 1. patches
echo "[export] Generating patches..."
git format-patch "$BASE_REF..$TOPIC_BRANCH" -o "$EXPORT_DIR/patches/" > /dev/null

# 2. diffstat
echo "[export] Generating diffstat..."
git diff --stat "$BASE_REF..$TOPIC_BRANCH" > "$EXPORT_DIR/diffstat.txt"

# 3. range-diff
echo "[export] Generating range-diff..."
git range-diff "$BASE_REF..$TOPIC_BRANCH" "$BASE_REF..$TOPIC_BRANCH" > "$EXPORT_DIR/range-diff.txt" 2>/dev/null || true

# 4. summary template
echo "[export] Creating summary template..."
cat > "$EXPORT_DIR/summary.md" <<TEMPLATE
# Upstream Issue Summary: $SM_NAME

## Problem Statement
<!-- Describe the problem this patch set solves -->

## Reproduction Notes
<!-- Provide steps to reproduce the issue or verify the fix -->

## Implementation Strategy
<!-- Explain the approach taken in the attached patches -->

## Acceptance Criteria
<!-- What must be true for this issue to be considered resolved? -->

## Patch Series
- Count: $(ls "$EXPORT_DIR/patches/" | wc -l)
- Base: $BASE_REF
TEMPLATE

echo "--- Issue Package Exported to: $EXPORT_DIR ---"
echo "Next steps:"
echo "1. Open an issue in the upstream repository."
echo "2. Paste the summary.md content."
echo "3. Attach the files in $EXPORT_DIR/patches/"
