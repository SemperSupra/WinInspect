# WineBot Integration — Wine Testing for WinInspect

This directory provides Docker-based Wine testing infrastructure for WinInspect,
leveraging [WineBot](https://github.com/SemperSupra/WineBot) for the Wine runtime
environment.

## Quick Start

```bash
# 1. Build the daemon with MSVC (or MinGW)
cmake -B build_msvc && cmake --build build_msvc --config Release

# 2. Build and run the Wine test container
docker compose -f winebot/docker-compose.yml build
docker compose -f winebot/docker-compose.yml up -d

# 3. Test connectivity
./build_msvc/Release/wininspect.exe health --tcp localhost:1985

# 4. Cleanup
docker compose -f winebot/docker-compose.yml down
```

## Network safety

WinInspect now keeps unauthenticated TCP on loopback by default. The Wine Docker lane is an explicit test-only exception because Docker port forwarding must reach the daemon through the container interface.

The supplied Compose/CI configuration therefore starts the daemon with:

`--bind 0.0.0.0 --allow-unauthenticated-nonloopback`

while publishing TCP 1985 on the **host loopback only** (`127.0.0.1:1985:1985`). This flag is a deliberate authority-widening opt-in for a bounded container test environment; do not use it on an ordinary/native host merely for convenience. For remote/non-loopback deployments, configure authentication instead.

## CI

The `.github/workflows/wine-test.yml` workflow runs automatically on pushes to master,
building WinInspect with MinGW cross-compiler and testing it under Wine in Docker.

## Files

| File | Purpose |
|------|---------|
| `Dockerfile` | Wine test image based on WineBot's base |
| `docker-compose.yml` | Compose file for local testing |
| `README.md` | This file |

## Requirements

- Docker with Compose v2
- Internet access to pull WineBot base images from ghcr.io
- MinGW cross-compiler (for Linux CI builds) or MSVC (for local Windows builds)
