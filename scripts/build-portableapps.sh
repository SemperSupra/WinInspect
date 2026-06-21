#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Mark E. DeYoung

set -euo pipefail

# Build portable distributions for WinInspect.
# Usage: scripts/build-portableapps.sh <version> [build_dir]
#
# Produces:
#   dist/WinInspectPortable-<version>.zip     (always — ready to extract)
#   dist/WinInspectPortable-<version>.paf.exe (best-effort — needs launcher binary)
#
# The .zip can be extracted onto any PortableApps.com USB drive, or used
# standalone by agents as a zero-install deployment.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-dev}"
BUILD_DIR="${2:-$ROOT/build/Release}"
STAGE_DIR="$ROOT/build/portable-staging"
DIST_DIR="$ROOT/dist"
LAUNCHER_URL="https://downloads.sourceforge.net/portableapps/PortableApps.comLauncher_3.2.4.paf.exe"
INSTALLER_URL="https://downloads.sourceforge.net/portableapps/PortableApps.comInstaller_3.2.4.paf.exe"

echo "=== WinInspect Portable Distribution Builder ==="
echo "Version: $VERSION"
echo "Build dir: $BUILD_DIR"

# 1. Clean and create staging layout
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/App/WinInspect"
mkdir -p "$DIST_DIR"

# 2. Stage binaries
echo "[1/6] Staging binaries..."
for bin in wininspectd.exe wininspect.exe wininspect-gui.exe; do
  src=$(find "$BUILD_DIR" -name "$bin" -type f | head -1)
  if [[ -z "$src" ]]; then
    echo "ERROR: $bin not found in $BUILD_DIR"
    exit 1
  fi
  cp "$src" "$STAGE_DIR/App/WinInspect/"
  echo "  $bin -> App/WinInspect/"
done

# 3. Copy portable app metadata
echo "[2/6] Copying portable app config..."
cp -r "$ROOT/deploy/portableapps/App/AppInfo" "$STAGE_DIR/App/"
cp "$ROOT/LICENSE" "$STAGE_DIR/App/WinInspect/"

# Update version in appinfo.ini
sed -i "s/^DisplayVersion=.*/DisplayVersion=$VERSION/" "$STAGE_DIR/App/AppInfo/appinfo.ini"
sed -i "s/^PackageVersion=.*/PackageVersion=${VERSION}.0/" "$STAGE_DIR/App/AppInfo/appinfo.ini"

# 4. Always produce a portable ZIP
echo "[3/6] Creating portable ZIP..."
ZIP_OUT="$DIST_DIR/WinInspectPortable-${VERSION}.zip"
(cd "$STAGE_DIR" && zip -9qr "$ZIP_OUT" .)
echo "  $ZIP_OUT"
sha256sum "$ZIP_OUT" > "$ZIP_OUT.sha256"
echo "  SHA256: $(cat $ZIP_OUT.sha256)"

# 5. Download and extract launcher binary
echo "[4/6] Setting up PortableApps.com Launcher..."
LAUNCHER_BIN="$STAGE_DIR/WinInspectPortable.exe"
LAUNCHER_DL="$STAGE_DIR/../launcher.paf.exe"

curl -L -o "$LAUNCHER_DL" "$LAUNCHER_URL" 2>/dev/null && {
  echo "  Downloaded launcher, extracting..."
  if command -v 7z &>/dev/null; then
    7z x "$LAUNCHER_DL" -o"$STAGE_DIR/launcher-extract" -y >/dev/null 2>&1 || true
    FOUND=$(find "$STAGE_DIR/launcher-extract" -name "*.exe" -not -name "*.paf.*" -not -name "unins*" | head -1)
    if [[ -n "$FOUND" ]]; then
      cp "$FOUND" "$LAUNCHER_BIN"
      echo "  Launcher extracted: $LAUNCHER_BIN"
    fi
  else
    echo "  WARNING: 7z not available, cannot extract launcher."
  fi
} || echo "  WARNING: Could not download launcher. .paf.exe will be skipped."

# 6. Build .paf.exe (best-effort)
echo "[5/6] Building .paf.exe..."
if [[ -f "$LAUNCHER_BIN" ]]; then
  INSTALLER_BIN="$STAGE_DIR/../PortableApps.comInstaller.exe"
  curl -L -o "$INSTALLER_BIN" "$INSTALLER_URL" 2>/dev/null && {
    echo "  Downloaded installer, running..."
    "$INSTALLER_BIN" "$STAGE_DIR" 2>/dev/null || true
    PAF_FILE=$(find "$STAGE_DIR/.." -maxdepth 1 -name "*.paf.exe" -not -name "launcher.*" -not -name "*Installer*" | head -1)
    if [[ -n "$PAF_FILE" ]]; then
      PAF_OUT="$DIST_DIR/WinInspectPortable-${VERSION}.paf.exe"
      cp "$PAF_FILE" "$PAF_OUT"
      echo "  $PAF_OUT"
      sha256sum "$PAF_OUT" > "$PAF_OUT.sha256"
      echo "  SHA256: $(cat $PAF_OUT.sha256)"
    fi
  } || echo "  WARNING: Could not download installer. Skipping .paf.exe."
else
  echo "  SKIP: No launcher binary available. Only .zip was produced."
fi

# 7. Summary
echo "[6/6] Done."
echo ""
echo "Artifacts:"
ls -lh "$DIST_DIR"/WinInspectPortable-${VERSION}.* 2>/dev/null || true
