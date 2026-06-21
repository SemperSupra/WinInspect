# TLA+ Formal Models

## Models

| File | Scope |
|---|---|
| `WinInspect_v1.tla` | Minimal: 5 methods, 2 clients, multi-client non-interference |
| `WinInspect_v2.tla` | Full protocol: 30+ methods, snapshot LRU, sessions, events, read-only mode |

## Properties Verified (v2)

| Property | What It Checks |
|---|---|
| **NonInterference** | Response IDs and session data never leak across connections |
| **DesiredStateIdempotent** | ensureVisible/ensureForeground produce `changed:false` after convergence |
| **SnapshotReferencesValid** | No response references an evicted snapshot |
| **SubscribeHasBaseline** | After subscribe, `lastSnapId != NULL` (ready for poll) |
| **ConnectionLimitEnforced** | `|conns| <= MaxConns` always |
| **SessionLimitEnforced** | `sessionCount <= MaxSessions` always |
| **ReadOnlyBlocksMutations** | Mutations are rejected when read-only mode is active |
| **ForegroundExclusive** | At most one window in foreground |

## How to Check the Model

### Prerequisites

```powershell
# Install Java
winget install EclipseAdoptium.Temurin.21.JDK

# Download TLA+ tools
curl -L -o tla2tools.jar https://github.com/tlaplus/tlaplus/releases/download/v1.8.0/tla2tools.jar
```

### Run the model checker

```bash
cd formal/tla

# Check all invariants with default model values (2 clients, 3 windows, 3 snapshots)
java -cp tla2tools.jar tlc2.TLC WinInspect_v2

# Run with larger state space (more thorough)
java -cp tla2tools.jar tlc2.TLC WinInspect_v2 -workers 4 -coverage 1
```

### Expected output

```
Model checking completed. No error has been found.
  Estimates of the probability that TLC did not examine all reachable states
  because two distinct states had the same fingerprint:
  calculated (optimistic):  val < 1.0E-6
```

### State space size

| Parameters | States |
|---|---|
| 2 clients, 3 windows, 3 snaps | ~50K |
| 3 clients, 5 windows, 5 snaps | ~2M |
| 5 clients, 10 windows, 10 snaps | ~500M (requires cloud/distributed TLC) |

## Model-to-Code Mapping

| Model Concept | Code |
|---|---|
| `Connect(c)` | `handle_client()` entry in `daemon/src/server.cpp:52` |
| `ClientDisconnect(c)` | `ConnGuard` destructor in `server.cpp:67-72` |
| `HandleOne(c)` | `server.cpp:74-252` (pipe loop) |
| `AllocateSnapshot` | `server.cpp:137-158` |
| `PinSnapshot / UnpinSnapshot` | `server.cpp:181-183` / `server.cpp:245-250` |
| `CaptureAndReturn` | `server.cpp:137-163` |
| `DesiredStateIdempotent` | `EnsureVisible` (commutative idempotent) in `core/src/win32_backend.cpp:960-968` |
| `ReadOnlyBlocksMutations` | `server.cpp:124-131` |

## Traces

The `formal/traces/` directory contains canonical JSON traces used for
contract testing:

- `system_orchestration.json` — Process, Registry, Clipboard operations
- `two_clients_non_interference.json` — Multi-client isolation
- `uia_recursive_tree.json` — UI Automation recursive discovery

These are replayed by `core/tests/test_trace_replay.cpp`.
