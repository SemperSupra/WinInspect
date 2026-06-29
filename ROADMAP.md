# WinInspect Roadmap & Backlog

## 🏷️ Current Release: v0.1.1

Published 2026-06-21. Installer, portable ZIP, and Go CLI available on
[GitHub Releases](https://github.com/SemperSupra/WinInspect/releases).

### What's New (since v0.0.1)
- **O(1) method dispatch** — 52-method lookup map replaces linear if-chain
- **AES-256-GCM encryption** — wired into TCP transport post-handshake
- **Runtime capability detection** — `daemon.capabilities` with 7 live probes
- **Auto-update** — `daemon.checkUpdate` + `daemon.downloadUpdate` via GitHub Releases API
- **Multi-instance** — `--pipe-name` / `--pipe` for running multiple daemons
- **Portable distribution** — `.zip` extract-anywhere + `.paf.exe` (best-effort)
- **SHA256 checksums** for all release artifacts
- **55 contract tests** covering all 51 protocol methods
- **Privacy**: env credential redaction, hostname opt-in, admin-logs gate
- **Security**: `--require-auth`, BCryptGenRandom, key caching, Ed25519 fix, `--no-clipboard`
- **Formal TLA+ v2 model** — 8 invariants validated (197M states)
- **Debug + Release CI** — 100% tests pass in both configs
- **Version alignment** — 11 versioned artifacts synced, CI enforcement
- **Connection rate limiting** — `--rate-limit-ms` flag, TCP DoS protection
- **GetDIBits pixel search** — 100-1000x faster region scanning
- **Method-level authorization** — `--allow`/`--deny` method access control
- **Granular mutex** — `snapshots_mu` replaces single global lock
- **shared_ptr snapshots** — map stores shared pointers, eliminates deep copies
- **Dispatch deduplication** — shared `request_handler.hpp` eliminates 150 LoC
- **Protocol versioning** — schema normalized to valid JSON, VERSIONING.md policy

---

## 📋 Remaining (2 issues)

| # | Item | Status |
|---|---|---|
| [#40](https://github.com/SemperSupra/WinInspect/issues/40) | v0.2.0 WineBot features | Epic tracking 8 features |
| [#41](https://github.com/SemperSupra/WinInspect/issues/41) | Formal TLA+ model update | Update model for new features, re-check |

## Deferred

| # | Item | Why |
|---|---|---|
| #16 | Watchdog timeout cancellation | Needs cooperative cancellation pattern |
| #21 | JSON config file | Needs format design |
| #25 | Audit logging | Needs file I/O design |

## Blocked (needs CI minutes)

| # | Item |
|---|---|
| #26 | Native smoke test in CI |
| #27 | TCP fuzz tests in CI |

## External Dependencies

| Repo | Issue | Status |
|---|---|---|
| mark-e-deyoung/WineBot | [#57](https://github.com/SemperSupra/WineBot/issues/57) Add LICENSE | Open |
| SemperSupra/WineBotAppBuilder | [#15](https://github.com/SemperSupra/WineBotAppBuilder/issues/15) Add LICENSE | Open |
| SemperSupra/supragoflow | [#58](https://github.com/SemperSupra/supragoflow/issues/58) Add LICENSE | Open |
| SignPath Foundation | Code signing application | Awaiting submission |
