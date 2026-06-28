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
- **Privacy**: environment variable credential redaction, hostname opt-in, admin-logs gate
- **Security**: `--require-auth`, BCryptGenRandom, auth key caching, Ed25519 correct magic, `--no-clipboard`
- **Formal TLA+ v2 model** — 8 invariants validated (197M states)
- **Debug + Release CI** — 100% tests pass in both configs
- **Version alignment** — 11 versioned artifacts synced, CI enforcement script

---

## 📋 Open Issues

### Near-Term (v0.1.x — Code Changes)

| # | Item | Priority | Why Now |
|---|---|---|---|
| [#15](https://github.com/SemperSupra/WinInspect/issues/15) | Connection rate limiting | Medium | DoS protection for TCP |
| [#18](https://github.com/SemperSupra/WinInspect/issues/18) | shared_ptr<Snapshot> | Medium | Reduces memory pressure under load |
| [#19](https://github.com/SemperSupra/WinInspect/issues/19) | GetDIBits pixel search | Low | 100-1000x faster for large regions |
| [#20](https://github.com/SemperSupra/WinInspect/issues/20) | Protocol versioning policy | Low | Schema normalization + compat policy |
| [#21](https://github.com/SemperSupra/WinInspect/issues/21) | JSON config file (`--config`) | Low | Operational convenience |
| [#22](https://github.com/SemperSupra/WinInspect/issues/22) | Granular mutex locks | Low | Concurrency under 32 clients |

### Architectural (Next Major)

| # | Item | Why Deferred |
|---|---|---|
| [#16](https://github.com/SemperSupra/WinInspect/issues/16) | Watchdog timeout cancellation | Needs cooperative cancellation pattern |
| [#17](https://github.com/SemperSupra/WinInspect/issues/17) | Deduplicate pipe/TCP dispatch | ~150 lines shared between handlers |
| [#23](https://github.com/SemperSupra/WinInspect/issues/23) | Method-level authorization | Design decision for multi-agent |
| [#25](https://github.com/SemperSupra/WinInspect/issues/25) | Audit logging | Needs file I/O + rotation design |

### CI-Dependent

| # | Item | Blocked By |
|---|---|---|
| [#26](https://github.com/SemperSupra/WinInspect/issues/26) | Native smoke test | GitHub Actions minutes |
| [#27](https://github.com/SemperSupra/WinInspect/issues/27) | TCP fuzz tests | GitHub Actions minutes |

### v0.2.0 — WineBot Integration

| # | Item | Description |
|---|---|---|
| [#40](https://github.com/SemperSupra/WinInspect/issues/40) | Roadmap Epic | DXGI capture, window management, process execution, z-order |

### Already Closed (Implemented or Triaged)

- **#10** ImageMatch — already exists as `image.match` method
- **#24** `--no-clipboard` flag — implemented in v0.1.2-dev
- **#33** Control tree — already exists as `window.getTree`
- **#35** JSON output — all methods return JSON by default
- **#37** Process info — `getInfo` returns PID + process_image
- **#39** IPC server mode — daemon already IS an IPC server

## External Dependencies

| Repo | Issue | Status |
|---|---|---|
| mark-e-deyoung/WineBot | [#57](https://github.com/SemperSupra/WineBot/issues/57) Add LICENSE | Open |
| SemperSupra/WineBotAppBuilder | [#15](https://github.com/SemperSupra/WineBotAppBuilder/issues/15) Add LICENSE | Open |
| SemperSupra/supragoflow | [#58](https://github.com/SemperSupra/supragoflow/issues/58) Add LICENSE | Open |
| SignPath Foundation | Code signing application | Awaiting submission |
