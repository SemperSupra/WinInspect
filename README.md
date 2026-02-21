# WinInspect

WinInspect is a WinSpy++-like window inspection tool designed as a **core + broker + clients** system:

- **Core** (`libwininspect_core`) is deterministic, concurrency-safe, and idempotent.
- **Broker/Daemon** (`wininspectd.exe`) exposes a local IPC API (Windows Named Pipes).
- **Clients**: CLI, Win32 native GUI, and API clients can run **concurrently** without interfering.

This repository is scaffolded to follow the **WineBotAppBuilder (WBAB)** lifecycle:
- `wbab lint`
- `wbab build`
- `wbab test`
- `wbab package`
- `wbab sign`
- `wbab smoke`
- `wbab discover`

## Quick start (developer)

### Prereqs
- Windows: Visual Studio Build Tools (MSVC), CMake >= 3.22
- Linux: optional (for running TLA+ / formatting); for Wine, run Windows binaries under Wine.

### Build + test
```bash
cmake -S . -B build -DWININSPECT_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Run daemon + CLI (Windows)
```bash
build\Debug\wininspectd.exe
build\Debug\wininspect.exe top
```

## Submodule Policy

This project follows a **Submodule Co-Evolution Policy**.
- Submodules may be modified locally for development and testing.
- **Pushes to upstream submodule repositories are strictly forbidden.**
- Upstream synchronization happens via issues and patches.

For full details, see [docs/SUBMODULE_POLICY.md](docs/SUBMODULE_POLICY.md).

To ensure your local guardrails are active, run:
```bash
./scripts/submodules/enforce-policy.sh
```

## Architecture
See:
- `docs/ARCHITECTURE.md`
- `docs/PROTOCOL.md`
- `docs/CONTRACTS.md`
- `formal/tla/`

## License
MIT (see `LICENSE`).
