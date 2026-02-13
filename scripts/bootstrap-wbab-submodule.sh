#!/usr/bin/env bash
set -euo pipefail
# Add WineBotAppBuilder as a submodule in tools/WineBotAppBuilder
# Run from repo root.
if [[ ! -d .git ]]; then
  echo "Run in a git repo (git init first)."
  exit 1
fi
if [[ -d tools/WineBotAppBuilder/.git ]]; then
  echo "WBAB submodule already present."
  exit 0
fi
git submodule add https://github.com/SemperSupra/WineBotAppBuilder tools/WineBotAppBuilder
echo "WBAB submodule added. Now run: git submodule update --init --recursive"
