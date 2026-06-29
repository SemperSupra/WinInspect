# WinInspect — Remaining Issue Strategy

## Completed in This Session

| # | Issue | Branch | Status |
|---|---|---|---|
| 15 | Connection rate limiting | `feat/no-clipboard-tcp-rate-limit` | ✅ Merged |
| 19 | GetDIBits pixel search | `feat/getdibits-pixel-search` | ✅ Merged |
| 20 | Protocol versioning policy | `feat/protocol-versioning-docs` | ✅ Merged |
| 24 | --no-clipboard TCP wiring | `feat/no-clipboard-tcp-rate-limit` | ✅ Merged |

## Remaining (10 issues)

### Phase 4b: shared_ptr<Snapshot> (#18)
**Effort:** Medium. **Branch:** `feat/shared-ptr-snapshots`
- Change `std::map<std::string, Snapshot>` → `std::map<std::string, std::shared_ptr<Snapshot>>`
- Touches `server.cpp`, `tcp_server.cpp`, `server_state.hpp`
- Eliminates 40KB deep-copy per snapshot reference
- Build-verify critical since both handlers have near-identical logic

### Phase 4c: Granular mutex locking (#22)
**Effort:** Medium. **Branch:** `feat/granular-mutex`
- Split `ServerState::mu` into `snapshots_mu`, `sessions_mu`, `event_mu`
- Touches `server.cpp`, `tcp_server.cpp`, `server_state.hpp`
- Safe split: no operation currently holds more than one lock

### Phase 5: Dispatch deduplication (#17) + Watchdog timeout (#16)
**Effort:** Medium-Hard. **Branch:** `feat/dispatch-refactor`
- Extract shared request handler function from both pipe and TCP handlers
- Add cooperative cancellation for in-flight async tasks
- Largest refactor — touches both handler files heavily

### Phase 6: JSON config file (#21) + Method authorization (#23)
**Effort:** Medium each. **Branches:** `feat/json-config`, `feat/method-auth`
- Config: adds `--config <file>` flag, CLI overrides file values
- Auth: adds `--allow <methods>` and `--deny <methods>` flags

### Phase 7: Audit logging (#25)
**Effort:** Medium-Hard. **Branch:** `feat/audit-logging`
- Adds `--audit-log <file>` flag writing structured JSON entries
- Requires file I/O, rotation, and format design

### Deferred (needs CI)
| # | Issue | Why |
|---|---|---|
| 26 | Native smoke test | Needs running daemon + CLI |
| 27 | TCP fuzz tests | Needs running daemon + Python |
| 40 | v0.2.0 features | Requires integration testing |

## Branch Strategy
- Each phase gets its own branch off master
- Build-verify in Docker before merging
- Merge directly to master (no PRs — CI minutes exhausted)
- Close issues on merge
