# WinInspect Roadmap & Backlog

## 🏷️ Current Release: v0.1.2

Published 2026-06-29. 7 WineBot integration features added.

### What's New (since v0.1.1)
- **DXGI Desktop Duplication** — GPU-accelerated screen capture with GDI fallback, `dxgi_capture` capability flag
- **Window move/resize** — `window.move` and `window.resize` via SetWindowPos
- **Desktop info** — `screen.desktopInfo` returning resolution, DPI, scale factor
- **Z-order output** — `window.getZOrder` for occlusion-aware automation
- **Process execution** — `process.execute` via CreateProcess with pipe redirection
- **Mouse drag** — `input.mouseDrag` with linear interpolation between points
- **Key combos** — `input.hotkey` supporting Ctrl+C, Alt+Tab, F-keys, and named keys
- **Multi-instance LAN analysis** — `docs/MULTI_INSTANCE_LAN.md` covers physical, VM, and container topology
- **v0.3.0 bring-up strategy** — `docs/BRINGUP_STRATEGY.md` with 8 phased milestones

---

## 🎯 Next Release: v0.3.0 — Multi-Instance LAN & Remote Access

Milestone: 9 issues, 0 closed. Target: 2026-09.

| # | Issue | Phase | Effort |
|---|---|---|---|
| [#43](https://github.com/SemperSupra/WinInspect/issues/43) | Instance identity (--instance-name, UUID) | 1 | 1-2d |
| [#44](https://github.com/SemperSupra/WinInspect/issues/44) | Dynamic port acquisition (--port 0, --port-file) | 2 | 1d |
| [#45](https://github.com/SemperSupra/WinInspect/issues/45) | Multicast discovery (mDNS/DNS-SD) | 3a | 3-5d |
| [#46](https://github.com/SemperSupra/WinInspect/issues/46) | Rendezvous discovery (HTTP registry) | 3b | 3-5d |
| [#47](https://github.com/SemperSupra/WinInspect/issues/47) | HTTP server + REST API | 4 | 5-10d |
| [#48](https://github.com/SemperSupra/WinInspect/issues/48) | WebUI dashboard | 5 | 3-5d |
| [#49](https://github.com/SemperSupra/WinInspect/issues/49) | Burst capture (screen.record) | 6 | 2-3d |
| [#50](https://github.com/SemperSupra/WinInspect/issues/50) | Integration smoke tests | 7 | 2-3d |
| [#51](https://github.com/SemperSupra/WinInspect/issues/51) | Mutual authentication (daemon → client cert) | 8 | 3-5d |

**Key documents:**
- [Multi-Instance LAN Deployment](docs/MULTI_INSTANCE_LAN.md) — topology analysis and research
- [Bring-Up Strategy](docs/BRINGUP_STRATEGY.md) — phased implementation plan with branches, testing, and PR checklists

---

## ✅ v0.2.0 — Complete

All 7 WineBot integration features implemented in this session.

| Feature | Phase | Status |
|---|---|---|
| DXGI Desktop Duplication | 1 | ✅ |
| Window move/resize | 2 | ✅ |
| Desktop info (resolution/DPI) | 3 | ✅ |
| Z-order output | 4 | ✅ |
| Process execution | 5 | ✅ |
| Mouse drag | 6 | ✅ |
| Key combos | 7 | ✅ |

---

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
