# Contracts (What must never regress)

## Core
- Inspection methods are side-effect free.
- Desired-state actions are idempotent:
  - repeating the same `ensureX` yields `changed:false` after first convergence.
- Deterministic output: under the same fake snapshot, responses are byte-identical when `canonical:true`.

## Daemon
- Multi-client non-interference: responses/events never leak across connections.
- Request routing uses per-connection IDs; daemon holds no global "selected window".

## Clients
- CLI subcommands are stable and covered by contract tests.
- GUI logic is primarily in ViewModel with unit tests; Win32 shell is thin.
