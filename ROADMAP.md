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
- **Security**: `--require-auth`, BCryptGenRandom, auth key caching, Ed25519 correct magic
- **Formal TLA+ v2 model** — 8 invariants validated (197M states)
- **Debug + Release CI** — 100% tests pass in both configs

---

## 📋 Backlog (Prioritized)

### Immediate (v0.1.x)

| # | Item | Status |
|---|---|---|
| [#15](https://github.com/SemperSupra/WinInspect/issues/15) | Connection rate limiting | Open |
| [#16](https://github.com/SemperSupra/WinInspect/issues/16) | Watchdog timeout cancellation | Open |
| [#17](https://github.com/SemperSupra/WinInspect/issues/17) | Deduplicate pipe/TCP dispatch | Open |
| [#18](https://github.com/SemperSupra/WinInspect/issues/18) | shared_ptr<Snapshot> | Open |
| [#19](https://github.com/SemperSupra/WinInspect/issues/19) | GetDIBits pixel search | Open |
| [#20](https://github.com/SemperSupra/WinInspect/issues/20) | Protocol versioning policy | Open |
| [#21](https://github.com/SemperSupra/WinInspect/issues/21) | JSON config file (`--config`) | Open |
| [#22](https://github.com/SemperSupra/WinInspect/issues/22) | Granular mutex locks | Open |
| [#23](https://github.com/SemperSupra/WinInspect/issues/23) | Method-level authorization | Open |
| [#24](https://github.com/SemperSupra/WinInspect/issues/24) | `--no-clipboard` flag | Open |
| [#25](https://github.com/SemperSupra/WinInspect/issues/25) | Audit logging | Open |
| [#26](https://github.com/SemperSupra/WinInspect/issues/26) | Native smoke test in CI | Open |
| [#27](https://github.com/SemperSupra/WinInspect/issues/27) | TCP fuzz tests in CI | Open |

### External Dependencies

| Repo | Issue | Status |
|---|---|---|
| mark-e-deyoung/WineBot | [#57](https://github.com/SemperSupra/WineBot/issues/57) Add LICENSE | Open |
| SemperSupra/WineBotAppBuilder | [#15](https://github.com/SemperSupra/WineBotAppBuilder/issues/15) Add LICENSE | Open |
| SemperSupra/supragoflow | [#58](https://github.com/SemperSupra/supragoflow/issues/58) Add LICENSE | Open |
| SignPath Foundation | Code signing application | Awaiting submission |

### Feature Requests (Unplanned)

- [#7](https://github.com/SemperSupra/WinInspect/issues/7) OCR Support
- [#8](https://github.com/SemperSupra/WinInspect/issues/8) Real-time Registry Monitoring
- [#9](https://github.com/SemperSupra/WinInspect/issues/9) Network Discovery
- [#10](https://github.com/SemperSupra/WinInspect/issues/10) Functional ImageMatch
- [#12](https://github.com/SemperSupra/WinInspect/issues/12) mDNS / Bonjour
- [#13](https://github.com/SemperSupra/WinInspect/issues/13) Request Deduplication GUIDs
- [#14](https://github.com/SemperSupra/WinInspect/issues/14) Atomic Registry Transactions

### Roadmap Items (from original ROADMAP.md)

1. **Visual Telemetry Overlay:** Real-time GDI HUD for human supervision.
2. **Semantic Action Chains:** Server-side atomic macro execution to bypass latency.
3. **Optical Character Recognition (OCR):** `screen.findText` / `screen.ocr`.
4. **Virtual Input Driver (Control):** HID-level injection, bypasses anti-automation.
5. **Advanced Hooking / Event Streaming:** `SetWindowsHookEx` for record-and-replay.
