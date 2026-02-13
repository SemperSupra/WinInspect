# Architecture

## Goals
- Works on Windows (x86/x64) and Wine (32-bit prefix + 64-bit/WoW64 prefix).
- Core features usable concurrently by CLI, GUI, and API clients without interference.
- Idempotent behavior regardless of activation path.
- Formally modeled state machines with tests that validate the model.

## Components
### Core (`core/`)
- Pure request/response logic built on an injected backend.
- Deterministic, thread-safe, no shared mutable state for inspection operations.
- Optional desired-state actions: `ensureVisible`, `ensureForeground`.

### Daemon (`daemon/`)
- `wininspectd` hosts core and exposes a local IPC API via Windows Named Pipes.
- Multi-client: one connection per client; no shared per-client selection state.
- Includes a system tray icon for basic control (About, Exit) and visibility.
- **Security:** TCP listener binds to `127.0.0.1` by default.
- **Resource Management:** 
    - Enforces a 10MB maximum message size for TCP requests.
    - Limits snapshot storage to the most recent 100 entries.

### Clients (`clients/`)
- CLI (`wininspect`): formatting and interactive loops.
- GUI (`wininspect-gui`): thin Win32 shell + tested ViewModel.
- API: any client speaking the protocol.

## Reliability choices
- Avoid DLL injection / remote hooks in v1.
- Prefer `events.subscribe + events.poll` over server-push for portability and testability.
