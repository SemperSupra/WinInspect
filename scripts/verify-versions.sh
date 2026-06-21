#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Mark E. DeYoung

# Verify version consistency across all versioned artifacts.
# Exit 0 if consistent, exit 1 if mismatched.
# Usage: scripts/verify-versions.sh [expected_version]

set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-}"

fail() { echo "ERROR: $1"; exit 1; }
extract() {
  # Extract a version string from a file using a regex pattern
  # Usage: extract <file> <pattern> <label>
  local file="$1" pattern="$2" label="$3"
  local val=$(grep -oP "$pattern" "$file" 2>/dev/null | head -1 || echo "")
  if [ -z "$val" ]; then fail "$label: not found in $file"; fi
  echo "$val"
}

echo "=== WinInspect Version Consistency Check ==="

# 1. PROTOCOL_VERSION (C++)
PROTO_CPP=$(extract "$ROOT/core/include/wininspect/types.hpp" \
  'PROTOCOL_VERSION = "\K[^"]+' "PROTOCOL_VERSION(C++)")

# 2. WININSPECT_VERSION (C++)
APP_CPP=$(extract "$ROOT/core/include/wininspect/types.hpp" \
  'WININSPECT_VERSION = "\K[^"]+' "WININSPECT_VERSION(C++)")

# 3. ProtocolVersion (Go)
PROTO_GO=$(extract "$ROOT/clients/portable/main.go" \
  'ProtocolVersion = "\K[^"]+' "ProtocolVersion(Go)")

# 4. Protocol doc
PROTO_DOC=$(extract "$ROOT/docs/PROTOCOL.md" \
  'Protocol Version\*\*: \K[0-9.]+' "ProtocolVersion(docs)")

# 5. Schema $id
SCHEMA_VER=$(extract "$ROOT/protocol/schema_v1.json" \
  'wininspect:protocol:v\K[0-9.]+' "Schema\$id")

# 6. CMake VERSION
CMAKE_VER=$(extract "$ROOT/CMakeLists.txt" \
  'project\(WinInspect VERSION \K[0-9.]+' "CMake(VERSION)")

# 7. PortableApps DisplayVersion
PA_VER=$(extract "$ROOT/deploy/portableapps/App/AppInfo/appinfo.ini" \
  'DisplayVersion=\K[0-9.]+' "PortableApps(DisplayVersion)")

# Cross-checks
echo ""
echo "Protocol:   C++=$PROTO_CPP  Go=$PROTO_GO  Doc=$PROTO_DOC  Schema=v$SCHEMA_VER"
echo "Release:    C++=$APP_CPP  CMake=$CMAKE_VER  PortableApps=$PA_VER"

# Protocol pool must be consistent
[ "$PROTO_CPP" = "$PROTO_GO" ] || fail "Protocol: C++($PROTO_CPP) != Go($PROTO_GO)"
[ "$PROTO_CPP" = "$PROTO_DOC" ] || fail "Protocol: C++($PROTO_CPP) != Docs($PROTO_DOC)"
[ "$PROTO_CPP" = "$SCHEMA_VER" ] || fail "Protocol: C++($PROTO_CPP) != Schema(v$SCHEMA_VER)"

# Release pool must be consistent
APP_STRIPPED="${APP_CPP#v}"
[ "$APP_STRIPPED" = "$CMAKE_VER" ] || fail "Release: WININSPECT_VERSION($APP_CPP) != CMake($CMAKE_VER)"
[ "$APP_STRIPPED" = "$PA_VER" ] || fail "Release: WININSPECT_VERSION($APP_CPP) != PortableApps($PA_VER)"

# If an expected version is passed, verify the release version matches
if [ -n "$VERSION" ]; then
  EXPECTED="${VERSION#v}"
  echo "Expected:   $EXPECTED"
  [ "$APP_STRIPPED" = "$EXPECTED" ] || fail "Release version $APP_STRIPPED != expected $EXPECTED"
fi

echo ""
echo "OK: All version artifacts are consistent."
